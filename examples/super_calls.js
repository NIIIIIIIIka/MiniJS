class Parent {
  get() {
    return 1;
  }
}

class Child < Parent {
  get() {
    return super.get();
  }
}

let child = Child();
child.get();
