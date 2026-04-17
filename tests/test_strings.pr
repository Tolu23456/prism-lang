# test_strings.pm — string operations and methods

let s = "Hello, Prism!"

assert_eq(len(s),         13,             "string len")
assert_eq(s[0],           "H",            "string index")
assert_eq(s[7:12],        "Prism",        "string slice")
assert_eq(s.upper(),      "HELLO, PRISM!","upper()")
assert_eq(s.lower(),      "hello, prism!","lower()")
assert_eq(s.startswith("Hello"), true,    "startswith true")
assert_eq(s.startswith("World"), false,   "startswith false")
assert_eq(s.endswith("!"),       true,    "endswith true")
assert_eq(s.endswith("?"),       false,   "endswith false")

let padded = "  hello  "
assert_eq(padded.strip(),       "hello",   "strip()")
assert_eq(padded.lstrip(),      "hello  ", "lstrip()")
assert_eq(padded.rstrip(),      "  hello", "rstrip()")

assert_eq("hello world".split(" "), ["hello", "world"], "split()")
assert_eq("-".join(["a", "b", "c"]), "a-b-c",           "join()")
assert_eq("hello".replace("l", "r"), "herro",            "replace()")
assert_eq("hello".find("ell"),       1,                  "find() found")
assert_eq("hello".find("xyz"),       -1,                 "find() not found")
assert_eq("hello".index("l"),        2,                  "index()")

assert_eq("abc" * 3, "abcabcabc", "string repeat")
assert_eq("ab" + "cd", "abcd",    "string concat")

let name = "Prism"
assert_eq(f"Hi {name}!", "Hi Prism!", "f-string")

assert_eq("3" in "hello 3 world", true,  "string membership")
assert_eq("z" in "hello",         false, "string non-membership")

output("[PASS] test_strings")
