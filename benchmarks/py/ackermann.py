import sys
sys.setrecursionlimit(100000)

def ack(m, n):
    if m == 0:
        return n + 1
    if n == 0:
        return ack(m - 1, 1)
    return ack(m - 1, ack(m, n - 1))

total = 0
for _ in range(100):
    total += ack(3, 5)
print(total)
