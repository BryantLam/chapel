class Foo {
  var x: int;

  proc init(xVal) {
    cobegin {
      {
        on xVal.locale {
          x = xVal;
        }
      }
      writeln("In a cobegin, whee!");
    }
    super.init();
  }
}

var foo = new Foo(14);
writeln(foo);
delete foo;