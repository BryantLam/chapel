record C {
  var i : integer = 2;
}

record D {
  var j : integer = 3;
}

function =(d : D, c : C) {
  d.j = c.i;
  return d;
}

var c : C;
var d : D;

writeln(c);
writeln(d);

d = c;

writeln(d);
