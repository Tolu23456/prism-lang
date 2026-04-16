/* Prism Standard Library — math module
   Pure Prism implementation — no Python imports.
   Full mathematical toolkit: arithmetic, number theory,
   statistics, geometry, combinatorics, and easing.
*/

const TAU          = PI * 2
const GOLDEN_RATIO = 1.6180339887498948
const SQRT2        = 1.4142135623730951
const SQRT3        = 1.7320508075688772
const LN2          = 0.6931471805599453
const LN10         = 2.302585092994046
const LOG2E        = 1.4426950408889634
const LOG10E       = 0.4342944819032518
const EPSILON      = 2.220446049250313e-16
const MAX_SAFE_INT = 9007199254740991

/* ── Basic ───────────────────────────────────────────────── */

func clamp(val, lo, hi) {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
}

func lerp(a, b, t) {
    return a + (b - a) * t
}

func inverseLerp(a, b, val) {
    if a == b { return 0 }
    return (val - a) / (b - a)
}

func mapRange(val, in_min, in_max, out_min, out_max) {
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
}

func signOf(x) {
    if x > 0 { return 1 }
    if x < 0 { return -1 }
    return 0
}

func isEven(n)  { return n % 2 == 0 }
func isOdd(n)   { return n % 2 != 0 }
func isInt(x)   { return floor(x) == x }

func toRadians(deg) { return deg * PI / 180 }
func toDegrees(rad) { return rad * 180 / PI }

func sqr(x)   { return x * x }
func cube(x)  { return x * x * x }

func nthRoot(x, n) {
    if n == 0 { error("nthRoot: n cannot be zero") }
    return pow(x, 1.0 / n)
}

func hypot2d(x, y)    { return sqrt(x * x + y * y) }
func hypot3d(x, y, z) { return sqrt(x * x + y * y + z * z) }

/* ── Number Theory ──────────────────────────────────────── */

func gcdOf(a, b) {
    a = abs(int(a))
    b = abs(int(b))
    while b != 0 {
        let t = b
        b = a % b
        a = t
    }
    return a
}

func lcmOf(a, b) {
    let g = gcdOf(a, b)
    if g == 0 { return 0 }
    return abs(a * b) // g
}

func isPrime(n) {
    if n < 2 { return false }
    if n == 2 { return true }
    if n % 2 == 0 { return false }
    if n == 3 { return true }
    if n % 3 == 0 { return false }
    let i = 5
    while i * i <= n {
        if n % i == 0 or n % (i + 2) == 0 { return false }
        i += 6
    }
    return true
}

func primes(limit) {
    let sieve = []
    let i = 0
    while i <= limit {
        push(sieve, true)
        i += 1
    }
    sieve[0] = false
    if limit >= 1 { sieve[1] = false }
    let j = 2
    while j * j <= limit {
        if sieve[j] {
            let k = j * j
            while k <= limit {
                sieve[k] = false
                k += j
            }
        }
        j += 1
    }
    let result = []
    let n = 2
    while n <= limit {
        if sieve[n] { push(result, n) }
        n += 1
    }
    return result
}

func primeFactors(n) {
    n = abs(int(n))
    let factors = []
    let d = 2
    while d * d <= n {
        while n % d == 0 {
            push(factors, d)
            n = n // d
        }
        d += 1
    }
    if n > 1 { push(factors, n) }
    return factors
}

func factorial(n) {
    if n < 0 { error("factorial: n must be non-negative") }
    if n == 0 or n == 1 { return 1 }
    let result = 1
    let i = 2
    while i <= n {
        result *= i
        i += 1
    }
    return result
}

func fibonacci(n) {
    if n < 0 { error("fibonacci: n must be non-negative") }
    if n == 0 { return 0 }
    if n == 1 { return 1 }
    let a = 0
    let b = 1
    let i = 2
    while i <= n {
        let t = a + b
        a = b
        b = t
        i += 1
    }
    return b
}

func fibSequence(n) {
    let seq = []
    let i = 0
    while i <= n {
        push(seq, fibonacci(i))
        i += 1
    }
    return seq
}

func combinations(n, k) {
    if k < 0 or k > n { return 0 }
    if k == 0 or k == n { return 1 }
    if k > n - k { k = n - k }
    let result = 1
    let i = 0
    while i < k {
        result = result * (n - i) // (i + 1)
        i += 1
    }
    return result
}

func permutations(n, k) {
    if k < 0 or k > n { return 0 }
    return factorial(n) // factorial(n - k)
}

func digitalRoot(n) {
    n = abs(int(n))
    if n == 0 { return 0 }
    let r = n % 9
    if r == 0 { return 9 }
    return r
}

func sumDigits(n) {
    let s = str(abs(int(n)))
    let total = 0
    for ch in chars(s) {
        if ch >= "0" and ch <= "9" {
            total += parseInt(ch)
        }
    }
    return total
}

/* ── Statistics ─────────────────────────────────────────── */

func statMean(arr) {
    if len(arr) == 0 { error("mean: empty array") }
    let total = 0
    for x in arr { total += x }
    return total / len(arr)
}

func statMedian(arr) {
    if len(arr) == 0 { error("median: empty array") }
    let s = arr.sort()
    let n = len(s)
    let mid = n // 2
    if n % 2 == 0 {
        return (s[mid - 1] + s[mid]) / 2.0
    }
    return s[mid]
}

func statMode(arr) {
    if len(arr) == 0 { error("mode: empty array") }
    let counts = {}
    for x in arr {
        let k = str(x)
        if has(counts, k) { counts[k] = counts[k] + 1 }
        else               { counts[k] = 1 }
    }
    let best_key = ""
    let best_val = 0
    for k in keys(counts) {
        if counts[k] > best_val {
            best_val = counts[k]
            best_key = k
        }
    }
    return parseFloat(best_key)
}

func statVariance(arr, population) {
    if len(arr) < 2 { error("variance: need at least 2 values") }
    let m = statMean(arr)
    let sq_sum = 0
    for x in arr { sq_sum += (x - m) * (x - m) }
    let denom = population ?? false
    if denom { return sq_sum / len(arr) }
    return sq_sum / (len(arr) - 1)
}

func statStdDev(arr, population) {
    return sqrt(statVariance(arr, population))
}

func statMin(arr) {
    if len(arr) == 0 { error("statMin: empty array") }
    let m = arr[0]
    for x in arr { if x < m { m = x } }
    return m
}

func statMax(arr) {
    if len(arr) == 0 { error("statMax: empty array") }
    let m = arr[0]
    for x in arr { if x > m { m = x } }
    return m
}

func statRange(arr) {
    return statMax(arr) - statMin(arr)
}

func statSum(arr) {
    let total = 0
    for x in arr { total += x }
    return total
}

func statPercentile(arr, p) {
    if len(arr) == 0 { error("percentile: empty array") }
    let s = arr.sort()
    let idx = (p / 100.0) * (len(s) - 1)
    let lo = int(floor(idx))
    let hi = lo + 1
    if hi >= len(s) { return s[len(s) - 1] }
    let frac = idx - lo
    return s[lo] + frac * (s[hi] - s[lo])
}

func statZScore(x, mean, stddev) {
    if stddev == 0 { error("z-score: stddev cannot be zero") }
    return (x - mean) / stddev
}

func statNormalize(arr) {
    let lo = statMin(arr)
    let hi = statMax(arr)
    let span = hi - lo
    if span == 0 { return arr }
    let result = []
    for x in arr { push(result, (x - lo) / span) }
    return result
}

func statCovariance(xs, ys) {
    if len(xs) != len(ys) { error("covariance: arrays must be same length") }
    let mx = statMean(xs)
    let my = statMean(ys)
    let total = 0
    let i = 0
    while i < len(xs) {
        total += (xs[i] - mx) * (ys[i] - my)
        i += 1
    }
    return total / (len(xs) - 1)
}

func statCorrelation(xs, ys) {
    let cov = statCovariance(xs, ys)
    let sx  = statStdDev(xs, false)
    let sy  = statStdDev(ys, false)
    if sx == 0 or sy == 0 { return 0 }
    return cov / (sx * sy)
}

/* ── Geometry ────────────────────────────────────────────── */

func areaCircle(r)          { return PI * r * r }
func areaRect(w, h)         { return w * h }
func areaTriangle(b, h)     { return 0.5 * b * h }
func areaTriangleSSS(a, b, c) {
    let s = (a + b + c) / 2.0
    return sqrt(s * (s - a) * (s - b) * (s - c))
}
func areaRegularPolygon(n, s) {
    return (n * s * s) / (4.0 * tan(PI / n))
}

func circumferenceCircle(r) { return TAU * r }
func perimeterRect(w, h)    { return 2 * (w + h) }

func volumeSphere(r)     { return (4.0 / 3.0) * PI * r * r * r }
func volumeCube(s)       { return s * s * s }
func volumeCylinder(r, h) { return PI * r * r * h }
func volumeCone(r, h)    { return (1.0 / 3.0) * PI * r * r * h }

func distance2d(x1, y1, x2, y2) {
    return hypot2d(x2 - x1, y2 - y1)
}

func distance3d(x1, y1, z1, x2, y2, z2) {
    return hypot3d(x2 - x1, y2 - y1, z2 - z1)
}

func midpoint2d(x1, y1, x2, y2) {
    return [(x1 + x2) / 2.0, (y1 + y2) / 2.0]
}

func angleToPoint(x1, y1, x2, y2) {
    return atan2(y2 - y1, x2 - x1)
}

/* ── Easing Functions ───────────────────────────────────── */

func easeInQuad(t)    { return t * t }
func easeOutQuad(t)   { return t * (2 - t) }
func easeInOutQuad(t) {
    if t < 0.5 { return 2 * t * t }
    return -1 + (4 - 2 * t) * t
}
func easeInCubic(t)   { return t * t * t }
func easeOutCubic(t)  {
    let s = t - 1
    return s * s * s + 1
}
func easeInSine(t)    { return 1 - cos(t * PI / 2) }
func easeOutSine(t)   { return sin(t * PI / 2) }
func easeInExpo(t)    {
    if t == 0 { return 0 }
    return pow(2, 10 * t - 10)
}
func easeOutElastic(t) {
    let c4 = (2 * PI) / 3.0
    if t == 0 { return 0 }
    if t == 1 { return 1 }
    return pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1
}
func easeInBounce(t)  { return 1 - easeOutBounce(1 - t) }
func easeOutBounce(t) {
    let n1 = 7.5625
    let d1 = 2.75
    if t < 1 / d1 {
        return n1 * t * t
    } elif t < 2 / d1 {
        t -= 1.5 / d1
        return n1 * t * t + 0.75
    } elif t < 2.5 / d1 {
        t -= 2.25 / d1
        return n1 * t * t + 0.9375
    } else {
        t -= 2.625 / d1
        return n1 * t * t + 0.984375
    }
}

/* ── Bezier ─────────────────────────────────────────────── */

func bezierLinear(t, p0, p1) {
    return lerp(p0, p1, t)
}

func bezierQuadratic(t, p0, p1, p2) {
    let a = lerp(p0, p1, t)
    let b = lerp(p1, p2, t)
    return lerp(a, b, t)
}

func bezierCubic(t, p0, p1, p2, p3) {
    let a = bezierQuadratic(t, p0, p1, p2)
    let b = bezierQuadratic(t, p1, p2, p3)
    return lerp(a, b, t)
}
