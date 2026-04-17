# Prism Built-in Functions

All built-ins are available in any Prism program without imports.

## I/O

| Function | Description |
|----------|-------------|
| `output(value)` | Print value with newline. Accepts any number of args. Can be called without parens: `output "msg"` |
| `print(value)` | Print without trailing newline |
| `input(prompt?)` | Read a line from stdin |

## Type Conversion

| Function | Description |
|----------|-------------|
| `int(x)` | Convert to integer |
| `float(x)` | Convert to float |
| `str(x)` | Convert to string |
| `bool(x)` | Convert to boolean |
| `complex(re, im)` | Create complex number |

## Type Inspection

| Function | Description |
|----------|-------------|
| `type(x)` | Returns type name: `"int"`, `"float"`, `"string"`, `"bool"`, `"array"`, `"dict"`, `"set"`, `"tuple"`, `"function"`, `"null"` |
| `repr(x)` | Detailed string representation |
| `id(x)` | Unique object identity integer |

## Collections — Arrays

| Function | Description |
|----------|-------------|
| `push(arr, item)` | Append item to array (returns null) |
| `pop(arr)` | Remove and return last item |
| `len(arr)` | Number of elements |
| `sort(arr)` | Sort in place |
| `reverse(arr)` | Reverse in place (also works on strings) |
| `slice(arr, start, end?)` | Sub-array or substring |
| `flatten(arr)` | Flatten one level |
| `range(start, end, step?)` | Create integer range array |
| `zip(a, b)` | Zip two arrays into array of pairs |
| `enumerate(arr)` | Array of `[index, value]` pairs |
| `sum(arr)` | Sum all numeric elements |

## Collections — Dicts & Sets

| Function | Description |
|----------|-------------|
| `dict(...)` | Create empty dict or copy from another |
| `set(...)` | Create empty set or copy from another |
| `keys(d)` | Array of dict keys |
| `values(d)` | Array of dict values |
| `items(d)` | Array of `[key, value]` pairs |
| `has(col, key)` | Check membership (array, dict, set) |
| `copy(x)` | Shallow copy |

## Strings

| Function | Description |
|----------|-------------|
| `upper(s)` | Uppercase |
| `lower(s)` | Lowercase |
| `trim(s)` | Strip whitespace |
| `split(s, delim)` | Split into array |
| `join(sep, arr)` | Join array into string |
| `chars(s)` | Split into character array |
| `replace(s, from, to)` | Replace first occurrence |
| `starts(s, prefix)` | Boolean prefix check |
| `ends(s, suffix)` | Boolean suffix check |
| `contains(s, needle)` | Boolean contains check |
| `slice(s, start, end?)` | Substring |
| `ord(ch)` | Character code point |
| `chr(n)` | Code point to character |
| `hex(n)` | Integer to hex string |
| `bin(n)` | Integer to binary string |
| `oct(n)` | Integer to octal string |
| `parseInt(s, base?)` | Parse integer string |
| `parseFloat(s)` | Parse float string |

## Mathematics

| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `sqrt(x)` | Square root |
| `pow(x, y)` | x to the power y |
| `floor(x)` | Round down |
| `ceil(x)` | Round up |
| `round(x)` | Round to nearest integer |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `clamp(x, lo, hi)` | Clamp value to range |
| `sin(x)` | Sine (radians) |
| `cos(x)` | Cosine |
| `tan(x)` | Tangent |
| `asin(x)` | Arcsine |
| `acos(x)` | Arccosine |
| `atan(x)` | Arctangent |
| `atan2(y, x)` | 2-argument arctangent |
| `log(x)` | Natural logarithm |
| `log2(x)` | Base-2 logarithm |
| `log10(x)` | Base-10 logarithm |
| `exp(x)` | e raised to x |
| `hypot(x, y)` | Euclidean distance |
| `isnan(x)` | Check for NaN |
| `isinf(x)` | Check for Infinity |
| `sum(arr)` | Sum array elements |

## Assertions & Errors

| Function | Description |
|----------|-------------|
| `assert(cond, msg?)` | Raise error if condition is false |
| `assert_eq(a, b, msg?)` | Raise error if a != b |
| `error(msg)` | Raise a runtime error |
| `exit(code?)` | Exit the program |

## Timing

| Function | Description |
|----------|-------------|
| `clock()` | CPU time in seconds (float) |
| `time()` | Wall clock time (float seconds since epoch) |

## Constructors

| Function | Description |
|----------|-------------|
| `array(...)` | Create array |
| `dict(...)` | Create dict |
| `set(...)` | Create set |
| `tuple(...)` | Create tuple |

## GUI (built-in window toolkit)

| Function | Description |
|----------|-------------|
| `gui_window(title, w, h)` | Create a native GUI window |
| `gui_label(text)` | Add a text label |
| `gui_button(label, callback)` | Add a clickable button |
| `gui_input(placeholder)` | Add a text input field |
| `gui_run()` | Start the event loop |

## X11 GUI (`xgui_*`)

Low-level X11 drawing primitives — available when built with `HAVE_X11`:

`xgui_init`, `xgui_style`, `xgui_running`, `xgui_begin`, `xgui_end`,
`xgui_label`, `xgui_button`, `xgui_input`, `xgui_spacer`,
`xgui_row_begin`, `xgui_row_end`, `xgui_close`.
