function singleHotLoop(count) {
  let index = 0;
  let checksum = 0;
  while (index < count) {
    checksum = checksum + index;
    index = index + 1;
  }
  return checksum;
}

singleHotLoop(500000);
