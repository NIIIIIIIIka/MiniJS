class Parent {
  value() {
    return 10;
  }
}

class Child < Parent {
  value() {
    return super.value() + 1;
  }
}

Child().value();
