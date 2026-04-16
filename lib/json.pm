/* Prism Standard Library — json module
   Pure Prism implementation — no Python imports.
   Full JSON parser (RFC 8259) and serializer from scratch.
*/

/* ── Serializer ──────────────────────────────────────────── */

func stringify(val, indent_size, current_depth) {
    current_depth = current_depth ?? 0
    let t = type(val)

    if val == void or val == null { return "null" }
    if t == "bool" {
        if val { return "true" }
        return "false"
    }
    if t == "int" or t == "float" {
        let s = str(val)
        return s
    }
    if t == "string" {
        return _escapeString(val)
    }
    if t == "array" {
        if len(val) == 0 { return "[]" }
        let items = []
        for item in val {
            push(items, stringify(item, indent_size, current_depth + 1))
        }
        if indent_size != void {
            let pad  = repeat("  ", current_depth + 1)
            let close = repeat("  ", current_depth)
            return "[\n" + pad + join(",\n" + pad, items) + "\n" + close + "]"
        }
        return "[" + join(",", items) + "]"
    }
    if t == "dict" {
        let ks = keys(val)
        if len(ks) == 0 { return "{}" }
        let pairs = []
        for k in ks {
            let kStr = _escapeString(str(k))
            let vStr = stringify(val[k], indent_size, current_depth + 1)
            if indent_size != void {
                push(pairs, kStr + ": " + vStr)
            } else {
                push(pairs, kStr + ":" + vStr)
            }
        }
        if indent_size != void {
            let pad  = repeat("  ", current_depth + 1)
            let close = repeat("  ", current_depth)
            return "{\n" + pad + join(",\n" + pad, pairs) + "\n" + close + "}"
        }
        return "{" + join(",", pairs) + "}"
    }
    return "null"
}

func pretty(val) {
    return stringify(val, 2, 0)
}

func _escapeString(s) {
    let out = "\""
    for ch in chars(s) {
        if      ch == "\""  { out = out + "\\\"" }
        elif ch == "\\"  { out = out + "\\\\" }
        elif ch == "\n"  { out = out + "\\n" }
        elif ch == "\r"  { out = out + "\\r" }
        elif ch == "\t"  { out = out + "\\t" }
        else              { out = out + ch }
    }
    return out + "\""
}

func repeat(s, n) {
    let r = ""
    let i = 0
    while i < n { r = r + s; i += 1 }
    return r
}

/* ── Parser ──────────────────────────────────────────────── */

let _src  = ""
let _pos  = 0
let _len  = 0

func parse(text) {
    _src = text
    _pos = 0
    _len = len(text)
    _skipWS()
    let result = _parseValue()
    _skipWS()
    if _pos < _len {
        error("JSON parse error: unexpected trailing characters at pos " + str(_pos))
    }
    return result
}

func _peek() {
    if _pos >= _len { return "" }
    return slice(_src, _pos, _pos + 1)
}

func _consume() {
    let ch = _peek()
    _pos += 1
    return ch
}

func _expect(ch) {
    let got = _consume()
    if got != ch { error("JSON parse error: expected '" + ch + "' got '" + got + "' at pos " + str(_pos)) }
}

func _skipWS() {
    while _pos < _len {
        let ch = _peek()
        if ch == " " or ch == "\n" or ch == "\r" or ch == "\t" {
            _pos += 1
        } else {
            return
        }
    }
}

func _parseValue() {
    _skipWS()
    let ch = _peek()
    if ch == "{" { return _parseObject() }
    if ch == "[" { return _parseArray() }
    if ch == "\"" { return _parseString() }
    if ch == "t" { return _parseLiteral("true", true) }
    if ch == "f" { return _parseLiteral("false", false) }
    if ch == "n" { return _parseLiteral("null", void) }
    if ch == "-" or (ch >= "0" and ch <= "9") { return _parseNumber() }
    error("JSON parse error: unexpected character '" + ch + "' at pos " + str(_pos))
}

func _parseLiteral(word, result) {
    let i = 0
    while i < len(word) {
        let got = _consume()
        if got != slice(word, i, i + 1) {
            error("JSON parse error: invalid literal at pos " + str(_pos))
        }
        i += 1
    }
    return result
}

func _parseNumber() {
    let start = _pos
    if _peek() == "-" { _pos += 1 }
    while _pos < _len and _peek() >= "0" and _peek() <= "9" { _pos += 1 }
    let is_float = false
    if _pos < _len and _peek() == "." {
        is_float = true
        _pos += 1
        while _pos < _len and _peek() >= "0" and _peek() <= "9" { _pos += 1 }
    }
    if _pos < _len and (_peek() == "e" or _peek() == "E") {
        is_float = true
        _pos += 1
        if _peek() == "+" or _peek() == "-" { _pos += 1 }
        while _pos < _len and _peek() >= "0" and _peek() <= "9" { _pos += 1 }
    }
    let numStr = slice(_src, start, _pos)
    if is_float { return parseFloat(numStr) }
    return parseInt(numStr)
}

func _parseString() {
    _expect("\"")
    let out = ""
    while _pos < _len {
        let ch = _consume()
        if ch == "\"" { return out }
        if ch == "\\" {
            let esc = _consume()
            if esc == "\"" { out = out + "\"" }
            elif esc == "\\" { out = out + "\\" }
            elif esc == "/"  { out = out + "/" }
            elif esc == "n"  { out = out + "\n" }
            elif esc == "r"  { out = out + "\r" }
            elif esc == "t"  { out = out + "\t" }
            elif esc == "b"  { out = out + "\b" }
            elif esc == "f"  { out = out + "\f" }
            elif esc == "u"  {
                let hex = slice(_src, _pos, _pos + 4)
                _pos += 4
                out = out + fromCharCode(parseInt(hex, 16))
            } else {
                out = out + "\\" + esc
            }
        } else {
            out = out + ch
        }
    }
    error("JSON parse error: unterminated string")
}

func _parseArray() {
    _expect("[")
    _skipWS()
    let result = []
    if _peek() == "]" { _consume(); return result }
    while true {
        _skipWS()
        push(result, _parseValue())
        _skipWS()
        let ch = _peek()
        if ch == "]" { _consume(); return result }
        if ch != "," { error("JSON parse error: expected ',' or ']' in array at pos " + str(_pos)) }
        _consume()
    }
    return result
}

func _parseObject() {
    _expect("{")
    _skipWS()
    let result = {}
    if _peek() == "}" { _consume(); return result }
    while true {
        _skipWS()
        if _peek() != "\"" { error("JSON parse error: expected string key at pos " + str(_pos)) }
        let k = _parseString()
        _skipWS()
        _expect(":")
        _skipWS()
        let v = _parseValue()
        result[k] = v
        _skipWS()
        let ch = _peek()
        if ch == "}" { _consume(); return result }
        if ch != "," { error("JSON parse error: expected ',' or '}' in object at pos " + str(_pos)) }
        _consume()
    }
    return result
}

func isValid(text) {
    try {
        parse(text)
        return true
    } catch (e) {
        return false
    }
}

func parseOr(text, default_val) {
    try {
        return parse(text)
    } catch (e) {
        return default_val
    }
}

func minify(text) {
    return stringify(parse(text), void, 0)
}

func merge(a_text, b_text) {
    let a = parse(a_text)
    let b = parse(b_text)
    if type(a) != "dict" or type(b) != "dict" {
        error("json.merge: both values must be objects")
    }
    let result = {}
    for k in keys(a) { result[k] = a[k] }
    for k in keys(b) { result[k] = b[k] }
    return stringify(result)
}
