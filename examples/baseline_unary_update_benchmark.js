function unaryUpdateLoop(count) {
  let i = 0;
  let total = 0;
  let value = 7;
  let flag = false;
  while (i < count) {
    value = -value;
    flag = !flag;
    total = total + value;
    i = i + 1;
  }
  return total;
}

function repeatUnaryUpdateLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + unaryUpdateLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatUnaryUpdateLoop(20, 5000);
