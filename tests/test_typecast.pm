# test_typecast.pm — typecasting functions

# int()
assert_eq(int(3.9),       3,    "int(float)")
assert_eq(int(true),      1,    "int(true)")
assert_eq(int(false),     0,    "int(false)")
assert_eq(int("42"),      42,   "int(string decimal)")
assert_eq(int("0xFF"),    255,  "int(string hex)")
assert_eq(int("0b1010"),  10,   "int(string binary)")
assert_eq(int("0o17"),    15,   "int(string octal)")
assert_eq(int(null),      0,    "int(null)")

# float()
assert_eq(float(7),       7.0,  "float(int)")
assert_eq(float(true),    1.0,  "float(true)")
assert_eq(float(false),   0.0,  "float(false)")
assert_eq(float("3.14"),  3.14, "float(string)")
assert_eq(float(null),    0.0,  "float(null)")

# bool()
assert_eq(bool(1),         true,    "bool(1)")
assert_eq(bool(0),         false,   "bool(0)")
assert_eq(bool("true"),    true,    "bool(string true)")
assert_eq(bool("false"),   false,   "bool(string false)")
assert_eq(bool("unknown"), unknown, "bool(string unknown)")

# str()
assert_eq(str(42),    "42",   "str(int)")
assert_eq(str(true),  "true", "str(true)")
assert_eq(str(false), "false","str(false)")
assert_eq(str(null),  "null", "str(null)")

# complex()
let c1 = complex(3)
assert_eq(type(c1), "complex", "complex(int) type")
let c2 = complex(1, 4)
assert_eq(type(c2), "complex", "complex(int,int) type")

# array()
assert_eq(array((1, 2, 3)), [1, 2, 3], "array(tuple)")
let a = array("hi")
assert_eq(len(a), 2, "array(string) length")

# tuple()
let t = tuple([10, 20, 30])
assert_eq(type(t), "tuple", "tuple(array) type")
assert_eq(len(t),  3,       "tuple(array) length")

# set()
let s = set([1, 2, 2, 3, 3])
assert_eq(type(s), "set",  "set(array) type")
assert_eq(len(s),  3,      "set(array) deduplication")

output("[PASS] test_typecast")
