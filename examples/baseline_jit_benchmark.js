function add(a, b) {
  return a + b;
}

let i = 0;
let total = 0;

while (i < 10000) {
  total = add(total, 1);
  i = i + 1;
}

total;
