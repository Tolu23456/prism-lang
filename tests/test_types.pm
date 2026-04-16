# test_types.pm — value types and type() function

assert_eq(type(42),           "int",      "type(int)")
assert_eq(type(3.14),         "float",    "type(float)")
assert_eq(type(true),         "bool",     "type(bool true)")
assert_eq(type(false),        "bool",     "type(bool false)")
assert_eq(type(unknown),      "bool",     "type(bool unknown)")
assert_eq(type("hello"),      "string",   "type(string)")
assert_eq(type(null),         "null",     "type(null)")
assert_eq(type([1, 2]),       "array",    "type(array)")
assert_eq(type({"k": "v"}),   "dict",     "type(dict)")
assert_eq(type({1, 2}),       "set",      "type(set)")
assert_eq(type((1, 2, 3)),    "tuple",    "type(tuple)")
assert_eq(type(1j),           "complex",  "type(complex)")

func dummy() { return 0 }
assert_eq(type(dummy),        "function", "type(function)")

output("[PASS] test_types")
