/* Prism Standard Library — datetime module
   Pure Prism implementation — no Python imports.
   Gregorian calendar arithmetic, formatting, parsing,
   duration calculation, and timezone-offset support.
*/

/* ── Constants ───────────────────────────────────────────── */

const MONTHS_SHORT = ["Jan","Feb","Mar","Apr","May","Jun",
                      "Jul","Aug","Sep","Oct","Nov","Dec"]
const MONTHS_LONG  = ["January","February","March","April","May","June",
                      "July","August","September","October","November","December"]
const DAYS_SHORT   = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"]
const DAYS_LONG    = ["Sunday","Monday","Tuesday","Wednesday",
                      "Thursday","Friday","Saturday"]
const SECONDS_PER_MINUTE = 60
const SECONDS_PER_HOUR   = 3600
const SECONDS_PER_DAY    = 86400
const DAYS_PER_400Y      = 146097

/* ── Struct ──────────────────────────────────────────────── */

struct DateTime { year, month, day, hour, minute, second, ms, tz_offset }

func dt(year, month, day, hour, minute, second, ms, tz_offset) {
    hour      = hour      ?? 0
    minute    = minute    ?? 0
    second    = second    ?? 0
    ms        = ms        ?? 0
    tz_offset = tz_offset ?? 0
    return new DateTime(year, month, day, hour, minute, second, ms, tz_offset)
}

/* ── Leap year ───────────────────────────────────────────── */

func isLeapYear(y) {
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)
}

func daysInMonth(y, m) {
    let d = [31,28,31,30,31,30,31,31,30,31,30,31]
    if m == 2 and isLeapYear(y) { return 29 }
    return d[m - 1]
}

func daysInYear(y) {
    if isLeapYear(y) { return 366 }
    return 365
}

/* ── Epoch conversion (days since 1970-01-01) ───────────── */

func _ymdToEpochDay(y, m, d) {
    let era = (y - (m <= 2 ? 1 : 0)) // 400
    let yoe = y - era * 400 - (m <= 2 ? 1 : 0)
    let doy = int((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1
    let doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * DAYS_PER_400Y + doe - 719468
}

func _epochDayToYmd(z) {
    z = z + 719468
    let era = z // DAYS_PER_400Y
    if z < 0 and z % DAYS_PER_400Y != 0 { era = era - 1 }
    let doe = z - era * DAYS_PER_400Y
    let yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    let y   = yoe + era * 400
    let doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    let mp  = int((5 * doy + 2) / 153)
    let d   = doy - int((153 * mp + 2) / 5) + 1
    let m   = mp + (mp < 10 ? 3 : -9)
    if m <= 2 { y = y + 1 }
    return [y, m, d]
}

func toTimestamp(d) {
    let epoch_day = _ymdToEpochDay(d.year, d.month, d.day)
    let seconds   = epoch_day * SECONDS_PER_DAY +
                    d.hour * SECONDS_PER_HOUR +
                    d.minute * SECONDS_PER_MINUTE +
                    d.second
    return seconds * 1000 + d.ms - d.tz_offset * SECONDS_PER_MINUTE * 1000
}

func fromTimestamp(ms_epoch, tz_offset) {
    tz_offset = tz_offset ?? 0
    let adj_ms = ms_epoch + tz_offset * SECONDS_PER_MINUTE * 1000
    let total_seconds = adj_ms // 1000
    let ms = int(adj_ms % 1000)
    let epoch_day = total_seconds // SECONDS_PER_DAY
    if total_seconds < 0 and total_seconds % SECONDS_PER_DAY != 0 {
        epoch_day = epoch_day - 1
    }
    let day_seconds = total_seconds - epoch_day * SECONDS_PER_DAY
    let ymd = _epochDayToYmd(epoch_day)
    let h   = day_seconds // SECONDS_PER_HOUR
    let rem = day_seconds % SECONDS_PER_HOUR
    let min = rem // SECONDS_PER_MINUTE
    let sec = rem % SECONDS_PER_MINUTE
    return new DateTime(ymd[0], ymd[1], ymd[2], h, min, sec, ms, tz_offset)
}

/* ── Arithmetic ──────────────────────────────────────────── */

func addDays(d, n) {
    let ts = toTimestamp(d) + n * SECONDS_PER_DAY * 1000
    return fromTimestamp(ts, d.tz_offset)
}

func addHours(d, n) {
    let ts = toTimestamp(d) + n * SECONDS_PER_HOUR * 1000
    return fromTimestamp(ts, d.tz_offset)
}

func addMinutes(d, n) {
    let ts = toTimestamp(d) + n * SECONDS_PER_MINUTE * 1000
    return fromTimestamp(ts, d.tz_offset)
}

func addSeconds(d, n) {
    let ts = toTimestamp(d) + n * 1000
    return fromTimestamp(ts, d.tz_offset)
}

func addMonths(d, n) {
    let total_months = d.month - 1 + n
    let new_year  = d.year + total_months // 12
    let new_month = total_months % 12 + 1
    if new_month <= 0 { new_month = new_month + 12; new_year = new_year - 1 }
    let max_day = daysInMonth(new_year, new_month)
    let new_day = min(d.day, max_day)
    return dt(new_year, new_month, new_day, d.hour, d.minute, d.second, d.ms, d.tz_offset)
}

func addYears(d, n) {
    return addMonths(d, n * 12)
}

func diffMs(a, b)      { return toTimestamp(b) - toTimestamp(a) }
func diffSeconds(a, b) { return diffMs(a, b) // 1000 }
func diffMinutes(a, b) { return diffSeconds(a, b) // SECONDS_PER_MINUTE }
func diffHours(a, b)   { return diffSeconds(a, b) // SECONDS_PER_HOUR }
func diffDays(a, b)    { return diffSeconds(a, b) // SECONDS_PER_DAY }

func isBefore(a, b)    { return toTimestamp(a) < toTimestamp(b) }
func isAfter(a, b)     { return toTimestamp(a) > toTimestamp(b) }
func isSame(a, b)      { return toTimestamp(a) == toTimestamp(b) }

func isBetween(d, start, end) {
    return not isBefore(d, start) and not isAfter(d, end)
}

/* ── Day of week ─────────────────────────────────────────── */

func dayOfWeek(d) {
    let epoch = _ymdToEpochDay(d.year, d.month, d.day)
    let dow = (epoch + 4) % 7
    if dow < 0 { dow = dow + 7 }
    return dow
}

func dayName(d, short)   {
    let idx = dayOfWeek(d)
    if short { return DAYS_SHORT[idx] }
    return DAYS_LONG[idx]
}

func monthName(d, short) {
    if short { return MONTHS_SHORT[d.month - 1] }
    return MONTHS_LONG[d.month - 1]
}

func isWeekend(d) {
    let dow = dayOfWeek(d)
    return dow == 0 or dow == 6
}

func isWeekday(d) { return not isWeekend(d) }

func weekOfYear(d) {
    let jan1 = dt(d.year, 1, 1)
    let start_dow = dayOfWeek(jan1)
    let day_of_year = diffDays(jan1, d) + 1
    return (day_of_year + start_dow - 1) // 7 + 1
}

func dayOfYear(d) {
    let jan1 = dt(d.year, 1, 1)
    return diffDays(jan1, d) + 1
}

/* ── Formatting ──────────────────────────────────────────── */

func _p2(n) {
    let s = str(n)
    if len(s) < 2 { return "0" + s }
    return s
}

func _p3(n) {
    let s = str(n)
    while len(s) < 3 { s = "0" + s }
    return s
}

func _p4(n) {
    let s = str(n)
    while len(s) < 4 { s = "0" + s }
    return s
}

func format(d, fmt) {
    fmt = fmt ?? "YYYY-MM-DD"
    let out = fmt
    out = _replaceAll(out, "YYYY", _p4(d.year))
    out = _replaceAll(out, "YY",   slice(str(d.year), len(str(d.year)) - 2))
    out = _replaceAll(out, "MMMM", monthName(d, false))
    out = _replaceAll(out, "MMM",  monthName(d, true))
    out = _replaceAll(out, "MM",   _p2(d.month))
    out = _replaceAll(out, "M",    str(d.month))
    out = _replaceAll(out, "DD",   _p2(d.day))
    out = _replaceAll(out, "D",    str(d.day))
    out = _replaceAll(out, "dddd", dayName(d, false))
    out = _replaceAll(out, "ddd",  dayName(d, true))
    out = _replaceAll(out, "HH",   _p2(d.hour))
    out = _replaceAll(out, "hh",   _p2((d.hour % 12 == 0 ? 12 : d.hour % 12)))
    out = _replaceAll(out, "mm",   _p2(d.minute))
    out = _replaceAll(out, "ss",   _p2(d.second))
    out = _replaceAll(out, "SSS",  _p3(d.ms))
    out = _replaceAll(out, "A",    (d.hour < 12 ? "AM" : "PM"))
    out = _replaceAll(out, "a",    (d.hour < 12 ? "am" : "pm"))
    return out
}

func toISO(d) {
    let base = _p4(d.year) + "-" + _p2(d.month) + "-" + _p2(d.day) + "T" +
               _p2(d.hour) + ":" + _p2(d.minute) + ":" + _p2(d.second) + "." + _p3(d.ms)
    if d.tz_offset == 0 { return base + "Z" }
    let sign = "+"
    let off  = d.tz_offset
    if off < 0 { sign = "-"; off = -off }
    let oh = off // 60
    let om = off % 60
    return base + sign + _p2(oh) + ":" + _p2(om)
}

func toDateString(d) { return format(d, "YYYY-MM-DD") }
func toTimeString(d) { return format(d, "HH:mm:ss") }

func _replaceAll(s, from, to) {
    return join(to, split(s, from))
}

/* ── Parsing (ISO 8601 subset) ───────────────────────────── */

func parseISO(s) {
    let parts = split(s, "T")
    let date_part = parts[0]
    let time_part = len(parts) > 1 ? parts[1] : "00:00:00"

    let d = split(date_part, "-")
    let y = parseInt(d[0])
    let mo = parseInt(d[1])
    let day = parseInt(d[2])

    let tz_off = 0
    let t = time_part
    if ends(t, "Z") { t = slice(t, 0, len(t) - 1) }
    elif contains(t, "+") {
        let tp = split(t, "+")
        t = tp[0]
        let tz = split(tp[1], ":")
        tz_off = parseInt(tz[0]) * 60 + parseInt(tz[1])
    } elif regexMatch(t, ".*-\\d{2}:\\d{2}$") {
        let idx = lastIndex(t)
        let tz_str = slice(t, idx)
        t = slice(t, 0, idx)
        let tz = split(slice(tz_str, 1), ":")
        tz_off = -(parseInt(tz[0]) * 60 + parseInt(tz[1]))
    }

    let time_parts = split(t, ":")
    let h   = parseInt(time_parts[0] ?? "0")
    let min = parseInt(time_parts[1] ?? "0")
    let sec_parts = split(time_parts[2] ?? "0", ".")
    let sec = parseInt(sec_parts[0])
    let ms  = len(sec_parts) > 1 ? parseInt(slice(sec_parts[1] + "000", 0, 3)) : 0

    return dt(y, mo, day, h, min, sec, ms, tz_off)
}

func lastIndex(t) {
    let i = len(t) - 1
    while i >= 0 {
        let ch = slice(t, i, i + 1)
        if ch == "-" or ch == "+" { return i }
        i -= 1
    }
    return -1
}

/* ── Relative time ───────────────────────────────────────── */

func timeAgo(d, now_ts) {
    let now = fromTimestamp(now_ts ?? 0)
    let diff_sec = abs(diffSeconds(d, now))

    if diff_sec < 60            { return "just now" }
    if diff_sec < 3600          { return str(diff_sec // 60) + " minutes ago" }
    if diff_sec < 86400         { return str(diff_sec // 3600) + " hours ago" }
    if diff_sec < 86400 * 7     { return str(diff_sec // 86400) + " days ago" }
    if diff_sec < 86400 * 30    { return str(diff_sec // (86400 * 7)) + " weeks ago" }
    if diff_sec < 86400 * 365   { return str(diff_sec // (86400 * 30)) + " months ago" }
    return str(diff_sec // (86400 * 365)) + " years ago"
}

/* ── Utility ─────────────────────────────────────────────── */

func startOfDay(d) { return dt(d.year, d.month, d.day, 0, 0, 0, 0, d.tz_offset) }
func endOfDay(d)   { return dt(d.year, d.month, d.day, 23, 59, 59, 999, d.tz_offset) }
func startOfMonth(d) { return dt(d.year, d.month, 1, 0, 0, 0, 0, d.tz_offset) }
func endOfMonth(d) {
    let last = daysInMonth(d.year, d.month)
    return dt(d.year, d.month, last, 23, 59, 59, 999, d.tz_offset)
}
func startOfYear(d) { return dt(d.year, 1, 1, 0, 0, 0, 0, d.tz_offset) }
func endOfYear(d)   { return dt(d.year, 12, 31, 23, 59, 59, 999, d.tz_offset) }

func withTimezone(d, tz_offset_minutes) {
    let ts = toTimestamp(d)
    return fromTimestamp(ts, tz_offset_minutes)
}

func toUTC(d) { return withTimezone(d, 0) }

func daysUntil(target, from_ts) {
    let from_d = fromTimestamp(from_ts ?? 0)
    return diffDays(from_d, target)
}

func businessDaysBetween(a, b) {
    let count = 0
    let cur = a
    while isBefore(cur, b) {
        if isWeekday(cur) { count += 1 }
        cur = addDays(cur, 1)
    }
    return count
}
