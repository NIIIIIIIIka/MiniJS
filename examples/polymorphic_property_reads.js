function read(object) {
  return object.value;
}

let first = { value: 1 };
let second = { tag: 0, value: 2 };

read(first);
read(second);
read(first);
read(second);
