let point = {
  x: 1,
  y: 2
};

let i = 0;
let sum = 0;

while (i < 10000) {
  sum = sum + point.x;
  i = i + 1;
}

print(sum);
