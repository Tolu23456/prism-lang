# Typecasting test for all datatypes

output("=== int() ===")
output(int(3.9))          # 3
output(int(true))         # 1
output(int(false))        # 0
output(int("42"))         # 42
output(int("0xFF"))       # 255
output(int("0b1010"))     # 10
output(int(null))         # 0
output(int(2.5j))         # 2  (real part)

output("=== float() ===")
output(float(7))          # 7.0
output(float(true))       # 1.0
output(float(false))      # 0.0
output(float("3.14"))     # 3.14
output(float(null))       # 0.0
output(float(1.5j))       # 1.5 (real part)

output("=== bool() ===")
output(bool(1))           # true
output(bool(0))           # false
output(bool("true"))      # true
output(bool("false"))     # false
output(bool("unknown"))   # unknown
output(bool(null))        # false

output("=== str() ===")
output(str(42))           # 42
output(str(3.14))         # 3.14
output(str(true))         # true
output(str(null))         # null
output(str([1, 2, 3]))    # [1, 2, 3]

output("=== complex() ===")
output(complex(3))        # 3+0j
output(complex(2.5))      # 2.5+0j
output(complex(1, 4))     # 1+4j
output(complex(2.0, 3.0)) # 2+3j

output("=== array() ===")
output(array((1, 2, 3)))      # [1, 2, 3] from tuple
let s = {4, 5, 6}
output(array(s))              # array from set
output(array("hi"))           # ["h", "i"]
let d = {"x": 10, "y": 20}
output(array(d))              # ["x", "y"] keys from dict

output("=== tuple() ===")
output(tuple([10, 20, 30]))   # (10, 20, 30) from array
let s2 = {7, 8, 9}
output(tuple(s2))             # tuple from set

output("=== set() ===")
output(set([1, 2, 2, 3]))     # {1, 2, 3} from array
output(set((4, 5, 5, 6)))     # {4, 5, 6} from tuple
let d2 = {"a": 1, "b": 2}
output(set(d2))               # {"a", "b"} keys from dict
output(set("hello"))          # set of chars
