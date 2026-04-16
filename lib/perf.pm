/* Prism Standard Library — perf module
   Performance measurement, benchmarking, and profiling utilities.
   Uses the built-in `time` module (os.clock / time.now) for timing.
*/

/* ── Low-level timing ────────────────────────────────────── */

func measure(f) {
    let t0     = clock()
    let result = f()
    let t1     = clock()
    return {
        "result":  result,
        "elapsed": t1 - t0
    }
}

func measureMs(f) {
    let m = measure(f)
    return {
        "result":  m["result"],
        "elapsedMs": m["elapsed"] * 1000.0
    }
}

/* ── Stopwatch ───────────────────────────────────────────── */

class Stopwatch {
    func init() {
        self._start   = void
        self._elapsed = 0.0
        self._running = false
        self._laps    = []
    }

    func start() {
        if not self._running {
            self._start   = clock()
            self._running = true
        }
        return self
    }

    func stop() {
        if self._running {
            self._elapsed += clock() - self._start
            self._running  = false
        }
        return self
    }

    func lap() {
        let current = self.elapsed()
        let last    = 0.0
        if len(self._laps) > 0 { last = self._laps[len(self._laps) - 1]["total"] }
        push(self._laps, {
            "lap":   len(self._laps) + 1,
            "split": current - last,
            "total": current
        })
        return self
    }

    func reset() {
        self._start   = void
        self._elapsed = 0.0
        self._running = false
        self._laps    = []
        return self
    }

    func restart() {
        self.reset()
        self.start()
        return self
    }

    func elapsed() {
        if self._running {
            return self._elapsed + (clock() - self._start)
        }
        return self._elapsed
    }

    func elapsedMs()  { return self.elapsed() * 1000.0 }
    func laps()       { return self._laps }
    func isRunning()  { return self._running }

    func report() {
        return {
            "elapsed":   self.elapsed(),
            "elapsedMs": self.elapsedMs(),
            "running":   self._running,
            "laps":      self._laps
        }
    }
}

func stopwatch() {
    return new Stopwatch()
}

/* ── Benchmarking ────────────────────────────────────────── */

func bench(f, n) {
    n = n ?? 100
    if n <= 0 { error("perf.bench: n must be > 0") }

    let times = []
    let i = 0
    while i < n {
        let t0 = clock()
        f()
        let t1 = clock()
        push(times, (t1 - t0) * 1000.0)
        i += 1
    }

    let total = 0.0
    for t in times { total += t }
    let avg = total / n

    let sorted = times.sort()
    let med = sorted[n // 2]

    let mn = sorted[0]
    let mx = sorted[n - 1]

    let variance = 0.0
    for t in times { variance += (t - avg) * (t - avg) }
    variance /= n
    let stddev = sqrt(variance)

    let p95_idx = int(floor(0.95 * (n - 1)))
    let p99_idx = int(floor(0.99 * (n - 1)))
    let p95 = sorted[p95_idx]
    let p99 = sorted[p99_idx]

    return {
        "n":       n,
        "totalMs": total,
        "avgMs":   avg,
        "medMs":   med,
        "minMs":   mn,
        "maxMs":   mx,
        "stdMs":   stddev,
        "p95Ms":   p95,
        "p99Ms":   p99
    }
}

func benchMs(f, n) {
    return bench(f, n)
}

func warmup(f, n) {
    n = n ?? 5
    let i = 0
    while i < n { f(); i += 1 }
}

func benchWarm(f, warmupN, benchN) {
    warmupN = warmupN ?? 5
    benchN  = benchN  ?? 100
    warmup(f, warmupN)
    return bench(f, benchN)
}

/* ── Comparison benchmarking ─────────────────────────────── */

func benchCompare(namedFns, n) {
    n = n ?? 100
    let results = {}
    for pair in namedFns {
        let name = pair[0]
        let f    = pair[1]
        results[name] = bench(f, n)
    }
    return results
}

func benchRank(namedFns, n) {
    let compared = benchCompare(namedFns, n)
    let entries  = []
    for name in keys(compared) {
        push(entries, [name, compared[name]])
    }
    let i = 1
    while i < len(entries) {
        let key = entries[i]
        let kv  = key[1]["avgMs"]
        let j   = i - 1
        while j >= 0 and entries[j][1]["avgMs"] > kv {
            entries[j + 1] = entries[j]
            j -= 1
        }
        entries[j + 1] = key
        i += 1
    }
    let ranked = []
    let rank = 1
    for entry in entries {
        push(ranked, {
            "rank":   rank,
            "name":   entry[0],
            "avgMs":  entry[1]["avgMs"],
            "minMs":  entry[1]["minMs"],
            "maxMs":  entry[1]["maxMs"],
            "p99Ms":  entry[1]["p99Ms"]
        })
        rank += 1
    }
    return ranked
}

/* ── Profiler (call counter + timing) ───────────────────────── */

class Profiler {
    func init() {
        self._calls   = {}
        self._totalMs = {}
        self._minMs   = {}
        self._maxMs   = {}
    }

    func wrap(name, f) {
        let prof = self
        return fn(...args) {
            let t0     = clock()
            let result = f(...args)
            let dt     = (clock() - t0) * 1000.0
            if not has(prof._calls, name) {
                prof._calls[name]   = 0
                prof._totalMs[name] = 0.0
                prof._minMs[name]   = dt
                prof._maxMs[name]   = dt
            }
            prof._calls[name]   += 1
            prof._totalMs[name] += dt
            if dt < prof._minMs[name] { prof._minMs[name] = dt }
            if dt > prof._maxMs[name] { prof._maxMs[name] = dt }
            return result
        }
    }

    func report() {
        let result = {}
        for name in keys(self._calls) {
            let n   = self._calls[name]
            let tot = self._totalMs[name]
            let avgVal = 0.0
            if n > 0 { avgVal = tot / n }
            result[name] = {
                "calls":   n,
                "totalMs": tot,
                "avgMs":   avgVal,
                "minMs":   self._minMs[name],
                "maxMs":   self._maxMs[name]
            }
        }
        return result
    }

    func reset() {
        self._calls   = {}
        self._totalMs = {}
        self._minMs   = {}
        self._maxMs   = {}
        return self
    }

    func calls(name) {
        if has(self._calls, name) { return self._calls[name] }
        return 0
    }
    func totalMs(name) {
        if has(self._totalMs, name) { return self._totalMs[name] }
        return 0.0
    }
    func avgMs(name) {
        let n = self.calls(name)
        if n == 0 { return 0.0 }
        return self.totalMs(name) / n
    }
}

func profiler() {
    return new Profiler()
}

/* ── Rate limiting / throttle helpers ───────────────────────── */

func throttle(f, intervalMs) {
    let lastCall = -intervalMs
    return fn(...args) {
        let now = clock() * 1000.0
        if now - lastCall >= intervalMs {
            lastCall = now
            return f(...args)
        }
        return void
    }
}

/* ── Memoize with hit/miss stats ─────────────────────────── */

func memoStats(f) {
    let cache = {}
    let hits  = 0
    let misses = 0
    let wrapper = fn(...args) {
        let key = str(args)
        if has(cache, key) {
            hits += 1
            return cache[key]
        }
        misses += 1
        let result = f(...args)
        cache[key] = result
        return result
    }
    let stats = fn() => { "hits": hits, "misses": misses, "cached": len(keys(cache)) }
    return [wrapper, stats]
}

/* ── Throughput helpers ──────────────────────────────────── */

func throughput(f, durationMs) {
    let deadline = clock() * 1000.0 + durationMs
    let ops = 0
    while clock() * 1000.0 < deadline {
        f()
        ops += 1
    }
    return {
        "ops":       ops,
        "durationMs": durationMs,
        "opsPerSec":  ops / (durationMs / 1000.0)
    }
}

/* ── Formatting helpers ──────────────────────────────────── */

func fmtBench(b) {
    return "avg=" + str(round(b["avgMs"], 3)) + "ms  " +
           "med=" + str(round(b["medMs"], 3)) + "ms  " +
           "min=" + str(round(b["minMs"], 3)) + "ms  " +
           "max=" + str(round(b["maxMs"], 3)) + "ms  " +
           "p99=" + str(round(b["p99Ms"], 3)) + "ms  " +
           "n="   + str(b["n"])
}

func fmtRank(ranked) {
    let lines = []
    for entry in ranked {
        push(lines, "#" + str(entry["rank"]) + " " +
            entry["name"] + "  avg=" +
            str(round(entry["avgMs"], 3)) + "ms")
    }
    return lines
}

func round(x, decimals) {
    let factor = pow(10, decimals)
    return int(x * factor + 0.5) / factor
}
