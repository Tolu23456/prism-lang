d = {}
for i in range(10000):
    d[str(i)] = i * 2

total = 0
for j in range(10000):
    total += d[str(j)]
print(total)
