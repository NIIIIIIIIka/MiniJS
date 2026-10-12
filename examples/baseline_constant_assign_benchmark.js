function constantAssignLoop(count) {
  let i = 0;
  let total = 0;
  let value = 0;
  while (i < count) {
    value = 7;
    total = total + value;
    i = i + 1;
  }
  return total;
}

function repeatConstantAssignLoop(rounds, count) {
  let round = 0;
  let checksum = 0;
  while (round < rounds) {
    checksum = checksum + constantAssignLoop(count);
    round = round + 1;
  }
  return checksum;
}

repeatConstantAssignLoop(20, 5000);
