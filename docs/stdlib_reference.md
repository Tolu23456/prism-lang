# Prism Standard Library Reference

This document covers all built-in functions and importable library modules.

---

## Built-in Functions (always available)

### I/O
| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(...values)` | Print values separated by spaces |
| `output` | `output(value)` | Print a single value |
| `input` | `input(prompt?)` | Read a line from stdin |

### Type Conversion
| Function | Example | Returns |
|----------|---------|---------|
| `int(x)` | `int("42")` | Integer |
| `float(x)` | `float("3.14")` | Float |
| `str(x)` | `str(42)` | String representation |
| `bool(x)` | `bool(0)` | Boolean (false for falsy values) |
| `type(x)` | `type(42)` | Type name string (`"int"`) |

### Math (built-in, no import needed)
| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `sqrt(x)` | Square root |
| `floor(x)` | Round down to integer |
| `ceil(x)` | Round up to integer |
| `round(x, n?)` | Round to n decimal places |
| `min(a, b, ...)` or `min([...])` | Minimum |
| `max(a, b, ...)` or `max([...])` | Maximum |
| `sum([...])` | Sum of array |
| `pow(x, y)` | x raised to y |
| `sin(x)`, `cos(x)`, `tan(x)` | Trig functions (radians) |
| `asin(x)`, `acos(x)`, `atan(x)` | Inverse trig |
| `atan2(y, x)` | 2-argument arctangent |
| `log(x)` | Natural log |
| `log2(x)` | Base-2 log |
| `log10(x)` | Base-10 log |
| `exp(x)` | e^x |
| `clamp(x, lo, hi)` | Clamp x between lo and hi |
| `hypot(x, y)` | sqrt(x²+y²) |
| `isnan(x)` | True if x is NaN |
| `isinf(x)` | True if x is ±Infinity |

### String (built-in)
| Function | Description |
|----------|-------------|
| `len(s)` | String length |
| `upper(s)` | Uppercase |
| `lower(s)` | Lowercase |
| `trim(s)` | Strip whitespace |
| `ltrim(s)` | Strip leading whitespace |
| `rtrim(s)` | Strip trailing whitespace |
| `split(s, delim)` | Split string into array |
| `join(delim, arr)` | Join array into string |
| `replace(s, from, to)` | Replace occurrences |
| `contains(s, sub)` | True if sub is found |
| `startsWith(s, prefix)` | True if s starts with prefix |
| `endsWith(s, suffix)` | True if s ends with suffix |
| `indexOf(s, sub)` | Index of first occurrence (-1 if missing) |
| `chars(s)` | Array of characters |
| `chr(n)` | Character from code point |
| `ord(c)` | Code point of character |
| `hex(n)` | Hex string e.g. `"ff"` |
| `bin(n)` | Binary string e.g. `"1010"` |
| `repeat(s, n)` | Repeat string n times |
| `format(fmt, ...)` | C-style printf format |

### Array (built-in)
| Function | Description |
|----------|-------------|
| `len(arr)` | Array length |
| `push(arr, val)` | Append value |
| `pop(arr)` | Remove and return last element |
| `insert(arr, i, val)` | Insert at index |
| `remove(arr, val)` | Remove first occurrence |
| `sort(arr)` | Sort in-place |
| `reverse(arr)` | Reverse in-place |
| `slice(arr, start, stop?)` | Return subarray |
| `flatten(arr)` | Recursively flatten nested arrays |
| `zip(a, b)` | Zip two arrays into pairs |
| `enumerate(arr)` | Array of `[index, value]` pairs |
| `filter(arr, fn)` | Return elements where fn(x) is truthy |
| `map(arr, fn)` | Apply fn to each element |
| `reduce(arr, fn, init?)` | Fold array with fn |
| `any(arr)` or `any(arr, fn)` | True if any element is truthy |
| `all(arr)` or `all(arr, fn)` | True if all elements are truthy |
| `sum([...])` | Sum numeric array |
| `range(n)` or `range(start, stop, step?)` | Generate integer range |

### Dict (built-in)
| Function | Description |
|----------|-------------|
| `len(d)` | Number of entries |
| `keys(d)` | Array of keys |
| `values(d)` | Array of values |
| `items(d)` | Array of `(key, value)` tuples |
| `has(d, key)` | True if key exists |
| `get(d, key, default?)` | Safe get with optional default |
| `delete(d, key)` | Remove key |
| `merge(d1, d2)` | Merge dicts (d2 wins on conflict) |

### Other
| Function | Description |
|----------|-------------|
| `clock()` | Milliseconds since program start |
| `sleep(ms)` | Pause for ms milliseconds |
| `assert(cond, msg?)` | Raise error if cond is falsy |
| `assert_eq(a, b)` | Raise error if a != b |
| `print_err(...)` | Print to stderr |
| `exit(code?)` | Exit with code (default 0) |

---

## lib/math — Extended Math

```prism
import "lib/math"
```

| Symbol / Function | Description |
|-------------------|-------------|
| `PI` | π ≈ 3.14159265358979 |
| `E` | e ≈ 2.71828182845905 |
| `TAU` | 2π |
| `gcd(a, b)` | Greatest common divisor |
| `lcm(a, b)` | Least common multiple |
| `isPrime(n)` | True if n is prime |
| `nextPrime(n)` | Smallest prime > n |
| `primes(n)` | All primes up to n (sieve) |
| `fib(n)` | nth Fibonacci number |
| `factorial(n)` | n! |
| `choose(n, k)` | Binomial coefficient C(n,k) |
| `mean(arr)` | Arithmetic mean |
| `median(arr)` | Median value |
| `variance(arr)` | Population variance |
| `stddev(arr)` | Standard deviation |

---

## lib/strings — String Utilities

```prism
import "lib/strings"
```

| Function | Description |
|----------|-------------|
| `capitalize(s)` | Capitalize first letter |
| `titleCase(s)` | Title Case each word |
| `camelCase(s)` | camelCase from words |
| `snakeCase(s)` | snake_case from camelCase |
| `kebabCase(s)` | kebab-case |
| `swapCase(s)` | Toggle case of each character |
| `padLeft(s, w, ch)` | Left-pad to width w with character ch |
| `padRight(s, w, ch)` | Right-pad |
| `padCenter(s, w, ch)` | Center |
| `repeat(s, n)` | Repeat s n times |
| `reverse(s)` | Reverse string |
| `isPalindrome(s)` | True if palindrome |
| `truncate(s, max, ellipsis)` | Truncate with ellipsis |
| `countOccurrences(s, sub)` | Count occurrences of sub |
| `isNumeric(s)` | True if all digits |
| `isAlpha(s)` | True if all alphabetic |
| `isAlphaNumeric(s)` | True if alphanumeric |
| `isEmpty(s)` | True if len == 0 |
| `isBlank(s)` | True if only whitespace |
| `startsWithAny(s, prefixes)` | True if starts with any prefix in array |
| `endsWithAny(s, suffixes)` | True if ends with any suffix |

---

## lib/collections — Data Structures

```prism
import "lib/collections"
```

| Function | Description |
|----------|-------------|
| `sorted(arr, key?, reverse?)` | Return sorted copy |
| `flatten(arr)` | Recursively flatten |
| `unique(arr)` | Remove duplicates (preserve order) |
| `zip2(a, b)` | Zip two arrays |
| `chunk(arr, size)` | Split into chunks of size |
| `first(arr)` | First element |
| `last(arr)` | Last element |
| `take(arr, n)` | First n elements |
| `drop(arr, n)` | All but first n elements |
| `sumArr(arr)` | Sum of numeric array |
| `product(arr)` | Product of array |
| `countWhere(arr, fn)` | Count elements matching predicate |
| `groupBy(arr, fn)` | Group into dict by key function |
| `frequency(arr)` | Dict of value → count |

---

## lib/random — Random Numbers

```prism
import "lib/random"
```

| Function | Description |
|----------|-------------|
| `randInt(lo, hi)` | Random int in [lo, hi] |
| `randFloat()` | Random float in [0, 1) |
| `randChoice(arr)` | Random element from array |
| `shuffle(arr)` | Shuffle array in-place |
| `sample(arr, n)` | Random sample of n elements |
| `randNormal(mean, std)` | Normal distribution sample |

---

## lib/json — JSON Parsing

```prism
import "lib/json"
```

| Function | Description |
|----------|-------------|
| `json_parse(str)` | Parse JSON string → Prism value |
| `json_stringify(val, indent?)` | Serialize Prism value → JSON string |
| `json_pretty(val)` | Pretty-print JSON with 2-space indent |

---

## lib/fs — File System

```prism
import "lib/fs"
```

Complements the built-in file functions:

| Function | Description |
|----------|-------------|
| `file_exists(path)` | True if file or dir exists |
| `is_file(path)` | True if path is a file |
| `is_dir(path)` | True if path is a directory |
| `read_file(path)` | Read file as string |
| `write_file(path, data)` | Write string to file |
| `append_file(path, data)` | Append to file |
| `delete_file(path)` | Delete file |
| `listdir(path)` | Array of entry names |
| `getcwd()` | Current working directory |
| `getenv(name, default?)` | Environment variable |
| `file_size(path)` | File size in bytes |
| `copy_file(src, dst)` | Copy file |
| `glob(pattern)` | Array of matching paths |

---

## lib/datetime — Date and Time

```prism
import "lib/datetime"
```

| Function | Description |
|----------|-------------|
| `now()` | Dict: `{year, month, day, hour, min, sec, ms}` |
| `timestamp()` | Unix timestamp in seconds |
| `formatDate(dt, fmt)` | Format date dict |
| `parseDate(str, fmt)` | Parse date string |
| `dayOfWeek(dt)` | 0=Mon, 6=Sun |
| `isLeapYear(year)` | True if leap year |
| `daysInMonth(year, month)` | Number of days |
| `addDays(dt, n)` | Add n days to date |
| `diffDays(dt1, dt2)` | Days between dates |

---

## lib/crypto — Hashing

```prism
import "lib/crypto"
```

| Function | Description |
|----------|-------------|
| `md5(s)` | MD5 hex digest string |
| `sha1(s)` | SHA-1 hex digest |
| `sha256(s)` | SHA-256 hex digest |
| `hash(s)` | Fast non-cryptographic hash |
| `randomBytes(n)` | n random bytes as hex string |
| `base64Encode(s)` | Base64 encode |
| `base64Decode(s)` | Base64 decode |
| `xorEncrypt(s, key)` | Simple XOR cipher |
| `hmac(key, msg)` | HMAC (using SHA-256) |

---

*See also: [language_guide.md](language_guide.md), [vm_internals.md](vm_internals.md)*
