/* Prism Standard Library — io module
   Extended IO utilities built on top of the io backed module.
*/

import io

func readJSON(path) {
    import json
    let content = io.readFile(path)
    return json.parse(content)
}

func writeJSON(path, data) {
    import json
    let content = json.pretty(data)
    return io.writeFile(path, content)
}

func readCSV(path) {
    let lines = io.readLines(path)
    if len(lines) == 0 { return [] }
    let headers = split(lines[0], ",")
    let rows = []
    let i = 1
    while i < len(lines) {
        if len(trim(lines[i])) > 0 {
            let values = split(lines[i], ",")
            let row = {}
            let j = 0
            while j < len(headers) {
                row[trim(headers[j])] = trim(values[j] ?? "")
                j += 1
            }
            push(rows, row)
        }
        i += 1
    }
    return rows
}

func writeCSV(path, rows) {
    if len(rows) == 0 { return io.writeFile(path, "") }
    let headers = keys(rows[0])
    let lines = [join(",", headers)]
    for row in rows {
        let vals = []
        for h in headers {
            push(vals, str(row[h] ?? ""))
        }
        push(lines, join(",", vals))
    }
    return io.writeLines(path, lines)
}

func copyFile(src, dst) {
    let data = io.readBytes(src)
    return io.writeBytes(dst, data)
}

func prompt(msg) {
    output(msg)
    return io.stdin()
}
