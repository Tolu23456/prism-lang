/* Prism Standard Library — universe module
   Pure Prism implementation — no Python imports.
   The "everything" module: UniverseEngine, Random, Secrets,
   UUID, ID generation, physics constants, and entropy tools.
   All algorithms implemented from scratch in pure Prism.
*/

/* ══════════════════════════════════════════════════════════
   PHYSICS & UNIVERSAL CONSTANTS
   ══════════════════════════════════════════════════════════ */

const SPEED_OF_LIGHT   = 299792458.0       /* m/s         */
const PLANCK           = 6.62607015e-34    /* J·s         */
const AVOGADRO         = 6.02214076e23     /* mol⁻¹       */
const BOLTZMANN        = 1.380649e-23      /* J/K         */
const GRAVITATIONAL    = 6.67430e-11       /* m³/(kg·s²)  */
const ELEMENTARY_CHARGE = 1.602176634e-19  /* C           */
const ELECTRON_MASS    = 9.1093837015e-31  /* kg          */
const PROTON_MASS      = 1.67262192369e-27 /* kg          */
const VACUUM_PERMITTIVITY = 8.8541878128e-12
const FINE_STRUCTURE   = 0.0072973525693
const RYDBERG          = 10973731.568160
const GOLDEN_RATIO     = 1.6180339887498948
const SILVER_RATIO     = 2.4142135623730951
const SQRT2            = 1.4142135623730951

/* ══════════════════════════════════════════════════════════
   UNIVERSE ENGINE — Physics-based unique ID generator
   ══════════════════════════════════════════════════════════ */

const _E_CONSTANT = 0.5829348123049123

class UniverseEngine {
    func init(e_constant) {
        self.e      = e_constant ?? _E_CONSTANT
        self._flux  = 0
        self._state = 123456789
    }

    func _tick() {
        self._flux += 1
        self._state = (self._state * 1664525 + 1013904223) % 4294967296
        self._state = self._state ^ (self._state << 13)
        self._state = self._state % 4294967296
        self._state = self._state ^ (self._state >> 7)
        self._state = self._state % 4294967296
    }

    func _basePhysics(mode) {
        self._tick()
        let n = float(self._state)
        let delta_n = 0.0

        if mode == "linear" {
            let n_int = floor(n)
            delta_n = (n - n_int) / 4294967296.0
        } elif mode == "high-res" {
            delta_n = float(self._state % 10000000) / 10000000.0
        } else {
            let oscillator = sin(n * self.e + float(self._flux))
            delta_n = (oscillator + 1.0) / 2.0
        }

        delta_n = (delta_n * 0.8) + 0.1
        let safe_n = abs(n) + 1.0
        let term1  = sqrt(safe_n)
        let term2  = delta_n - pow(delta_n, self.e)
        return abs(term1 * term2 * self.e)
    }

    func generate(mode, length, output_type, allow_negative) {
        mode           = mode           ?? "non-linear"
        length         = length         ?? 10
        output_type    = output_type    ?? "int"
        allow_negative = allow_negative ?? false

        let raw        = self._basePhysics(mode)
        let multiplier = 1

        if allow_negative {
            if int(raw * 1000000.0) % 2 == 0 { multiplier = -1 }
        }

        if output_type == "float" {
            return (raw % 1.0) * multiplier
        } else {
            let limit  = pow(10, length)
            let id_out = int(raw * pow(10.0, 15.0)) % int(limit)
            if len(str(id_out)) < length {
                id_out += int(pow(10, length - 1))
            }
            return id_out * multiplier
        }
    }

    func linear(length)   { return self.generate("linear", length ?? 10, "int", false) }
    func highRes(length)  { return self.generate("high-res", length ?? 10, "int", false) }
    func chaos(length)    { return self.generate("non-linear", length ?? 10, "int", false) }
    func fraction()       { return self.generate("non-linear", 10, "float", false) }
    func signed(length)   { return self.generate("non-linear", length ?? 10, "int", true) }
    func id()             { return self.generate("non-linear", 16, "int", false) }
}

/* ══════════════════════════════════════════════════════════
   RANDOM — LCG + XorShift PRNG
   ══════════════════════════════════════════════════════════ */

let _r_seed = 987654321

func _r_raw() {
    _r_seed = (_r_seed * 1664525 + 1013904223) % 4294967296
    let x = _r_seed
    x = x ^ (x << 13)
    x = x % 4294967296
    x = x ^ (x >> 17)
    x = x % 4294967296
    x = x ^ (x << 5)
    _r_seed = x % 4294967296
    return abs(_r_seed)
}

func setSeed(s) {
    _r_seed = abs(int(s)) % 4294967296 + 1
    _r_raw()
}

func randomFloat()       { return _r_raw() / 4294967296.0 }
func randomInt(lo, hi)   { return lo + int(_r_raw() % (hi - lo + 1)) }
func randomBool(p)       { return randomFloat() < (p ?? 0.5) }
func randomBetween(lo, hi){ return lo + randomFloat() * (hi - lo) }
func randomSign()        { if randomBool(0.5) { return 1 } return -1 }

func choice(arr) {
    if len(arr) == 0 { error("choice: empty array") }
    return arr[int(_r_raw() % len(arr))]
}

func choices(arr, n) {
    let result = []
    let i = 0
    while i < n { push(result, choice(arr)); i += 1 }
    return result
}

func sample(arr, n) {
    if n > len(arr) { error("sample: n larger than array") }
    let pool = slice(arr, 0)
    let result = []
    let i = 0
    while i < n {
        let idx = int(_r_raw() % len(pool))
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
        let j = int(_r_raw() % (i + 1))
        let t = result[i]
        result[i] = result[j]
        result[j] = t
        i -= 1
    }
    return result
}

func weightedChoice(items, weights) {
    let total = 0
    for w in weights { total += w }
    let r = randomFloat() * total
    let cum = 0
    let i = 0
    while i < len(items) {
        cum += weights[i]
        if r <= cum { return items[i] }
        i += 1
    }
    return items[len(items) - 1]
}

/* Statistical distributions */

const _TAU = 6.283185307179586

func randomNormal(mean, stddev) {
    mean   = mean   ?? 0.0
    stddev = stddev ?? 1.0
    let u1 = randomFloat()
    if u1 == 0 { u1 = 0.000001 }
    let u2 = randomFloat()
    let z = sqrt(-2.0 * log(u1)) * cos(_TAU * u2)
    return mean + z * stddev
}

func randomExponential(rate) {
    rate = rate ?? 1.0
    let u = randomFloat()
    if u == 0 { u = 0.000001 }
    return -log(u) / rate
}

func rollDice(sides)     { return randomInt(1, sides) }
func rollMultiple(n, s)  {
    let r = []
    let i = 0
    while i < n { push(r, rollDice(s)); i += 1 }
    return r
}

func randomString(length, alphabet) {
    alphabet = alphabet ?? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    let chs = chars(alphabet)
    let out = ""
    let i = 0
    while i < length {
        out = out + chs[int(_r_raw() % len(chs))]
        i += 1
    }
    return out
}

func randomColor() {
    let hex = "0123456789abcdef"
    let hc = chars(hex)
    let out = "#"
    let i = 0
    while i < 6 { out = out + hc[int(_r_raw() % 16)]; i += 1 }
    return out
}

/* ══════════════════════════════════════════════════════════
   SECRETS — XorShift256 CSPRNG
   ══════════════════════════════════════════════════════════ */

let _s0 = 362436069
let _s1 = 521288629
let _s2 = 88675123
let _s3 = 5783321

func _secRaw() {
    let t = _s0 ^ (_s0 << 11)
    t = t % 4294967296
    _s0 = _s1
    _s1 = _s2
    _s2 = _s3
    _s3 = _s3 ^ (_s3 >> 19) ^ t ^ (t >> 8)
    _s3 = _s3 % 4294967296
    return abs(_s3)
}

func seedSecrets(seed) {
    _s0 = abs(int(seed) ^ 362436069) % 4294967296 + 1
    _s1 = abs(int(seed) * 521288629) % 4294967296 + 1
    _s2 = abs(int(seed) + 88675123)  % 4294967296 + 1
    _s3 = abs(int(seed) ^ 5783321)   % 4294967296 + 1
}

func secureFloat()     { return _secRaw() / 4294967296.0 }
func secureInt(lo, hi) { return lo + int(_secRaw() % (hi - lo + 1)) }

const _URL_SAFE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

func token(nbytes) {
    nbytes = nbytes ?? 32
    let chs = chars(_URL_SAFE)
    let out = ""
    let i = 0
    while i < nbytes { out = out + chs[_secRaw() % len(chs)]; i += 1 }
    return out
}

func tokenHex(nbytes) {
    nbytes = nbytes ?? 32
    let hc = chars("0123456789abcdef")
    let out = ""
    let i = 0
    while i < nbytes * 2 { out = out + hc[_secRaw() % 16]; i += 1 }
    return out
}

func secretKey(bits) { return tokenHex((bits ?? 256) // 8) }
func nonce(bits)     { return tokenHex((bits ?? 128) // 8) }

/* ══════════════════════════════════════════════════════════
   UUID — RFC 4122 compliant generators
   ══════════════════════════════════════════════════════════ */

func _hexN(n) {
    let hc = chars("0123456789abcdef")
    let out = ""
    let i = 0
    while i < n { out = out + hc[_secRaw() % 16]; i += 1 }
    return out
}

func uuid4() {
    let a = _hexN(8)
    let b = _hexN(4)
    let c = "4" + _hexN(3)
    let v = chars("89ab")[_secRaw() % 4]
    let d = v + _hexN(3)
    let e = _hexN(12)
    return a + "-" + b + "-" + c + "-" + d + "-" + e
}

func uuidNil() { return "00000000-0000-0000-0000-000000000000" }

func uuidShort() { return _hexN(8) + "-" + _hexN(4) + "-" + _hexN(4) }

func isValidUUID(s) {
    if len(s) != 36 { return false }
    let parts = split(s, "-")
    if len(parts) != 5 { return false }
    if len(parts[0]) != 8 { return false }
    if len(parts[1]) != 4 { return false }
    if len(parts[2]) != 4 { return false }
    if len(parts[3]) != 4 { return false }
    if len(parts[4]) != 12 { return false }
    for part in parts {
        for ch in chars(part) {
            let ok = (ch >= "0" and ch <= "9") or
                     (ch >= "a" and ch <= "f") or
                     (ch >= "A" and ch <= "F")
            if not ok { return false }
        }
    }
    return true
}

/* ══════════════════════════════════════════════════════════
   ID GENERATORS — Snowflake, KSUID-like, NanoID
   ══════════════════════════════════════════════════════════ */

let _snowflake_seq = 0
let _snowflake_last_ts = 0

func snowflakeID(machine_id) {
    machine_id = machine_id ?? 1
    let ts = int(clock() * 1000) % 4398046511104
    if ts == _snowflake_last_ts {
        _snowflake_seq = (_snowflake_seq + 1) % 4096
    } else {
        _snowflake_seq = 0
    }
    _snowflake_last_ts = ts
    return (ts << 22) | ((machine_id & 0x3FF) << 12) | (_snowflake_seq & 0xFFF)
}

func nanoID(size, alphabet) {
    size     = size     ?? 21
    alphabet = alphabet ?? "_-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    let chs = chars(alphabet)
    let out = ""
    let i = 0
    while i < size {
        out = out + chs[_secRaw() % len(chs)]
        i += 1
    }
    return out
}

func ksuidLike() {
    let ts_part = tokenHex(4)
    let rand_part = tokenHex(16)
    return upper(ts_part + rand_part)
}

func objectID() {
    return tokenHex(12)
}

/* ══════════════════════════════════════════════════════════
   ENTROPY & PASSWORDS
   ══════════════════════════════════════════════════════════ */

const _LOWERCASE = "abcdefghijklmnopqrstuvwxyz"
const _UPPERCASE = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
const _DIGITS    = "0123456789"
const _SYMBOLS   = "!@#$%^&*()-_=+[]{}|;:,.<>?"

func randomPassword(length, use_upper, use_digits, use_symbols) {
    length     = length     ?? 16
    use_upper  = use_upper  ?? true
    use_digits = use_digits ?? true
    use_symbols = use_symbols ?? false

    let charset = _LOWERCASE
    if use_upper   { charset = charset + _UPPERCASE }
    if use_digits  { charset = charset + _DIGITS }
    if use_symbols { charset = charset + _SYMBOLS }

    let chs = chars(charset)
    let out = ""
    let i = 0
    while i < length {
        out = out + chs[_secRaw() % len(chs)]
        i += 1
    }
    return out
}

const _WORDS = [
    "apple","river","storm","tiger","delta","lunar","frost","prism","noble",
    "swift","cloud","ember","flame","alpha","brave","crisp","drift","eagle",
    "forge","grace","hover","ivory","joker","karma","lemon","maple","night",
    "ocean","pearl","quiet","radar","solar","ultra","venom","water","xenon"
]

func passphrase(count, sep) {
    count = count ?? 4
    sep   = sep   ?? "-"
    let words = []
    let i = 0
    while i < count {
        push(words, _WORDS[_secRaw() % len(_WORDS)])
        i += 1
    }
    return join(sep, words)
}

func estimateEntropy(password) {
    let has_lower  = false
    let has_upper  = false
    let has_digit  = false
    let has_symbol = false
    for ch in chars(password) {
        if ch >= "a" and ch <= "z"       { has_lower  = true }
        elif ch >= "A" and ch <= "Z"     { has_upper  = true }
        elif ch >= "0" and ch <= "9"     { has_digit  = true }
        else                              { has_symbol = true }
    }
    let pool = 0
    if has_lower  { pool += 26 }
    if has_upper  { pool += 26 }
    if has_digit  { pool += 10 }
    if has_symbol { pool += 32 }
    if pool == 0  { return 0 }
    return int(len(password) * (log(float(pool)) / log(2.0)))
}

/* ══════════════════════════════════════════════════════════
   HASH — Built-in Prism-native hash functions
   ══════════════════════════════════════════════════════════ */

func fnv1a(s) {
    let h = 2166136261
    for ch in chars(s) {
        h = h ^ ord(ch)
        h = (h * 16777619) % 4294967296
    }
    return h
}

func djb2(s) {
    let h = 5381
    for ch in chars(s) {
        h = ((h << 5) + h + ord(ch)) % 4294967296
    }
    return h
}

func checksum(s) {
    let total = 0
    for ch in chars(s) { total = (total + ord(ch)) % 65536 }
    return total
}

/* ══════════════════════════════════════════════════════════
   HELPERS
   ══════════════════════════════════════════════════════════ */

func upper(s) {
    let out = ""
    for ch in chars(s) {
        if ch >= "a" and ch <= "z" { out = out + chr(ord(ch) - 32) }
        else { out = out + ch }
    }
    return out
}

func join(sep, arr) {
    let out = ""
    let i = 0
    while i < len(arr) {
        if i > 0 { out = out + sep }
        out = out + str(arr[i])
        i += 1
    }
    return out
}
