import sys
sys.setrecursionlimit(10000)

def rsum(n):
    if n <= 0:
        return 0
    return n + rsum(n - 1)

total = 0
for _ in range(5000):
    total += rsum(200)
print(total)
