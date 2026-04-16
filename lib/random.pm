/* Prism Standard Library — random module
   Pure Prism implementation — no Python imports.
   LCG + XorShift PRNG, full statistical distribution suite,
   shuffle, sampling, dice, and noise utilities.
*/

/* ── PRNG State (LCG + XorShift hybrid) ─────────────────── */

let _seed = 123456789

func _lcg() {
    _seed = (_seed * 1664525 + 1013904223) % 4294967296
    return _seed
}

func _xorshift() {
    _seed = _seed ^ (_seed << 13)
    _seed = _seed % 4294967296
    _seed = _seed ^ (_seed >> 7)
    _seed = _seed % 4294967296
    _seed = _seed ^ (_seed << 17)
    _seed = _seed % 4294967296
    return abs(_seed)
}

func _raw() {
    let a = _lcg()
    let b = _xorshift()
    return abs((a ^ b) % 4294967296)
}

func setSeed(s) {
    _seed = abs(int(s)) % 4294967296 + 1
    _lcg()
}

/* ── Core ────────────────────────────────────────────────── */

func randomFloat() {
    return _raw() / 4294967296.0
}

func randomInt(lo, hi) {
    if lo > hi { error("randomInt: lo must be <= hi") }
    return lo + int(_raw() % (hi - lo + 1))
}

func randomBool(probability) {
    probability = probability ?? 0.5
    return randomFloat() < probability
}

func randomSign() {
    if randomBool() { return 1 }
    return -1
}

func randomBetween(lo, hi) {
    return lo + randomFloat() * (hi - lo)
}

/* ── Collections ─────────────────────────────────────────── */

func choice(arr) {
    if len(arr) == 0 { error("choice: empty array") }
    return arr[int(_raw() % len(arr))]
}

func choices(arr, n) {
    let result = []
    let i = 0
    while i < n {
        push(result, choice(arr))
        i += 1
    }
    return result
}

func sample(arr, n) {
    if n > len(arr) { error("sample: n larger than array") }
    let pool  = slice(arr, 0)
    let result = []
    let i = 0
    while i < n {
        let idx = int(_raw() % len(pool))
        push(result, pool[idx])
        pool = slice(pool, 0, idx) + slice(pool, idx + 1)
        i += 1
    }
    return result
}

func shuffle(arr) {
    let result = slice(arr, 0)
    let i = len(result) - 1
    while i > 0 {
        let j = int(_raw() % (i + 1))
        let t = result[i]
        result[i] = result[j]
        result[j] = t
        i -= 1
    }
    return result
}

func weightedChoice(items, weights) {
    if len(items) != len(weights) { error("weightedChoice: items/weights length mismatch") }
    let total = 0
    for w in weights { total += w }
    let r = randomFloat() * total
    let cumulative = 0
    let i = 0
    while i < len(items) {
        cumulative += weights[i]
        if r <= cumulative { return items[i] }
        i += 1
    }
    return items[len(items) - 1]
}

/* ── Distributions ──────────────────────────────────────── */

func randomNormal(mean, stddev) {
    mean   = mean   ?? 0.0
    stddev = stddev ?? 1.0
    let u1 = randomFloat()
    let u2 = randomFloat()
    if u1 == 0 { u1 = 0.000001 }
    let z = sqrt(-2.0 * log(u1)) * cos(TAU * u2)
    return mean + z * stddev
}

const TAU = 6.283185307179586

func randomExponential(rate) {
    rate = rate ?? 1.0
    let u = randomFloat()
    if u == 0 { u = 0.000001 }
    return -log(u) / rate
}

func randomPoisson(lambda) {
    lambda = lambda ?? 1.0
    let L = exp(-lambda)
    let k = 0
    let p = 1.0
    while p > L {
        p = p * randomFloat()
        k += 1
    }
    return k - 1
}

func randomBinomial(n, p) {
    let count = 0
    let i = 0
    while i < n {
        if randomFloat() < p { count += 1 }
        i += 1
    }
    return count
}

func randomGeometric(p) {
    if p <= 0 or p > 1 { error("geometric: p must be in (0,1]") }
    return int(log(randomFloat()) / log(1 - p)) + 1
}

func randomTriangular(lo, hi, mode) {
    lo   = lo   ?? 0.0
    hi   = hi   ?? 1.0
    mode = mode ?? (lo + hi) / 2.0
    let u = randomFloat()
    let fc = (mode - lo) / (hi - lo)
    if u < fc {
        return lo + sqrt(u * (hi - lo) * (mode - lo))
    }
    return hi - sqrt((1 - u) * (hi - lo) * (hi - mode))
}

/* ── Noise (1D Value Noise) ─────────────────────────────── */

let _noise_table = void

func _initNoise() {
    let saved_seed = _seed
    setSeed(42)
    let t = []
    let i = 0
    while i < 256 {
        push(t, randomFloat())
        i += 1
    }
    _noise_table = t
    _seed = saved_seed
}

func valueNoise(x) {
    if _noise_table == void { _initNoise() }
    let xi = int(floor(x)) % 256
    if xi < 0 { xi = xi + 256 }
    let xf = x - floor(x)
    let smoothed = xf * xf * (3 - 2 * xf)
    let a = _noise_table[xi]
    let b = _noise_table[(xi + 1) % 256]
    return a + smoothed * (b - a)
}

func valueNoise2d(x, y) {
    if _noise_table == void { _initNoise() }
    let xi = int(floor(x)) % 256
    let yi = int(floor(y)) % 256
    if xi < 0 { xi = xi + 256 }
    if yi < 0 { yi = yi + 256 }
    let xf = x - floor(x)
    let yf = y - floor(y)
    let sx = xf * xf * (3 - 2 * xf)
    let sy = yf * yf * (3 - 2 * yf)
    let aa = _noise_table[(xi + yi * 37) % 256]
    let ba = _noise_table[((xi + 1) + yi * 37) % 256]
    let ab = _noise_table[(xi + (yi + 1) * 37) % 256]
    let bb = _noise_table[((xi + 1) + (yi + 1) * 37) % 256]
    let top    = aa + sx * (ba - aa)
    let bottom = ab + sx * (bb - ab)
    return top + sy * (bottom - top)
}

/* ── Dice & Games ────────────────────────────────────────── */

func rollDice(sides)       { return randomInt(1, sides) }
func rollDice6()           { return rollDice(6) }
func rollDice20()          { return rollDice(20) }
func rollMultiple(n, sides) {
    let result = []
    let i = 0
    while i < n {
        push(result, rollDice(sides))
        i += 1
    }
    return result
}
func rollSumDice(n, sides)  {
    let rolls = rollMultiple(n, sides)
    let total = 0
    for r in rolls { total += r }
    return total
}

/* ── String / ID ─────────────────────────────────────────── */

func randomString(length, alphabet) {
    alphabet = alphabet ?? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    let chs = chars(alphabet)
    let result = ""
    let i = 0
    while i < length {
        result = result + chs[int(_raw() % len(chs))]
        i += 1
    }
    return result
}

func randomHex(bytes) {
    let hex_chars = chars("0123456789abcdef")
    let result = ""
    let i = 0
    while i < bytes * 2 {
        result = result + hex_chars[int(_raw() % 16)]
        i += 1
    }
    return result
}

func randomColor() {
    return "#" + randomHex(3)
}

func randomElement(arr, exclude) {
    exclude = exclude ?? []
    let filtered = []
    for x in arr {
        let found = false
        for e in exclude {
            if str(x) == str(e) { found = true }
        }
        if not found { push(filtered, x) }
    }
    if len(filtered) == 0 { error("randomElement: no elements left after exclusion") }
    return choice(filtered)
}
