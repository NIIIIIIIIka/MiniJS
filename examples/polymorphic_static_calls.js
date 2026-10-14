class First {
  static get() {
    return 1;
  }
}

class Second {
  static get() {
    return 2;
  }
}

function read(Box) {
  return Box.get();
}

read(First);
read(Second);
read(First);
read(Second);
