class Box {
  init(value) {
    this.value = value;
  }

  get() {
    return this.value;
  }
}

let box = Box(7);
let total = 0;
let i = 0;

while (i < 3) {
  total = total + box.get();
  i = i + 1;
}

total;
