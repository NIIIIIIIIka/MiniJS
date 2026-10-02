class Counter {
  init(value) {
    this.value = value;
  }

  get() {
    return this.value;
  }
}

function read(counter) {
  return counter.get();
}

let counter = Counter(10);

read(counter);
read(counter);
read(counter);
