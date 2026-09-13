class Alpha {
  static make(value) {
    return value + 1;
  }
}

class Beta {
  static make(value) {
    return value + 10;
  }
}

function read(type) {
  return type.make(1);
}

read(Alpha) + read(Beta) + read(Alpha) + read(Beta);
