function write(object, value) {
  object.name = value;
}

let first = { name: 0, extra: 1 };
let second = { extra: 0, name: 0 };

write(first, 1);
write(second, 2);
write(first, 3);
write(second, 4);

first.name + second.name;
