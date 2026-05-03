items = []
for i in range(100000):
    items.append(i)

total = 0
for j in range(100000):
    total += items[j]

sl = items[0:1000]
print(total)
print(len(sl))
