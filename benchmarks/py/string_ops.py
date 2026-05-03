words = ["alpha", "bravo", "charlie", "delta", "echo"]
count = 0
for _ in range(50000):
    s = "-".join(words)
    parts = s.split("-")
    u = parts[0].upper()
    if u.startswith("A"):
        count += 1
print(count)
