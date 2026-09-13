class A {
  get() { return 1; }
}

class B {
  get() { return 2; }
}

function read(value) {
  return value.get();
}

read(A()) + read(B()) + read(A()) + read(B());
