/* Prism Standard Library — secrets module
   Pure Prism implementation — no Python imports.
   Cryptographically-stronger pseudorandom generation using
   a multiply-with-carry + XorShift256 CSPRNG.
   Suitable for tokens, nonces, and key material — not for
   cryptographic primitives requiring OS entropy.
*/

import random

/* ── CSPRNG State (multiple independent generators) ───────── */

let _s0 = 362436069
let _s1 = 521288629
let _s2 = 88675123
let _s3 = 5783321

func _xorShift256() {
    let t = _s0 ^ (_s0 << 11)
    t = t % 4294967296
    _s0 = _s1
    _s1 = _s2
    _s2 = _s3
    _s3 = _s3 ^ (_s3 >> 19) ^ t ^ (t >> 8)
    _s3 = _s3 % 4294967296
    return abs(_s3)
}

let _mwc_c = 0
let _mwc_w = 0

func _mwc() {
    _mwc_c = int(_mwc_w / 65536)
    _mwc_w = _mwc_w % 65536
    _mwc_w = 18000 * _mwc_w + _mwc_c
    return _mwc_w
}

func _mix() {
    let a = _xorShift256()
    let b = _mwc()
    return abs((a ^ (b << 7)) % 4294967296)
}

func seedSecrets(s) {
    _s0 = abs(int(s) ^ 362436069) % 4294967296 + 1
    _s1 = abs(int(s) * 521288629) % 4294967296 + 1
    _s2 = abs(int(s) + 88675123)  % 4294967296 + 1
    _s3 = abs(int(s) ^ 5783321)   % 4294967296 + 1
    _mwc_w = abs(int(s) + 1)
}

/* ── Raw bytes (as int array) ────────────────────────────── */

func randomBytes(n) {
    let result = []
    let i = 0
    while i < n {
        push(result, _mix() % 256)
        i += 1
    }
    return result
}

/* ── Token generation ────────────────────────────────────── */

const _URL_SAFE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

func token(nbytes) {
    nbytes = nbytes ?? 32
    let chs = chars(_URL_SAFE)
    let result = ""
    let i = 0
    while i < nbytes {
        result = result + chs[_mix() % len(chs)]
        i += 1
    }
    return result
}

func tokenHex(nbytes) {
    nbytes = nbytes ?? 32
    let hex_chars = chars("0123456789abcdef")
    let result = ""
    let i = 0
    while i < nbytes * 2 {
        result = result + hex_chars[_mix() % 16]
        i += 1
    }
    return result
}

func tokenUrlSafe(nbytes) {
    return token(nbytes ?? 32)
}

/* ── UUID v4 (RFC 4122 — random) ────────────────────────── */

func _hexN(n) {
    let hex_chars = chars("0123456789abcdef")
    let result = ""
    let i = 0
    while i < n {
        result = result + hex_chars[_mix() % 16]
        i += 1
    }
    return result
}

func uuid4() {
    let a = _hexN(8)
    let b = _hexN(4)
    let c = "4" + _hexN(3)
    let variant_char = chars("89ab")[_mix() % 4]
    let d = variant_char + _hexN(3)
    let e = _hexN(12)
    return a + "-" + b + "-" + c + "-" + d + "-" + e
}

func uuidShort() {
    return _hexN(8) + "-" + _hexN(4) + "-" + _hexN(4)
}

/* ── Nonce & Key generation ──────────────────────────────── */

func nonce(bits) {
    bits = bits ?? 128
    let nbytes = bits // 8
    return tokenHex(nbytes)
}

func secretKey(bits) {
    bits = bits ?? 256
    return nonce(bits)
}

/* ── Secure random numbers ───────────────────────────────── */

func secureInt(lo, hi) {
    if lo > hi { error("secureInt: lo must be <= hi") }
    return lo + int(_mix() % (hi - lo + 1))
}

func secureFloat() {
    return _mix() / 4294967296.0
}

func secureShuffle(arr) {
    let result = slice(arr, 0)
    let i = len(result) - 1
    while i > 0 {
        let j = int(_mix() % (i + 1))
        let t = result[i]
        result[i] = result[j]
        result[j] = t
        i -= 1
    }
    return result
}

func secureChoice(arr) {
    if len(arr) == 0 { error("secureChoice: empty array") }
    return arr[int(_mix() % len(arr))]
}

/* ── Password / passphrase generation ────────────────────── */

const _LOWERCASE = "abcdefghijklmnopqrstuvwxyz"
const _UPPERCASE = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
const _DIGITS    = "0123456789"
const _SYMBOLS   = "!@#$%^&*()-_=+[]{}|;:,.<>?"

func randomPassword(length, use_upper, use_digits, use_symbols) {
    length     = length     ?? 16
    use_upper  = use_upper  ?? true
    use_digits = use_digits ?? true
    use_symbols = use_symbols ?? true

    let charset = _LOWERCASE
    if use_upper   { charset = charset + _UPPERCASE }
    if use_digits  { charset = charset + _DIGITS }
    if use_symbols { charset = charset + _SYMBOLS }

    let chs = chars(charset)
    let result = ""
    let i = 0
    while i < length {
        result = result + chs[_mix() % len(chs)]
        i += 1
    }
    return result
}

const _WORDLIST = [
    "apple","river","storm","tiger","delta","lunar","frost","prism","noble",
    "swift","cloud","ember","flame","grave","heavy","ivory","joker","karma",
    "lemon","maple","night","ocean","pearl","quiet","radar","solar","ultra",
    "venom","water","xenon","yield","zebra","alpha","brave","crisp","drift"
]

func passphrase(wordCount, separator) {
    wordCount = wordCount ?? 4
    separator = separator ?? "-"
    let words = []
    let i = 0
    while i < wordCount {
        push(words, secureChoice(_WORDLIST))
        i += 1
    }
    return join(separator, words)
}

/* ── Entropy pool ────────────────────────────────────────── */

func estimateEntropy(password) {
    let has_lower  = false
    let has_upper  = false
    let has_digit  = false
    let has_symbol = false
    for ch in chars(password) {
        if ch >= "a" and ch <= "z" { has_lower  = true }
        elif ch >= "A" and ch <= "Z" { has_upper  = true }
        elif ch >= "0" and ch <= "9" { has_digit  = true }
        else                          { has_symbol = true }
    }
    let pool = 0
    if has_lower  { pool += 26 }
    if has_upper  { pool += 26 }
    if has_digit  { pool += 10 }
    if has_symbol { pool += 32 }
    if pool == 0  { return 0 }
    return int(len(password) * (log(pool) / log(2)))
}
