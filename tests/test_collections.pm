# test_collections.pm — arrays, dicts, sets, tuples

# --- Array ---
let a = [10, 20, 30]
assert_eq(len(a),    3,  "array len")
assert_eq(a[0],      10, "array index 0")
assert_eq(a[-1],     30, "array negative index")
assert_eq(a[1:3],    [20, 30], "array slice")
a.add(40)
assert_eq(len(a),    4,  "array add")
assert_eq(a[-1],     40, "array add value")
a.insert(0, 5)
assert_eq(a[0],      5,  "array insert at 0")
a.remove(5)
assert_eq(a[0],      10, "array remove")
let popped = a.pop()
assert_eq(popped,    40, "array pop()")
assert_eq(len(a),    3,  "array len after pop")
let b = [3, 1, 2]
b.sort()
assert_eq(b, [1, 2, 3], "array sort")
let c = [1, 2]
c.extend([3, 4])
assert_eq(c, [1, 2, 3, 4], "array extend")
assert_eq(3 in c, true,  "array membership")
assert_eq(9 in c, false, "array non-membership")

# --- Dict ---
let d = {"name": "Alice", "age": 30}
assert_eq(d["name"],    "Alice", "dict get")
assert_eq(len(d),       2,       "dict len")
d["city"] = "NY"
assert_eq(d["city"],    "NY",    "dict set")
assert_eq(len(d),       3,       "dict len after set")
assert_eq("name" in d,  true,  "dict has true")
assert_eq("x" in d,     false, "dict has false")
let keys = d.keys()
assert_eq(type(keys),   "array", "dict keys type")
assert_eq(len(keys),    3,       "dict keys len")
d.erase()
assert_eq(len(d),       0,       "dict erase all")

# --- Set ---
let s1 = {1, 2, 3, 4}
let s2 = {3, 4, 5, 6}
assert_eq(len(s1), 4,  "set len")
assert_eq(3 in s1, true,  "set membership")
assert_eq(9 in s1, false, "set non-membership")
let u = s1 | s2
assert_eq(len(u), 6, "set union len")
let i = s1 & s2
assert_eq(len(i), 2, "set intersection len")
let diff = s1 - s2
assert_eq(len(diff), 2, "set difference len")

# --- Tuple ---
let t = (10, 20, 30)
assert_eq(len(t),   3,  "tuple len")
assert_eq(t[0],     10, "tuple index")
assert_eq(t[-1],    30, "tuple negative index")
assert_eq(t[1:3],   (20, 30), "tuple slice")
assert_eq(20 in t,  true,  "tuple membership")
assert_eq(99 in t,  false, "tuple non-membership")

# --- Memory module ---
let mem_stats = memory.stats()
assert_eq(type(mem_stats), "dict", "memory.stats returns dict")
assert_eq(type(mem_stats["policy"]), "string", "memory.stats includes policy")
assert(memory.limit("2mb") >= 2097152, "memory.limit parses mb")
assert(memory.collect() >= 0, "memory.collect returns freed count")
assert_eq(type(memory.profile()), "dict", "memory.profile returns stats")

output("[PASS] test_collections")
