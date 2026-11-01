function readX(value) {
  return value.x;
}

let propertyObject = {x: 7};
let propertyIndex = 0;
let propertyChecksum = 0;
while (propertyIndex < 50000) {
  propertyChecksum = propertyChecksum + readX(propertyObject);
  propertyIndex = propertyIndex + 1;
}
propertyChecksum;
