/* Prism Standard Library — csv module
   Pure Prism implementation — no Python imports.
   Full CSV parser (RFC 4180), serializer, and data utilities.
*/

/* ── Parser ──────────────────────────────────────────────── */

func parse(text, delimiter, has_header) {
    delimiter  = delimiter  ?? ","
    has_header = has_header ?? true

    let rows   = _parseRows(text, delimiter)
    if len(rows) == 0 { return [] }

    if not has_header {
        let result = []
        for row in rows { push(result, row) }
        return result
    }

    let headers = rows[0]
    let result  = []
    let i = 1
    while i < len(rows) {
        let row = rows[i]
        let obj = {}
        let j = 0
        while j < len(headers) {
            let key = trim(headers[j])
            let val = j < len(row) ? trim(row[j]) : ""
            obj[key] = val
            j += 1
        }
        push(result, obj)
        i += 1
    }
    return result
}

func parseRaw(text, delimiter) {
    return _parseRows(text, delimiter ?? ",")
}

func _parseRows(text, delim) {
    let rows    = []
    let current = []
    let field   = ""
    let in_quote = false
    let i = 0
    let chs = chars(text)
    let n   = len(chs)

    while i < n {
        let ch = chs[i]

        if in_quote {
            if ch == "\"" {
                if i + 1 < n and chs[i + 1] == "\"" {
                    field = field + "\""
                    i += 2
                    continue
                } else {
                    in_quote = false
                }
            } else {
                field = field + ch
            }
        } elif ch == "\"" {
            in_quote = true
        } elif ch == delim {
            push(current, field)
            field = ""
        } elif ch == "\n" or ch == "\r" {
            push(current, field)
            field = ""
            if len(current) > 0 {
                let has_content = false
                for f in current { if len(trim(f)) > 0 { has_content = true } }
                if has_content { push(rows, current) }
            }
            current = []
            if ch == "\r" and i + 1 < n and chs[i + 1] == "\n" { i += 1 }
        } else {
            field = field + ch
        }
        i += 1
    }

    push(current, field)
    if len(current) > 0 {
        let has_content = false
        for f in current { if len(trim(f)) > 0 { has_content = true } }
        if has_content { push(rows, current) }
    }

    return rows
}

/* ── Serializer ──────────────────────────────────────────── */

func stringify(rows, delimiter, headers) {
    delimiter = delimiter ?? ","

    if len(rows) == 0 { return "" }

    let lines = []

    if headers == void {
        if type(rows[0]) == "dict" {
            headers = keys(rows[0])
        }
    }

    if headers != void {
        push(lines, _rowToLine(headers, delimiter))
        for row in rows {
            let vals = []
            for h in headers {
                push(vals, has(row, h) ? str(row[h]) : "")
            }
            push(lines, _rowToLine(vals, delimiter))
        }
    } else {
        for row in rows {
            push(lines, _rowToLine(row, delimiter))
        }
    }

    return join("\n", lines)
}

func _rowToLine(fields, delim) {
    let parts = []
    for field in fields {
        let s = str(field)
        if contains(s, delim) or contains(s, "\"") or contains(s, "\n") {
            s = "\"" + _escapeField(s) + "\""
        }
        push(parts, s)
    }
    return join(delim, parts)
}

func _escapeField(s) {
    return join("\"\"", split(s, "\""))
}

/* ── Data Utilities ──────────────────────────────────────── */

func headers(rows) {
    if len(rows) == 0 { return [] }
    if type(rows[0]) == "dict" { return keys(rows[0]) }
    return rows[0]
}

func column(rows, name) {
    let result = []
    for row in rows {
        if type(row) == "dict" and has(row, name) {
            push(result, row[name])
        }
    }
    return result
}

func filterRows(rows, pred) {
    let result = []
    for row in rows {
        if pred(row) { push(result, row) }
    }
    return result
}

func mapRows(rows, fn) {
    let result = []
    for row in rows { push(result, fn(row)) }
    return result
}

func sortRows(rows, key) {
    let n = len(rows)
    let sorted = slice(rows, 0)
    let i = 1
    while i < n {
        let cur = sorted[i]
        let cv  = has(cur, key) ? cur[key] : ""
        let j   = i - 1
        while j >= 0 {
            let prev = has(sorted[j], key) ? sorted[j][key] : ""
            if str(prev) > str(cv) {
                sorted[j + 1] = sorted[j]
                j -= 1
            } else {
                break
            }
        }
        sorted[j + 1] = cur
        i += 1
    }
    return sorted
}

func addColumn(rows, name, fn) {
    let result = []
    for row in rows {
        let new_row = {}
        for k in keys(row) { new_row[k] = row[k] }
        new_row[name] = fn(row)
        push(result, new_row)
    }
    return result
}

func dropColumn(rows, name) {
    let result = []
    for row in rows {
        let new_row = {}
        for k in keys(row) {
            if k != name { new_row[k] = row[k] }
        }
        push(result, new_row)
    }
    return result
}

func renameColumn(rows, old_name, new_name) {
    let result = []
    for row in rows {
        let new_row = {}
        for k in keys(row) {
            if k == old_name { new_row[new_name] = row[k] }
            else              { new_row[k] = row[k] }
        }
        push(result, new_row)
    }
    return result
}

func groupByColumn(rows, key) {
    let groups = {}
    for row in rows {
        let k = has(row, key) ? str(row[key]) : "__null__"
        if not has(groups, k) { groups[k] = [] }
        push(groups[k], row)
    }
    return groups
}

func stats(rows, col) {
    let values = []
    for row in rows {
        if has(row, col) {
            let v = parseFloat(str(row[col]))
            if not isNaN(v) { push(values, v) }
        }
    }
    if len(values) == 0 { return void }
    let total = 0
    let mn = values[0]
    let mx = values[0]
    for v in values {
        total += v
        if v < mn { mn = v }
        if v > mx { mx = v }
    }
    let mean = total / len(values)
    let sq_diff = 0
    for v in values { sq_diff += (v - mean) * (v - mean) }
    let std = sqrt(sq_diff / len(values))
    return { "count": len(values), "sum": total, "mean": mean,
             "min": mn, "max": mx, "std": std }
}

func trim(s) { return s.trimStart().trimEnd() }
