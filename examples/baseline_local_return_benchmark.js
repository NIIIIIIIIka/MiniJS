function identity(value) {
  return value;
}

function localReturnLoop(count) {
  let i = 0;
  let total = 0;
  while (i < count) {
    total = total + identity(i);
    i = i + 1;
  }
  return total;
}

function repeatLocalReturnLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + localReturnLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatLocalReturnLoop(20, 5000);
