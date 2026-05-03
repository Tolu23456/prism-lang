from functools import reduce

print("=== Python Flexibility Benchmark ===")
print()

# ── 1. Closures & captured state ─────────────────────────────
def make_counter(start):
    n = [start]
    def inc(step):
        n[0] += step
        return n[0]
    return inc

ctr = make_counter(0)
ctr(1); ctr(2); ctr(3)
print("1. Closures:            ctr after +1+2+3 =", ctr(0))

# ── 2. First-class functions / higher-order ───────────────────
def apply(f, x): return f(x)
def compose(f, g):
    def h(x): return f(g(x))
    return h

double = lambda x: x * 2
inc    = lambda x: x + 1
dbl_then_inc = compose(inc, double)
print("2. compose(inc,double)(5) =", dbl_then_inc(5))

# ── 3. Recursion + accumulator ────────────────────────────────
def flatten(arr, acc):
    for item in arr:
        if isinstance(item, list):
            flatten(item, acc)
        else:
            acc.append(item)
    return acc

nested = [1, [2, [3, 4]], [5, 6]]
flat = flatten(nested, [])
print("3. Flatten nested array:", flat)

# ── 4. Classes & inheritance ──────────────────────────────────
class Animal:
    def __init__(self, name, sound):
        self.name = name
        self.sound = sound
    def speak(self):
        return f"{self.name} says {self.sound}"

class Dog(Animal):
    def __init__(self, name):
        super().__init__(name, "woof")
    def fetch(self, item):
        return f"{self.name} fetches the {item}!"

d = Dog("Rex")
print("4a. Inheritance speak():", d.speak())
print("4b. Method:             ", d.fetch("ball"))

# ── 5. Pattern matching (match/case, Python 3.10+) ─────────────
def classify(n):
    match n:
        case 0:
            return "zero"
        case 1 | 2:
            return "one or two"
        case n if 3 <= n <= 10:
            return "three to ten"
        case _:
            return "large"

print("5. Pattern match 0:", classify(0), "| 7:", classify(7), "| 99:", classify(99))

# ── 6. Dict + dynamic dispatch ───────────────────────────────
ops = {
    "add": lambda a, b: a + b,
    "mul": lambda a, b: a * b,
    "pow": lambda a, b: a ** b,
}
print("6. Dict dispatch add(3,4)=", ops["add"](3, 4), " pow(2,8)=", ops["pow"](2, 8))

# ── 7. F-strings & string interpolation ──────────────────────
items = ["apple", "banana", "cherry"]
report = ""
for idx, item in enumerate(items):
    report += f"[{idx}]={item} "
print("7. F-string loop:", report.strip())

# ── 8. Map / filter / reduce ──────────────────────────────────
nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
evens   = list(filter(lambda x: x % 2 == 0, nums))
doubled = list(map(lambda x: x * 2, evens))
total   = reduce(lambda a, b: a + b, doubled, 0)
print("8. map/filter/reduce sum of doubled evens:", total)

# ── 9. Multiple return values (tuple unpacking) ────────────────
def stats(arr):
    mn = min(arr); mx = max(arr); avg = sum(arr) / len(arr)
    return mn, mx, avg

lo, hi, avg = stats([3, 1, 4, 1, 5, 9, 2, 6])
print(f"9. Tuple unpack stats: min={lo} max={hi} avg={avg}")

# ── 10. Sets & membership ─────────────────────────────────────
a = {1, 2, 3, 4, 5}
b = {4, 5, 6, 7, 8}
print("10. Set ops: union len=", len(a | b), " intersect=", a & b, " diff=", a - b)

print()
print("=== Flexibility benchmark complete ===")
