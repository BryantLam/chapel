#ifndef _CHPL_LIST_H_
#define _CHPL_LIST_H_

// Lists for use with Stmts and Exprs (only)

#include <stdio.h>
#include "chpl.h"
#include "astutil.h"
#include "baseAST.h"

template <class elemType>
class AList : public BaseAST {
 public:
  elemType* head;
  elemType* tail;

  // constructors
  AList();
  AList(BaseAST*);
  AList(BaseAST*, BaseAST*);
  AList(BaseAST*, BaseAST*, BaseAST*);
  AList(BaseAST*, BaseAST*, BaseAST*, BaseAST*);
  void clear(void);

  // copy routines
  virtual AList<elemType>* copy(ASTMap* map = NULL,
                                Vec<BaseAST*>* update_list = NULL,
                                bool internal = false);

  // checks for length
  bool isEmpty(void);
  int length(void);

  // iteration
  elemType* first(void); // begin iteration over a list
  elemType* last(void);  // begin backwards iteration over a list

  // other ways to get elements from the list
  elemType* only(void); // return the single element in a list
  elemType* get(int index); // get the index-th element in a list

  // add element(s) at beginning of list
  void insertAtHead(BaseAST* new_ast);

  // add element(s) at end of list
  void insertAtTail(BaseAST* new_ast);

  // helper routines for insertAfter/insertBefore
  bool isList(void);
  void insertBeforeListHelper(BaseAST* ast);
  void insertAfterListHelper(BaseAST* ast);

  // different ways to print/codegen lists
  void print(FILE* outfile, char* separator = ", ");
  void printDef(FILE* outfile, char* separator = ", ");
  void codegen(FILE* outfile, char* separator = ", ");
  void codegenDef(FILE* outfile, char* separator = ", ");
};

#define for_alist(elemtype, node, list)  \
  for (elemtype *node = (list) ? ((list)->head ? dynamic_cast<elemtype*>((list)->head->next) : NULL) : NULL,      \
         *_alist_next = (node) ? dynamic_cast<elemtype*>((node)->next) : NULL;                  \
       _alist_next;                                                                             \
       node = _alist_next,                                                                      \
         _alist_next = (node) ? dynamic_cast<elemtype*>((node)->next) : NULL)

#define for_formals(formal, fn)                                         \
  for (ArgSymbol *formal = ((fn)->formals) ? (((fn)->formals)->head ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>(((fn)->formals)->head->next)->sym) : NULL) : NULL, \
         *_alist_next = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->next)->sym) : NULL; \
       (formal);                                                        \
       formal = _alist_next,                                            \
         _alist_next = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->next)->sym) : NULL)

#define for_formals_backward(formal, fn)                                \
  for (ArgSymbol *formal = ((fn)->formals) ? (((fn)->formals)->tail ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>(((fn)->formals)->tail->prev)->sym) : NULL) : NULL, \
         *_alist_prev = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->prev)->sym) : NULL; \
       (formal);                                                        \
       formal = _alist_prev,                                            \
         _alist_prev = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->prev)->sym) : NULL)

#define for_formals_actuals(formal, actual, call)                       \
  FnSymbol* _alist_fn = (call)->isResolved();                             \
  if (_alist_fn->formals->length() != (call)->argList->length())        \
    INT_FATAL(call, "number of actuals does not match number of formals"); \
  int _alist_i = 1;                                                       \
  Expr* actual = ((call)->argList->length() >= _alist_i) ? (call)->get(_alist_i) : NULL; \
  for (ArgSymbol *formal = ((_alist_fn)->formals) ? (((_alist_fn)->formals)->head ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>(((_alist_fn)->formals)->head->next)->sym) : NULL) : NULL, \
         *_alist_next = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->next)->sym) : NULL; \
       (formal);                                                        \
       formal = _alist_next,                                            \
         _alist_next = (formal) ? dynamic_cast<ArgSymbol*>(dynamic_cast<DefExpr*>((formal)->defPoint->next)->sym) : NULL, \
         _alist_i++, actual = formal ? (call)->get(_alist_i) : NULL)

#define for_actuals(actual, call)               \
  for_alist(Expr, actual, (call)->argList)

#define for_actuals_backward(actual, call)               \
  for_alist_backward(Expr, actual, (call)->argList)

#define for_fields(field, ct)                                           \
  for (Symbol *field = ((ct)->fields) ? (((ct)->fields)->head ? dynamic_cast<DefExpr*>(((ct)->fields)->head->next)->sym : NULL) : NULL, \
         *_alist_next = (field) ? dynamic_cast<DefExpr*>((field)->defPoint->next)->sym : NULL; \
       (field);                                                         \
       field = _alist_next,                                             \
         _alist_next = (field) ? dynamic_cast<DefExpr*>((field)->defPoint->next)->sym : NULL)

#define for_fields_backward(field, ct)                                  \
  for (Symbol *field = ((ct)->fields) ? (((ct)->fields)->tail ? dynamic_cast<DefExpr*>(((ct)->fields)->tail->prev)->sym : NULL) : NULL, \
         *_alist_prev = (field) ? dynamic_cast<DefExpr*>((field)->defPoint->prev)->sym : NULL; \
       (field);                                                         \
       field = _alist_prev,                                             \
         _alist_prev = (field) ? dynamic_cast<DefExpr*>((field)->defPoint->prev)->sym : NULL)

#define for_alist_backward(elemtype, node, list)  \
  for (elemtype *node = (list) ? ((list)->tail ? dynamic_cast<elemtype*>((list)->tail->prev) : NULL) : NULL,      \
         *_alist_prev = (node) ? dynamic_cast<elemtype*>((node)->prev) : NULL;                  \
       _alist_prev;                                                                             \
       node = _alist_prev,                                                                      \
         _alist_prev = (node) ? dynamic_cast<elemtype*>((node)->prev) : NULL)


template <class elemType>
AList<elemType>::AList() :
  BaseAST(LIST),
  head(new elemType()),
  tail(new elemType())
{
  clear();
}


template <class elemType>
AList<elemType>::AList(BaseAST* elem1) :
  BaseAST(LIST),
  head(new elemType()),
  tail(new elemType())
{
  clear();
  if (elem1) insertAtTail(elem1);
}


template <class elemType>
AList<elemType>::AList(BaseAST* elem1, BaseAST* elem2) :
  BaseAST(LIST),
  head(new elemType()),
  tail(new elemType())
{
  clear();
  if (elem1) insertAtTail(elem1);
  if (elem2) insertAtTail(elem2);
}


template <class elemType>
AList<elemType>::AList(BaseAST* elem1, BaseAST* elem2, BaseAST* elem3) :
  BaseAST(LIST),
  head(new elemType()),
  tail(new elemType())
{
  clear();
  if (elem1) insertAtTail(elem1);
  if (elem2) insertAtTail(elem2);
  if (elem3) insertAtTail(elem3);
}


template <class elemType>
AList<elemType>::AList(BaseAST* elem1, BaseAST* elem2,
                       BaseAST* elem3, BaseAST* elem4) :
  BaseAST(LIST),
  head(new elemType()),
  tail(new elemType())
{
  clear();
  if (elem1) insertAtTail(elem1);
  if (elem2) insertAtTail(elem2);
  if (elem3) insertAtTail(elem3);
  if (elem4) insertAtTail(elem4);
}


template <class elemType>
void AList<elemType>::clear(void) {
  head->next = tail;
  tail->prev = head;
}


template <class elemType>
bool AList<elemType>::isEmpty(void) {
  if (this == NULL) {
    return true;
  } else {
    return (head->next == tail);
  }
}


template <class elemType>
int AList<elemType>::length(void) {
  int numNodes = 0;
  for_alist(elemType, node, this) {
    numNodes++;
  }
  return numNodes;
}


template <class elemType>
elemType* AList<elemType>::first(void) {
  if (this == NULL || isEmpty())
    return NULL;
  return dynamic_cast<elemType*>(head->next);
}


template <class elemType>
elemType* AList<elemType>::last(void) {
  if (this == NULL || isEmpty())
    return NULL;
  return dynamic_cast<elemType*>(tail->prev);
}


template <class elemType>
elemType* AList<elemType>::only(void) {
  if (isEmpty()) {
    INT_FATAL(this, "only() called on empty list");
  }
  if (head->next->next == tail) {
    return first();
  } else {
    INT_FATAL(this, "only() called on list with more than one element");
    return NULL;
  }
}


template <class elemType>
elemType* AList<elemType>::get(int index) {
  if (index <=0) {
    INT_FATAL(this, "Indexing list must use positive integer");
  }
  int i = 0;
  for_alist(elemType, node, this) {
    i++;
    if (i == index) {
      return node;
    }
  }
  INT_FATAL(this, "Indexing list out of bounds");
  return NULL;
}


template <class elemType>
void AList<elemType>::insertAtHead(BaseAST* new_ast) {
  if (new_ast->parentSymbol) {
    INT_FATAL(new_ast, "Argument is already in AST in AList::insertAtHead");
  }
  head->next->insertBefore(new_ast);
}


template <class elemType>
void AList<elemType>::insertAtTail(BaseAST* new_ast) {
  if (new_ast->parentSymbol) {
    INT_FATAL(new_ast, "Argument is already in AST in AList::insertAtTail");
  }
  tail->prev->insertAfter(new_ast);
}


template <class elemType>
bool AList<elemType>::isList(void) {
  return true;
}


template <class elemType>
void AList<elemType>::insertBeforeListHelper(BaseAST* ast) {
  for_alist(elemType, elem, this) {
    elem->remove();
    ast->insertBefore(elem);
  }
}


template <class elemType>
void AList<elemType>::insertAfterListHelper(BaseAST* ast) {
  for_alist_backward(elemType, elem, this) {
    elem->remove();
    ast->insertAfter(elem);
  }
}


template <class elemType>
void AList<elemType>::print(FILE* outfile, char* separator) {
  for_alist(elemType, node, this) {
    node->print(outfile);
    if (node->next != tail) {
      fprintf(outfile, "%s", separator);
    }
  }
}


template <class elemType>
void AList<elemType>::printDef(FILE* outfile, char* separator) {
  for_alist(elemType, node, this) {
    node->printDef(outfile);
    if (node->next != tail) {
      fprintf(outfile, "%s", separator);
    }
  }
}


template <class elemType>
void AList<elemType>::codegen(FILE* outfile, char* separator) {
  for_alist(elemType, node, this) {
    node->codegen(outfile);
    if (node->next != tail) {
      fprintf(outfile, "%s", separator);
    }
  }
}



template <class elemType>
void AList<elemType>::codegenDef(FILE* outfile, char* separator) {
  for_alist(elemType, node, this) {
    node->codegenDef(outfile);
    if (node->next != tail) {
      fprintf(outfile, "%s", separator);
    }
  }
}


template <class elemType>
AList<elemType>*
AList<elemType>::copy(ASTMap* map,
                      Vec<BaseAST*>* update_list,
                      bool internal) {
  if (isEmpty()) {
    return new AList<elemType>();
  }

  if (!map) {
    map = new ASTMap();
  }

  AList<elemType>* newList = new AList<elemType>();
  for_alist(elemType, node, this) {
    elemType* newnode = COPY_INT(node);
    newnode->next = NULL;
    newnode->prev = NULL;
    newList->insertAtTail(newnode);
  }

  newList->lineno = lineno;
  newList->filename = filename;
  if (!internal) {
    if (update_list) {
      for (int j = 0; j < update_list->n; j++) {
        for (int i = 0; i < map->n; i++) {
          if (update_list->v[j] == map->v[i].key) {
            update_list->v[j] = map->v[i].value;
          }
        }
      }
    }
    for_alist(elemType, node, newList)
      update_symbols(node, map);
  }
  return newList;
}

#endif
