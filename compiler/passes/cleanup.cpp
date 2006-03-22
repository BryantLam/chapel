/*** cleanup
 ***
 *** This pass and function cleans up the AST after parsing but before
 *** scope resolution.
 ***/

#include "astutil.h"
#include "expr.h"
#include "passes.h"
#include "runtime.h"
#include "stmt.h"
#include "symbol.h"
#include "symtab.h"
#include "stringutil.h"

static void normalize_anonymous_record_or_forall_expression(DefExpr* def);
static void destructure_tuple(CallExpr* call);
static void build_constructor(ClassType* ct);
static void build_setters_and_getters(ClassType* ct);
static void flatten_primary_methods(FnSymbol* fn);
static void resolve_secondary_method_type(FnSymbol* fn);
static void add_this_formal_to_method(FnSymbol* fn);
static void hack_array(DefExpr* def);
static void construct_tuple_type(int size);


static bool stmtIsGlob(Stmt* stmt) {
  if (!stmt)
    INT_FATAL("Non-Stmt found in StmtIsGlob");
  if (ExprStmt* expr_stmt = dynamic_cast<ExprStmt*>(stmt)) {
    if (DefExpr* defExpr = dynamic_cast<DefExpr*>(expr_stmt->expr)) {
      if (dynamic_cast<FnSymbol*>(defExpr->sym) ||
          dynamic_cast<TypeSymbol*>(defExpr->sym)) {
        return true;
      }
    }
  }
  return false;
}


void createInitFn(ModuleSymbol* mod) {
  if (mod->initFn)
    return;

  currentLineno = mod->lineno;
  currentFilename = mod->filename;

  char* fnName = stringcat("__init_", mod->name);
  AList<Stmt>* globstmts = NULL;
  AList<Stmt>* initstmts = NULL;
  AList<Stmt>* definition = mod->stmts;

  // BLC: code to run user modules once only
  char* runOnce = NULL;
  if (mod != prelude && mod != compilerModule) {
    if (fnostdincs || mod != standardModule) {
      if (fnostdincs || fnostdincs_but_file) {
        definition->insertAtHead(new ImportExpr(IMPORT_USE, new SymExpr(new UnresolvedSymbol("_chpl_compiler"))));
        definition->insertAtHead(new ImportExpr(IMPORT_USE, new SymExpr(new UnresolvedSymbol("_chpl_base"))));
        definition->insertAtHead(new ImportExpr(IMPORT_USE, new SymExpr(new UnresolvedSymbol("_chpl_closure"))));
        if (fnostdincs_but_file)
          definition->insertAtHead(new ImportExpr(IMPORT_USE, new SymExpr(new UnresolvedSymbol("_chpl_file"))));
      } else
        definition->insertAtHead(new ImportExpr(IMPORT_USE, new SymExpr(new UnresolvedSymbol("_chpl_standard"))));
    }

    if (mod->modtype != MOD_INSTANTIATED) {
      runOnce = stringcat("__run_", mod->name, "_firsttime");
      DefExpr* varDefExpr = new DefExpr(new VarSymbol(runOnce, dtBool),
                                        new SymExpr(gTrue));
      compilerModule->initFn->insertAtHead(varDefExpr);
      Expr* assignVar = new CallExpr(PRIMITIVE_MOVE,
                                     new UnresolvedSymbol(runOnce),
                                     gFalse);
      definition->insertAtHead(assignVar);
    }
  }

  definition->filter(stmtIsGlob, globstmts, initstmts);

  definition = globstmts;
  BlockStmt* initFunBody;
  if (initstmts->isEmpty()) {
    initFunBody = new BlockStmt();
  } else {
    initFunBody = new BlockStmt(initstmts);
  }
  initFunBody->blkScope = mod->modScope;
  if (runOnce) {
    // put conditional in front of body
    Stmt* testRun =
      new CondStmt(
        new CallExpr(
          "!",
          new SymExpr(
            new UnresolvedSymbol(runOnce))), 
        new ReturnStmt());
    initFunBody->insertAtHead(testRun);
  }

  mod->initFn = new FnSymbol(fnName);
  mod->initFn->retType = dtVoid;
  mod->initFn->body = initFunBody;
  definition->insertAtHead(new DefExpr(mod->initFn));
  mod->stmts->insertAtHead(definition);
  clear_file_info(definition);
}


static void
process_import_expr(ImportExpr* expr) {
  if (expr->importTag == IMPORT_WITH) {
    if (TypeSymbol* ts = dynamic_cast<TypeSymbol*>(expr->parentSymbol)) {
      if (ClassType* ct = dynamic_cast<ClassType*>(ts->definition)) {
        AList<Stmt>* with_decls = expr->getStruct()->declarationList->copy();
        ct->addDeclarations(with_decls, expr->parentStmt);
        expr->parentStmt->remove();
        return;
      }
    }
    USR_FATAL(expr, "Cannot find ClassType in ImportExpr");
  }

  if (expr->importTag == IMPORT_USE) {
    ModuleSymbol* module = expr->getImportedModule();
    if (module != compilerModule)
      expr->parentStmt->insertBefore(new CallExpr(module->initFn));
    expr->parentScope->uses.add(module);
    expr->parentStmt->remove();
  }
}


static void
add_class_to_hierarchy(ClassType* ct, Vec<ClassType*>* seen = NULL) {
  if (seen == NULL) {
    Vec<ClassType*> seen;
    if (ct->inherits)
      add_class_to_hierarchy(ct, &seen);
    return;
  }

  forv_Vec(ClassType, at, *seen) {
    if (at == ct) {
      USR_FATAL(ct, "Cyclic class hierarchy detected");
    }
  }

  for_alist(Expr, expr, ct->inherits) {
    SymExpr* symExpr = dynamic_cast<SymExpr*>(expr);
    if (!symExpr) {
      USR_FATAL_CONT(symExpr, "Possible temporary restriction follows:");
      USR_FATAL(symExpr, "Inheritance is only supported from simple classes");
    }
    Symbol* symbol = Symboltable::lookupFromScope(symExpr->var->name, symExpr->parentScope);
    TypeSymbol* typeSymbol = dynamic_cast<TypeSymbol*>(symbol);
    if (!typeSymbol)
      USR_FATAL(expr, "Illegal to inherit from something other than a class");
    ClassType* pt = dynamic_cast<ClassType*>(typeSymbol->definition);
    if (!pt)
      USR_FATAL(expr, "Illegal to inherit from something other than a class");
    if (pt->classTag == CLASS_RECORD)
      USR_FATAL(expr, "Illegal to inherit from record");
    if (ct->classTag == CLASS_RECORD)
      USR_FATAL(expr, "Illegal for record to inherit");
    if (pt->inherits) {
      seen->add(ct);
      add_class_to_hierarchy(pt, seen);
    }
    ct->dispatchParents.add(pt);
    Stmt* insertPoint = ct->declarationList->first();
    forv_Vec(Symbol, field, pt->fields) {
      ct->addDeclarations(new AList<Stmt>(field->defPoint->parentStmt->copy()), insertPoint);
    }
    if (pt->classTag == CLASS_VALUECLASS) {
      ct->classTag = CLASS_VALUECLASS;
      ct->defaultValue = NULL;
    }
  }
  if (ct->dispatchParents.n == 0 && ct != dtObject && ct != dtValue) {
    if (ct->classTag == CLASS_RECORD)
      ct->dispatchParents.add(dtValue);
    else
      ct->dispatchParents.add(dtObject);
  }
  if (ct == dtValue) {
    ct->classTag = CLASS_VALUECLASS;
    ct->defaultValue = NULL;
  }
  ct->inherits = NULL;
}


void cleanup(void) {
  forv_Vec(ModuleSymbol, mod, allModules)
    createInitFn(mod);

  if (!fnostdincs && !fnostdincs_but_file) {
    construct_tuple_type(1);
    construct_tuple_type(2);
    construct_tuple_type(3);
    construct_tuple_type(4);
    construct_tuple_type(5);
  }

  forv_Vec(ModuleSymbol, mod, allModules) {
    Vec<BaseAST*> asts;
    collect_asts(&asts, mod);
    forv_Vec(BaseAST, ast, asts) {
      if (ImportExpr* a = dynamic_cast<ImportExpr*>(ast)) {
        process_import_expr(a);
      }
    }
  }

  forv_Vec(ModuleSymbol, mod, allModules)
    cleanup(mod);
}


void cleanup(BaseAST* base) {
  Vec<BaseAST*> asts;
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    if (ModuleSymbol* a = dynamic_cast<ModuleSymbol*>(ast))
      createInitFn(a);
  }

  asts.clear();
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    if (ImportExpr* a = dynamic_cast<ImportExpr*>(ast)) {
      process_import_expr(a);
    }
  }

  asts.clear();
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    if (DefExpr* def = dynamic_cast<DefExpr*>(ast)) {
      if (TypeSymbol* ts = dynamic_cast<TypeSymbol*>(def->sym)) {
        if (ClassType* ct = dynamic_cast<ClassType*>(ts->definition)) {
          add_class_to_hierarchy(ct);
        }
      }
    }
  }

  asts.clear();
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    currentLineno = ast->lineno;
    currentFilename = ast->filename;
    if (DefExpr* a = dynamic_cast<DefExpr*>(ast)) {
      normalize_anonymous_record_or_forall_expression(a);
    } else if (CallExpr* a = dynamic_cast<CallExpr*>(ast)) {
      SymExpr* base = dynamic_cast<SymExpr*>(a->baseExpr);
      if (base && !strncmp(base->var->name, "_construct__tuple", 6)) {
        CallExpr* parent = dynamic_cast<CallExpr*>(a->parentExpr);
        if (parent && parent->isAssign() && parent->get(1) == a)
          destructure_tuple(parent);
      }
    }
  }

  asts.clear();
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    if (TypeSymbol* type = dynamic_cast<TypeSymbol*>(ast)) {
      if (ClassType* ct = dynamic_cast<ClassType*>(type->definition)) {
        build_constructor(ct);
        build_setters_and_getters(ct);
      }
    }
  }

  asts.clear();
  collect_asts(&asts, base);
  forv_Vec(BaseAST, ast, asts) {
    currentLineno = ast->lineno;
    currentFilename = ast->filename;
    if (FnSymbol* fn = dynamic_cast<FnSymbol*>(ast)) {
      flatten_primary_methods(fn);
      resolve_secondary_method_type(fn);
      add_this_formal_to_method(fn);
    }
    if (DefExpr* def = dynamic_cast<DefExpr*>(ast)) {
      hack_array(def);
    }
  }
}


/*** normalize_anonymous_record_or_forall_expression
 ***   moves anonymous record definition into separate statement
 ***   moves anonymous forall expression into separate statement
 ***   NOTE: during parsing, these may be embedded in expressions
 ***/
static void normalize_anonymous_record_or_forall_expression(DefExpr* def) {
  if (!def->parentStmt || (!def->parentExpr && def->parentStmt->astType == STMT_EXPR))
    return;
  Stmt* stmt = def->parentStmt;
  if (stmt->getFunction() == stmt->getModule()->initFn)
    stmt = dynamic_cast<Stmt*>(stmt->getFunction()->defPoint->parentStmt->next);
  if ((!strncmp("_anon_record", def->sym->name, 12)) ||
      (!strncmp("_forallexpr", def->sym->name, 11))) {
    def->replace(new SymExpr(def->sym));
    stmt->insertBefore(def);
  } else if ((!strncmp("_let_fn", def->sym->name, 7)) ||
             (!strncmp("_if_fn", def->sym->name, 6))) {
    def->replace(new CallExpr(def->sym->name));
    stmt->insertBefore(def);
  }
}


/*** destructure_tuple
 ***
 ***   (i,j) = expr;    ==>    i = expr(1);
 ***                           j = expr(2);
 ***
 ***   NOTE: handles recursive tuple destructuring, (i,(j,k)) = ...
 ***/
static void destructure_tuple(CallExpr* call) {
  Stmt* stmt = call->parentStmt;
  VarSymbol* temp = new VarSymbol("_tuple_destruct");
  stmt->insertBefore(new DefExpr(temp));
  CallExpr* tuple = dynamic_cast<CallExpr*>(call->get(1));
  call->replace(new CallExpr(PRIMITIVE_MOVE, temp, call->get(2)->remove()));
  int i = 1;
  int length = tuple->argList->length();
  for_alist(Expr, expr, tuple->argList) {
    if (i - length/2 >= 1)
      stmt->insertAfter(
        new CallExpr("=", expr->remove(),
          new CallExpr(temp, new_IntLiteral(i-length/2))));
    i++;
  }
}


static void build_constructor(ClassType* ct) {
  if (ct->defaultConstructor)
    return;

  char* name = stringcat("_construct_", ct->symbol->name);
  FnSymbol* fn = new FnSymbol(name);
  ct->defaultConstructor = fn;
  fn->fnClass = FN_CONSTRUCTOR;
  fn->cname = stringcat("_construct_", ct->symbol->cname);

  AList<DefExpr>* args = new AList<DefExpr>();
  args->head->parentSymbol = fn;

  forv_Vec(TypeSymbol, type, ct->types) {
    if (VariableType* vt = dynamic_cast<VariableType*>(type->definition)) {
      ArgSymbol* arg = new ArgSymbol(INTENT_TYPE, type->name, vt->type);
      arg->isGeneric = true;
      arg->genericSymbol = type;
      args->insertAtTail(new DefExpr(arg));
    }
  }

  forv_Vec(Symbol, tmp, ct->fields) {
    char* name = tmp->name;
    Type* type = tmp->type;
    Expr* exprType = tmp->defPoint->exprType;
    if (exprType)
      exprType->remove();
    Expr* init = tmp->defPoint->init;
    if (init)
      init->remove();
    else
      init = new SymExpr(gNil);
    VarSymbol *vtmp = dynamic_cast<VarSymbol*>(tmp);
    ArgSymbol* arg = new ArgSymbol((vtmp && vtmp->consClass == VAR_PARAM) ? INTENT_PARAM : INTENT_BLANK, name, type, init);
    DefExpr* defExpr = new DefExpr(arg, NULL, exprType);
    args->insertAtTail(defExpr);
  }

  fn->formals = args;

  reset_file_info(fn, ct->symbol->lineno, ct->symbol->filename);
  ct->symbol->defPoint->parentStmt->insertBefore(new DefExpr(fn));
  //  ct->methods.add(fn);
//   if (ct->symbol->hasPragma("data class")) {
//     fn->addPragma("rename _data_construct");
//     fn->addPragma("no codegen");
//   }
  fn->typeBinding = ct->symbol;

  fn->_this = new VarSymbol("this", ct);
  dynamic_cast<VarSymbol*>(fn->_this)->noDefaultInit = true;

  char* description = stringcat("instance of class ", ct->symbol->name);
  Expr* alloc_rhs = new CallExpr(Symboltable::lookupInScope("_chpl_alloc", prelude->modScope),
                                 ct->symbol,
                                 new_StringLiteral(description));
  CallExpr* alloc_expr = new CallExpr(PRIMITIVE_MOVE, fn->_this, alloc_rhs);

  fn->insertAtTail(new DefExpr(fn->_this));
  fn->insertAtTail(alloc_expr);

  // assign formals to fields by name
  forv_Vec(Symbol, field, ct->fields) {
    for_alist(DefExpr, formalDef, fn->formals) {
      if (ArgSymbol* formal = dynamic_cast<ArgSymbol*>(formalDef->sym)) {
        if (!strcmp(formal->name, field->name)) {
          Expr* assign_expr = new CallExpr(PRIMITIVE_SET_MEMBER, fn->_this, 
                                           new_StringSymbol(field->name), formal);
          fn->insertAtTail(assign_expr);
        }
      }
    }
  }

  forv_Vec(FnSymbol, method, ct->methods) {
    if (!strcmp(method->name, "initialize")) {
      if (method->formals->length() == 0) {
        fn->insertAtTail(new CallExpr("initialize"));
        break;
      }
    }
  }

  fn->insertAtTail(new ReturnStmt(fn->_this));
  fn->retType = ct;
}


static void build_getter(ClassType* ct, Symbol *field) {
  forv_Vec(FnSymbol, fn, ct->methods) {
    if (fn->_getter == field)
      return;
  }

  FnSymbol* fn = new FnSymbol(field->name);
  fn->addPragma("inline");
  fn->_getter = field;
  fn->retType = field->type;
  ArgSymbol* _this = new ArgSymbol(INTENT_REF, "this", ct);
  fn->formals = new AList<DefExpr>(
    new DefExpr(new ArgSymbol(INTENT_REF, "_methodTokenDummy", dtMethodToken)),
    new DefExpr(_this));
  fn->body = new BlockStmt(new ReturnStmt(new CallExpr(PRIMITIVE_GET_MEMBER, new SymExpr(_this), new SymExpr(new_StringSymbol(field->name)))));
  DefExpr* def = new DefExpr(fn);
  ct->symbol->defPoint->parentStmt->insertBefore(def);
  reset_file_info(fn, field->lineno, field->filename);
  ct->methods.add(fn);
  fn->method_type = PRIMARY_METHOD;
  fn->typeBinding = ct->symbol;
  fn->cname = stringcat("_", fn->typeBinding->cname, "_", fn->cname);
  fn->noParens = true;
  fn->_this = _this;
}


static void build_setter(ClassType* ct, Symbol* field) {
  forv_Vec(FnSymbol, fn, ct->methods) {
    if (fn->_setter == field)
      return;
  }

  FnSymbol* fn = new FnSymbol(field->name);
  fn->addPragma("inline");
  fn->_setter = field;
  fn->retType = dtVoid;

  ArgSymbol* _this = new ArgSymbol(INTENT_REF, "this", ct);
  ArgSymbol* fieldArg = new ArgSymbol(INTENT_BLANK, "_arg", (no_infer) ? field->type : dtUnknown);
  DefExpr* argDef = new DefExpr(fieldArg);
  if (no_infer && field->defPoint->exprType)
    argDef->exprType = field->defPoint->exprType->copy();
  fn->formals = new AList<DefExpr>(
    new DefExpr(new ArgSymbol(INTENT_REF, "_methodTokenDummy", dtMethodToken)),
    new DefExpr(_this), 
    new DefExpr(new ArgSymbol(INTENT_REF, "_setterTokenDummy", dtSetterToken)),
    argDef);
  Expr *valExpr = new CallExpr(PRIMITIVE_GET_MEMBER, new SymExpr(_this), new SymExpr(new_StringSymbol(field->name)));
  Expr *assignExpr = new CallExpr("=", valExpr, fieldArg);
  fn->body->insertAtTail(
    new CallExpr(PRIMITIVE_SET_MEMBER, new SymExpr(_this), new SymExpr(new_StringSymbol(field->name)), assignExpr));
  fn->body->insertAtTail(new ReturnStmt(new SymExpr(_this)));
  ct->symbol->defPoint->parentStmt->insertBefore(new DefExpr(fn));
  reset_file_info(fn, field->lineno, field->filename);

  ct->methods.add(fn);
  fn->method_type = PRIMARY_METHOD;
  fn->typeBinding = ct->symbol;
  fn->cname = stringcat("_", fn->typeBinding->cname, "_", fn->cname);
  fn->noParens = true;
  fn->_this = _this;
}


static void build_setters_and_getters(ClassType* ct) {
  forv_Vec(Symbol, field, ct->fields) {
    build_setter(ct, field);
    build_getter(ct, field);
  }
  forv_Vec(TypeSymbol, tmp, ct->types) {
    if (tmp->type->astType == TYPE_USER || tmp->type->astType == TYPE_VARIABLE)
      build_getter(ct, tmp);
  }
}


static void flatten_primary_methods(FnSymbol* fn) {
  if (dynamic_cast<TypeSymbol*>(fn->defPoint->parentSymbol)) {
    Stmt* insertPoint = fn->typeBinding->defPoint->parentStmt;
    if (!fn->typeBinding)
      INT_FATAL(fn, "Primary method has no typeBinding");
    while (dynamic_cast<TypeSymbol*>(insertPoint->parentSymbol))
      insertPoint = insertPoint->parentSymbol->defPoint->parentStmt;
    Stmt* stmt = fn->defPoint->parentStmt;
    stmt->remove();
    insertPoint->insertBefore(stmt);
  }
}


static void resolve_secondary_method_type(FnSymbol* fn) {
  if (fn->typeBinding && fn->typeBinding->isUnresolved) {
    Symbol* typeBindingSymbol = Symboltable::lookupFromScope(fn->typeBinding->name, fn->parentScope);
    assert(!typeBindingSymbol->isUnresolved);
    if (TypeSymbol *ts = dynamic_cast<TypeSymbol*>(typeBindingSymbol)) {
      Type* typeBinding = ts->definition;
      fn->typeBinding = ts;
      if (fn->fnClass != FN_CONSTRUCTOR) {
        fn->method_type = SECONDARY_METHOD;
      }
      typeBinding->methods.add(fn);
    } else {
      USR_FATAL(fn, "Function is not bound to type");
    }
  }
}


static void add_this_formal_to_method(FnSymbol* fn) {
  if (fn->_this) // already added
    return;
  if (fn->typeBinding && fn->fnClass != FN_CONSTRUCTOR) {
    fn->cname = stringcat("_", fn->typeBinding->cname, "_", fn->cname);
    ArgSymbol* this_insert = new ArgSymbol(INTENT_REF, "this", fn->typeBinding->definition);
    fn->formals->insertAtHead(new DefExpr(this_insert));
    fn->_this = this_insert;
    if (strcmp(fn->name, "this")) {
      ArgSymbol* token_dummy = new ArgSymbol(INTENT_REF, "_methodTokenDummy",
                                             dtMethodToken);
      fn->formals->insertAtHead(new DefExpr(token_dummy));
    }
    if (fn->isSetter) {
      ArgSymbol* setter_dummy = new ArgSymbol(INTENT_REF, "_setterTokenDummy", 
                                              dtSetterToken);
      fn->formals->last()->insertBefore(new DefExpr(setter_dummy));
    }
  }
}


static void hack_array(DefExpr* def) {
  if (ArgSymbol* arg = dynamic_cast<ArgSymbol*>(def->sym)) {
    // handle arrays in constructors for arrays as fields
    if (CallExpr* type = dynamic_cast<CallExpr*>(def->exprType)) {
      if (type->isNamed("_build_array_type") ||
          type->isNamed("_build_sparse_domain_type") ||
          type->isNamed("_build_domain_type")) {
        if (!arg->defaultExpr)
          INT_FATAL(def, "Clean up arrays!!!");
        Expr* expr = def->exprType;
        expr->remove();
        arg->defaultExpr->replace(expr);
      }
    }
  } else if (VarSymbol* var = dynamic_cast<VarSymbol*>(def->sym)) {
    if (CallExpr* type = dynamic_cast<CallExpr*>(def->exprType)) {
      if (type->isNamed("_build_array_type") ||
          type->isNamed("_build_sparse_domain_type") ||
          type->isNamed("_build_domain_type")) {
        if (def->init) {
          Expr* init = def->init;
          init->remove();
          def->parentStmt->insertAfter(new CallExpr("=", def->sym, init));
        }
        def->init = def->exprType;
        def->exprType = NULL;
        var->noDefaultInit = true;
      }
    }
  }
}


/*** construct_tuple_type
 ***   builds rank-dependent tuple type
 ***/
static void construct_tuple_type(int rank) {
  currentLineno = 0;

  char *name = stringcat("_tuple", intstring(rank));

  if (Symboltable::lookupInScope(name, tupleModule->modScope))
    return;

  AList<Stmt>* decls = new AList<Stmt>();

  // Build type declarations
  Vec<Type*> types;
  for (int i = 1; i <= rank; i++) {
    char* typeName = stringcat("_t", intstring(i));
    VariableType* type = new VariableType(getMetaType(0));
    TypeSymbol* typeSymbol = new TypeSymbol(typeName, type);
    type->addSymbol(typeSymbol);
    decls->insertAtTail(new DefExpr(typeSymbol));
    types.add(type);
  }

  // Build field declarations
  Vec<VarSymbol*> fields;
  for (int i = 1; i <= rank; i++) {
    char* fieldName = stringcat("_f", intstring(i));
    VarSymbol* field = new VarSymbol(fieldName, types.v[i-1]);
    decls->insertAtTail(new DefExpr(field));
    fields.add(field);
  }

  // Build this methods
  for (int i = 1; i <= rank; i++) {
    FnSymbol* fn = new FnSymbol("this");
    ArgSymbol* arg = new ArgSymbol(INTENT_BLANK, "index", new_LiteralType(new_IntSymbol(i)));
    fn->formals = new AList<DefExpr>(new DefExpr(arg));

    fn->retRef = true;
    fn->body = new BlockStmt(new ReturnStmt(fields.v[i-1]->name));
    DefExpr* def = new DefExpr(fn);
    if (no_infer)
      fn->retExpr = new SymExpr(types.v[i-1]->symbol);
    decls->insertAtTail(def);
  }

  // Build tuple
  ClassType* tupleType = new ClassType(CLASS_RECORD);
  TypeSymbol* tupleSym = new TypeSymbol(name, tupleType);
  tupleType->addSymbol(tupleSym);
  tupleType->addDeclarations(decls);
  tupleModule->stmts->insertAtHead(new DefExpr(tupleSym));

  if (!fnostdincs) {
    // Build write function
    FnSymbol* fwriteFn = new FnSymbol("fwrite");
    TypeSymbol* fileType = dynamic_cast<TypeSymbol*>(Symboltable::lookupInFileModuleScope("file"));
    ArgSymbol* fileArg = new ArgSymbol(INTENT_BLANK, "f", fileType->definition);
    ArgSymbol* fwriteArg = new ArgSymbol(INTENT_BLANK, "val", tupleType);
    fwriteFn->formals = new AList<DefExpr>(new DefExpr(fileArg), new DefExpr(fwriteArg));
    AList<Expr>* actuals = new AList<Expr>();
    actuals->insertAtTail(new_StringLiteral(stringcpy("(")));
    for (int i = 1; i <= rank; i++) {
      if (i != 1)
        actuals->insertAtTail(new_StringLiteral(stringcpy(", ")));
      actuals->insertAtTail(
        new CallExpr(".", new SymExpr("val"),
          new_StringSymbol(stringcat("_f", intstring(i)))));
    }
    actuals->insertAtTail(new_StringLiteral(stringcpy(")")));
    Expr* fwriteCall = new CallExpr("fwrite", new SymExpr(fileArg), actuals);
    fwriteFn->body = new BlockStmt(new ExprStmt(fwriteCall));
    tupleModule->stmts->insertAtTail(new DefExpr(fwriteFn));
  }

  // Build htuple = tuple function
  if (!fnostdincs && !fnostdincs_but_file) {
    FnSymbol* assignFn = new FnSymbol("=");
    ArgSymbol* htupleArg = 
      new ArgSymbol(INTENT_BLANK, "_htuple", chpl_htuple->definition);
    ArgSymbol* tupleArg = new ArgSymbol(INTENT_BLANK, "val", tupleType);
    assignFn->formals = new AList<DefExpr>(new DefExpr(htupleArg),
                                           new DefExpr(tupleArg));
    assignFn->body = new BlockStmt();
    for (int i = 1; i <= rank; i++) {
      assignFn->insertAtTail(
        new CallExpr("=",
          new CallExpr(htupleArg, new_IntLiteral(i)),
          new CallExpr(tupleArg, new_IntLiteral(i))));
    }
    assignFn->insertAtTail(new ReturnStmt(htupleArg));
    tupleModule->stmts->insertAtTail(new DefExpr(assignFn));
  }

  // Build tuple = _ function
//   {
//     FnSymbol* assignFn = new FnSymbol("=");
//     ArgSymbol* tupleArg = new ArgSymbol(INTENT_BLANK, "tuple", tupleType);
//     ArgSymbol* secondArg = new ArgSymbol(INTENT_BLANK, "val", dtUnknown);
//     assignFn->formals = new AList<DefExpr>(new DefExpr(tupleArg),
//                                            new DefExpr(secondArg));
//     assignFn->body = new BlockStmt();
//     for (int i = 1; i <= rank; i++) {
//       assignFn->insertAtTail(
//         new ExprStmt(
//           new CallExpr(PRIMITIVE_MOVE,
//             new CallExpr(tupleArg, new_IntLiteral(i)),
//             new CallExpr(secondArg, new_IntLiteral(i)))));
//     }
//     assignFn->insertAtTail(new ReturnStmt(tupleArg));
//     tupleModule->stmts->insertAtTail(new ExprStmt(new DefExpr(assignFn)));
//   }
}
