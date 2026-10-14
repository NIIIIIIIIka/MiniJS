function arithmeticAssignLoop(count) {
  let i = 0;
  let total = 0;
  let result = 0;
  let offset = 2;
  while (i < count) {
    result = i + offset;
    total = total + result;
    result = i * 2;
    total = total + result;
    i = i + 1;
  }
  return total;
}

function repeatArithmeticAssignLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + arithmeticAssignLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatArithmeticAssignLoop(20, 5000);
