function one() {
  return 1;
}

function numberReturnLoop(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    total = total + one();
    i = i + 1;
  }
  return total;
}

function repeatNumberReturnLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + numberReturnLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatNumberReturnLoop(20, 5000);
