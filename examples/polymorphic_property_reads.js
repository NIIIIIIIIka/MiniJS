function read(object) {
  return object.name;
}

let first = { name: "A", extra: 1 };
let second = { extra: 2, name: "B" };

read(first) + read(second) + read(first) + read(second);
