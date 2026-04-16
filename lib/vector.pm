/* Prism Standard Library — vector module
   Pure Prism implementation — no Python imports.
   2D, 3D, and N-dimensional vector math, transformations,
   and geometric utilities.
*/

/* ── 2D Vector ───────────────────────────────────────────── */

struct Vec2 { x, y }

func vec2(x, y)       { return new Vec2(x, y) }
func vec2Zero()       { return vec2(0, 0) }
func vec2One()        { return vec2(1, 1) }
func vec2Up()         { return vec2(0, 1) }
func vec2Right()      { return vec2(1, 0) }

func v2Add(a, b)      { return vec2(a.x + b.x, a.y + b.y) }
func v2Sub(a, b)      { return vec2(a.x - b.x, a.y - b.y) }
func v2Scale(v, k)    { return vec2(v.x * k, v.y * k) }
func v2Neg(v)         { return vec2(-v.x, -v.y) }
func v2Dot(a, b)      { return a.x * b.x + a.y * b.y }
func v2Cross(a, b)    { return a.x * b.y - a.y * b.x }
func v2LenSq(v)       { return v.x * v.x + v.y * v.y }
func v2Len(v)         { return sqrt(v2LenSq(v)) }
func v2Dist(a, b)     { return v2Len(v2Sub(b, a)) }
func v2DistSq(a, b)   { return v2LenSq(v2Sub(b, a)) }
func v2Norm(v) {
    let l = v2Len(v)
    if l == 0 { error("v2Norm: zero vector") }
    return vec2(v.x / l, v.y / l)
}
func v2Lerp(a, b, t) {
    return vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)
}
func v2Angle(v) { return atan2(v.y, v.x) }
func v2AngleBetween(a, b) {
    let dot  = v2Dot(a, b)
    let mags = v2Len(a) * v2Len(b)
    if mags == 0 { return 0 }
    return acos(clamp(dot / mags, -1, 1))
}
func v2Rotate(v, angle) {
    let c = cos(angle)
    let s = sin(angle)
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c)
}
func v2Perpendicular(v) { return vec2(-v.y, v.x) }
func v2Reflect(v, normal) {
    let d = 2.0 * v2Dot(v, normal)
    return v2Sub(v, v2Scale(normal, d))
}
func v2Project(v, onto) {
    let dot  = v2Dot(v, onto)
    let lenSq = v2LenSq(onto)
    if lenSq == 0 { error("v2Project: zero vector") }
    return v2Scale(onto, dot / lenSq)
}
func v2Midpoint(a, b) { return v2Scale(v2Add(a, b), 0.5) }
func v2Min(a, b) { return vec2(min(a.x, b.x), min(a.y, b.y)) }
func v2Max(a, b) { return vec2(max(a.x, b.x), max(a.y, b.y)) }
func v2Clamp(v, lo, hi) {
    return vec2(clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y))
}
func v2Eq(a, b) { return a.x == b.x and a.y == b.y }
func v2Str(v)   { return "Vec2(" + str(v.x) + ", " + str(v.y) + ")" }
func v2ToArr(v) { return [v.x, v.y] }
func v2FromArr(a) { return vec2(a[0], a[1]) }

func clamp(val, lo, hi) {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
}

/* ── 3D Vector ───────────────────────────────────────────── */

struct Vec3 { x, y, z }

func vec3(x, y, z)    { return new Vec3(x, y, z) }
func vec3Zero()       { return vec3(0, 0, 0) }
func vec3One()        { return vec3(1, 1, 1) }
func vec3Up()         { return vec3(0, 1, 0) }
func vec3Right()      { return vec3(1, 0, 0) }
func vec3Forward()    { return vec3(0, 0, 1) }

func v3Add(a, b)      { return vec3(a.x + b.x, a.y + b.y, a.z + b.z) }
func v3Sub(a, b)      { return vec3(a.x - b.x, a.y - b.y, a.z - b.z) }
func v3Scale(v, k)    { return vec3(v.x * k, v.y * k, v.z * k) }
func v3Neg(v)         { return vec3(-v.x, -v.y, -v.z) }
func v3Dot(a, b)      { return a.x * b.x + a.y * b.y + a.z * b.z }
func v3Cross(a, b) {
    return vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    )
}
func v3LenSq(v)       { return v.x * v.x + v.y * v.y + v.z * v.z }
func v3Len(v)         { return sqrt(v3LenSq(v)) }
func v3Dist(a, b)     { return v3Len(v3Sub(b, a)) }
func v3DistSq(a, b)   { return v3LenSq(v3Sub(b, a)) }
func v3Norm(v) {
    let l = v3Len(v)
    if l == 0 { error("v3Norm: zero vector") }
    return vec3(v.x / l, v.y / l, v.z / l)
}
func v3Lerp(a, b, t) {
    return vec3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    )
}
func v3Reflect(v, normal) {
    let d = 2.0 * v3Dot(v, normal)
    return v3Sub(v, v3Scale(normal, d))
}
func v3Project(v, onto) {
    let dot   = v3Dot(v, onto)
    let lenSq = v3LenSq(onto)
    if lenSq == 0 { error("v3Project: zero vector") }
    return v3Scale(onto, dot / lenSq)
}
func v3Midpoint(a, b) { return v3Scale(v3Add(a, b), 0.5) }
func v3Min(a, b) { return vec3(min(a.x,b.x), min(a.y,b.y), min(a.z,b.z)) }
func v3Max(a, b) { return vec3(max(a.x,b.x), max(a.y,b.y), max(a.z,b.z)) }
func v3AngleBetween(a, b) {
    let dot  = v3Dot(a, b)
    let mags = v3Len(a) * v3Len(b)
    if mags == 0 { return 0 }
    return acos(clamp(dot / mags, -1.0, 1.0))
}
func v3Eq(a, b) { return a.x == b.x and a.y == b.y and a.z == b.z }
func v3Str(v)   { return "Vec3(" + str(v.x) + ", " + str(v.y) + ", " + str(v.z) + ")" }
func v3ToArr(v) { return [v.x, v.y, v.z] }
func v3FromArr(a) { return vec3(a[0], a[1], a[2]) }

/* ── N-dimensional vector ───────────────────────────────── */

func vecN(arr)      { return arr }
func vecNAdd(a, b) {
    let result = []
    let i = 0
    while i < len(a) { push(result, a[i] + b[i]); i += 1 }
    return result
}
func vecNSub(a, b) {
    let result = []
    let i = 0
    while i < len(a) { push(result, a[i] - b[i]); i += 1 }
    return result
}
func vecNScale(v, k) {
    let result = []
    for x in v { push(result, x * k) }
    return result
}
func vecNDot(a, b) {
    let s = 0
    let i = 0
    while i < len(a) { s += a[i] * b[i]; i += 1 }
    return s
}
func vecNLen(v)      { return sqrt(vecNDot(v, v)) }
func vecNDist(a, b)  { return vecNLen(vecNSub(b, a)) }
func vecNNorm(v) {
    let l = vecNLen(v)
    if l == 0 { error("vecNNorm: zero vector") }
    return vecNScale(v, 1.0 / l)
}
func vecNLerp(a, b, t) {
    let result = []
    let i = 0
    while i < len(a) {
        push(result, a[i] + (b[i] - a[i]) * t)
        i += 1
    }
    return result
}
func vecNZeros(n) {
    let result = []
    let i = 0
    while i < n { push(result, 0); i += 1 }
    return result
}
func vecNOnes(n) {
    let result = []
    let i = 0
    while i < n { push(result, 1); i += 1 }
    return result
}

/* ── Quaternion ──────────────────────────────────────────── */

struct Quat { w, x, y, z }

func quat(w, x, y, z)   { return new Quat(w, x, y, z) }
func quatIdentity()      { return quat(1, 0, 0, 0) }

func quatMul(a, b) {
    return quat(
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    )
}

func quatNorm(q) {
    let l = sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z)
    if l == 0 { error("quatNorm: zero quaternion") }
    return quat(q.w/l, q.x/l, q.y/l, q.z/l)
}

func quatConj(q)     { return quat(q.w, -q.x, -q.y, -q.z) }
func quatDot(a, b)   { return a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z }

func quatFromAxisAngle(axis, angle) {
    let norm = v3Norm(axis)
    let ha   = angle / 2.0
    let s    = sin(ha)
    return quat(cos(ha), norm.x * s, norm.y * s, norm.z * s)
}

func quatRotate(q, v) {
    let qv   = quat(0, v.x, v.y, v.z)
    let result = quatMul(quatMul(q, qv), quatConj(q))
    return vec3(result.x, result.y, result.z)
}

func quatSlerp(a, b, t) {
    let dot = quatDot(a, b)
    let bm  = b
    if dot < 0 { dot = -dot; bm = quat(-b.w, -b.x, -b.y, -b.z) }
    if dot > 0.9995 {
        return quatNorm(quat(
            a.w + t*(bm.w - a.w),
            a.x + t*(bm.x - a.x),
            a.y + t*(bm.y - a.y),
            a.z + t*(bm.z - a.z)
        ))
    }
    let theta0 = acos(dot)
    let theta   = theta0 * t
    let s0 = cos(theta) - dot * sin(theta) / sin(theta0)
    let s1 = sin(theta) / sin(theta0)
    return quat(
        s0*a.w + s1*bm.w,
        s0*a.x + s1*bm.x,
        s0*a.y + s1*bm.y,
        s0*a.z + s1*bm.z
    )
}

/* ── Color vectors ───────────────────────────────────────── */

struct Color { r, g, b, a }

func rgb(r, g, b)       { return new Color(r, g, b, 255) }
func rgba(r, g, b, a)   { return new Color(r, g, b, a) }
func colorLerp(a, b, t) {
    return rgba(
        int(a.r + (b.r - a.r) * t),
        int(a.g + (b.g - a.g) * t),
        int(a.b + (b.b - a.b) * t),
        int(a.a + (b.a - a.a) * t)
    )
}
func colorToHex(c) {
    return "#" + _byteHex(c.r) + _byteHex(c.g) + _byteHex(c.b)
}
func _byteHex(n) {
    let hex = "0123456789abcdef"
    let hc  = chars(hex)
    n = clamp(int(n), 0, 255)
    return hc[n // 16] + hc[n % 16]
}
