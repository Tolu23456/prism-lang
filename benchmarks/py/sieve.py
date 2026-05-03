limit = 500000
sieve = [True] * (limit + 1)
sieve[0] = sieve[1] = False

p = 2
while p * p <= limit:
    if sieve[p]:
        j = p * p
        while j <= limit:
            sieve[j] = False
            j += p
    p += 1

count = sum(1 for k in range(2, limit + 1) if sieve[k])
print(count)
