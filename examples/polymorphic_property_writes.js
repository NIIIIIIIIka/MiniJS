function write(object, value) {
  object.value = value;
  return value;
}

let first = { value: 1 };
let second = { tag: 0, value: 2 };

write(first, 10);
write(second, 20);
write(first, 30);
write(second, 40);
