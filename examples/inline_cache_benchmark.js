class A {
  get() {
    return this.value;
  }
}

class B {
  get() {
    return this.value;
  }
}

function readName(object) {
  return object.name;
}

function writeName(object, value) {
  object.name = value;
  return value;
}

function callGet(object) {
  return object.get();
}

let first = { name: 1, extra: 10 };
let second = { extra: 20, name: 2 };
let a = A();
let b = B();
a.value = 3;
b.value = 4;

let i = 0;
let total = 0;
while (i < 2000) {
  total = total + readName(first);
  total = total + readName(second);
  total = total + writeName(first, i);
  total = total + writeName(second, i + 1);
  total = total + callGet(a);
  total = total + callGet(b);
  i = i + 1;
}

total;
