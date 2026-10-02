class First {
  get() {
    return 1;
  }
}

class Second {
  get() {
    return 2;
  }
}

function read(box) {
  return box.get();
}

let first = First();
let second = Second();

read(first);
read(second);
read(first);
read(second);
