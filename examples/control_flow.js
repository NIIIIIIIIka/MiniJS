let sum = 0;

for (let i = 1; i <= 5; i = i + 1) {
  if (i == 3) {
    continue;
  }

  sum = sum + i;
}

print(sum);
