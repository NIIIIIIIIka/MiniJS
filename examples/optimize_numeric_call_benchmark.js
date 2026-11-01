function numericKernel(a, b) {
  return (a + b) * 2 - a / 2;
}

let numericIndex = 0;
let numericChecksum = 0;
while (numericIndex < 50000) {
  numericChecksum = numericChecksum + numericKernel(numericIndex, 3);
  numericIndex = numericIndex + 1;
}
numericChecksum;
