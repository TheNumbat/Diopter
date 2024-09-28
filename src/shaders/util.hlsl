
#ifndef _UTIL_HLSL
#define _UTIL_HLSL

#define f16     float16_t
#define f16v2   float16_t2
#define f16v3   float16_t3
#define f16v4   float16_t4
#define f16m2x2 float16_t2x2
#define f16m3x3 float16_t3x3
#define f16m3x4 float16_t3x4
#define f16m4x3 float16_t4x3
#define f16m4x4 float16_t4x4
#define f32     float32_t
#define f32v2   float32_t2
#define f32v3   float32_t3
#define f32v4   float32_t4
#define f32m2x2 float32_t2x2
#define f32m3x3 float32_t3x3
#define f32m3x4 float32_t3x4
#define f32m4x3 float32_t4x3
#define f32m4x4 float32_t4x4
#define u16     uint16_t
#define u16v2   uint16_t2
#define u16v3   uint16_t3
#define u16v4   uint16_t4
#define i16     int16_t
#define i16v2   int16_t2
#define i16v3   int16_t3
#define i16v4   int16_t4
#define u32     uint32_t
#define u32v2   uint32_t2
#define u32v3   uint32_t3
#define u32v4   uint32_t4
#define i32     int32_t
#define i32v2   int32_t2
#define i32v3   int32_t3
#define i32v4   int32_t4
#define u64     uint64_t
#define u64v2   uint64_t2
#define u64v3   uint64_t3
#define u64v4   uint64_t4
#define i64     int64_t
#define i64v2   int64_t2
#define i64v3   int64_t3
#define i64v4   int64_t4

#define uptr     u64
#define f32rgb   f32v3
#define f32rgba  f32v4
#define f32point f32v3
#define f32dir   f32v3
#define f32c     f32v2

#define PI          3.141592535f
#define LARGE_DIST  10000000.0f
#define EPSILON     0.00001f
#define INVALID_POS f32point(LARGE_DIST, LARGE_DIST, LARGE_DIST)

#define INT16_MAX  0x7FFF
#define INT16_MIN  0x8000
#define UINT16_MAX 0xFFFF
#define UINT16_MIN 0x0000
#define INT32_MAX  0x7FFFFFFF
#define INT32_MIN  0x80000000
#define UINT32_MAX 0xFFFFFFFF
#define UINT32_MIN 0x00000000
#define INT64_MAX  0x7FFFFFFFFFFFFFFF
#define INT64_MIN  0x8000000000000000
#define UINT64_MAX 0xFFFFFFFFFFFFFFFF
#define UINT64_MIN 0x0000000000000000

f32 sign_nonzero(in f32 v) {
    return v >= 0.0f ? 1.0f : -1.0f;
}

f32v2 sign_nonzero(in f32v2 v) {
    return f32v2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0 ? 1.0f : -1.0f);
}

f32 max3(in f32v3 v) {
    return max(max(v.x, v.y), v.z);
}

f32 avg3(in f32v3 v) {
    return (v.x + v.y + v.z) / 3.0f;
}

f32 sqr(in f32 x) {
    return x * x;
}

f32 copy_sign(in f32 from, in f32 to) {
    return abs(to) * sign_nonzero(from);
}

f32 power_heuristic(in u32 nf, in f32 fPdf, in u32 ng, in f32 gPdf) {
    f32 f = nf * fPdf, g = ng * gPdf;
    if(isinf(sqr(f))) return 1.0f;
    return sqr(f) / (sqr(f) + sqr(g));
}

f32v3 face_forward(in f32dir dir, in f32dir N) {
    return dot(dir, N) < 0.0f ? -dir : dir;
}

f32 transmittance(in f32 dy, in f32dir w) {
    return exp(-abs(dy / w.y));
}

bool same_hemisphere(in f32dir wo, in f32dir wi) {
    return wo.y * wi.y > 0.0f;
}

f32c complex_mul(in f32c x, in f32c y) {
    return f32c(x.x * y.x - x.y * y.y, x.x * y.y + x.y * y.x);
}

f32c complex_div(in f32c x, in f32c y) {
    f32 d = 1.0f / dot(y, y);
    if(isnan(d)) return 0.0f;
    return f32c((x.x * y.x + x.y * y.y) * d, (x.y * y.x - x.x * y.y) * d);
}

f32c complex_sqrt(in f32c x) {
    f32 z = sqrt(x.x * x.x + x.y * x.y);
    return f32c(sqrt(0.5f * (z + x.x)), sign(x.y) * sqrt(0.5f * (z - x.x)));
}

f32 complex_norm2(in f32c x) {
    return x.x * x.x + x.y * x.y;
}

f32c complex_sqr(in f32c x) {
    return complex_mul(x, x);
}

// RNG //////////////////////////////////////////

u32 tea(in u32 val0, in u32 val1) {
    u32 v0 = val0;
    u32 v1 = val1;
    u32 s0 = 0;
    for(u32 n = 0; n < 16; n++) {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

u32 lcg(inout u32 prev) {
    u32 LCG_A = 1664525u;
    u32 LCG_C = 1013904223u;
    prev = (LCG_A * prev + LCG_C);
    return prev & 0x00FFFFFF;
}

f32 randf(inout u32 prev) {
    return (f32(lcg(prev)) / f32(0x01000000));
}

u32 randu(inout u32 prev, u32 a, u32 b) {
    return lcg(prev) % (b - a) + a;
}

f32 rand_exponential(in f32 a, inout u32 seed) {
    return -log(1.0f - randf(seed)) / a;
}

// Colors ////////////////////////////////////////////

f32 luminance(in f32rgb color) {
    return dot(color, f32rgb(0.299f, 0.587f, 0.114f));
}

f32rgb temperature(in f32 t) {
    static const f32rgb c[10] = {
        f32rgb(   0.0f/255.0f,   2.0f/255.0f,  91.0f/255.0f ),
        f32rgb(   0.0f/255.0f, 108.0f/255.0f, 251.0f/255.0f ),
        f32rgb(   0.0f/255.0f, 221.0f/255.0f, 221.0f/255.0f ),
        f32rgb(  51.0f/255.0f, 221.0f/255.0f,   0.0f/255.0f ),
        f32rgb( 255.0f/255.0f, 252.0f/255.0f,   0.0f/255.0f ),
        f32rgb( 255.0f/255.0f, 180.0f/255.0f,   0.0f/255.0f ),
        f32rgb( 255.0f/255.0f, 104.0f/255.0f,   0.0f/255.0f ),
        f32rgb( 226.0f/255.0f,  22.0f/255.0f,   0.0f/255.0f ),
        f32rgb( 191.0f/255.0f,   0.0f/255.0f,  83.0f/255.0f ),
        f32rgb( 145.0f/255.0f,   0.0f/255.0f,  65.0f/255.0f ),
    };
    f32 s = t * 10.0f;
    i32 cur = i32(s) <= 9 ? i32(s) : 9;
    i32 prv = cur >= 1 ? cur-1 : 0;
    i32 nxt = cur < 9 ? cur+1 : 9;
    f32 blur = 0.8f;
    f32 wc = smoothstep(f32(cur) - blur, f32(cur) + blur, s) * (1.0f - smoothstep(f32(cur + 1) - blur, f32(cur + 1) + blur, s));
    f32 wp = 1.0f - smoothstep(f32(cur) - blur, f32(cur) + blur, s);
    f32 wn = smoothstep(f32(cur + 1) - blur, f32(cur + 1) + blur, s);
    f32rgb r = wc*c[cur] + wp*c[prv] + wn*c[nxt];
    return clamp(r, 0.0f, 1.0f);
}

f32rgb linear_to_srgb(in f32rgb lin, in f32 gamma) {
    bool3 cutoff = lin < 0.0031308f;
    f32v3 higher = 1.055f * pow(lin, 1.0f / gamma) - 0.055f;
    f32v3 lower = lin * 12.92f;
    return lerp(higher, lower, cutoff);
}

f32rgb srgb_to_linear(in f32rgb srgb, in f32 gamma) {
    bool3 cutoff = srgb < 0.04045f;
    f32v3 higher = pow((srgb + 0.055f) / 1.055f, gamma);
    f32v3 lower = srgb / 12.92f;
    return lerp(higher, lower, cutoff);
}

// Intersections /////////////////////////////////////

struct Ray {
    f32point o;
    f32dir d;
};

struct AABB {
    f32point min;
    f32point max;
};

f32 hitAABB(in AABB aabb, in Ray r) {
    f32v3 invDir = 1.0 / r.d;
    f32v3 tbot = invDir * (aabb.min - r.o);
    f32v3 ttop = invDir * (aabb.max - r.o);
    f32v3 tmin = min(ttop, tbot);
    f32v3 tmax = max(ttop, tbot);
    f32 t0 = max(tmin.x, max(tmin.y, tmin.z));
    f32 t1 = min(tmax.x, min(tmax.y, tmax.z));
    return t1 > max(t0, 0.0) ? t0 : -1.0;
}

// Sampling //////////////////////////////////////////

f32dir sample_hemisphere_cosine(inout u32 seed) {
    f32 phi = randf(seed);
    f32 cosT2 = randf(seed);
    f32 sinT = sqrt(1.0 - cosT2);
    f32dir direction = f32dir(cos(2 * PI * phi) * sinT, sqrt(cosT2), sin(2 * PI * phi) * sinT);
    return direction;
}

f32dir sample_hemisphere_cosine_tnb(inout u32 seed, in f32dir t, in f32dir n, in f32dir b) {
	f32dir direction = sample_hemisphere_cosine(seed);
	direction = direction.x * t + direction.y * n + direction.z * b;
	return direction;
}

f32v2 sample_disk(inout u32 seed) {
    f32 r = sqrt(randf(seed));
    f32 theta = 2.0f * PI * randf(seed);
    return f32v2(r * cos(theta), r * sin(theta));
}

f32v2 sphere_equal_area(in f32dir d) {
    f32v3 abs_d = abs(d);

    f32 r = sqrt(1.0f - abs_d.y);
    f32 a = max(abs_d.x, abs_d.z);
    f32 b = min(abs_d.x, abs_d.z);

    b = a == 0.0f ? 0.0f : b / a;

    f32 phi = 2.0f * atan(b) / PI;

    if(abs_d.x < abs_d.z) phi = 1.0f - phi;

    f32 v = phi * r;
    f32 u = r - v;

    if(d.y < 0.0f) {
        f32 swp = u;
        u = v;
        v = swp;
        u = 1.0f - u;
        v = 1.0f - v;
    }

    u *= sign_nonzero(d.x);
    v *= sign_nonzero(d.z);

    return f32v2(0.5f * (u + 1.0f), 0.5f * (v + 1.0f));
}

// Geometry //////////////////////////////////////////

f32dir make_tangent(in f32dir n) {
    f32dir t1;
    if(abs(n.y) > abs(n.z)) {
        t1 = f32dir(n.y, -n.x, 0.f);
    } else {
        t1 = f32dir(n.z, 0.f, -n.x);
    }
    t1 = normalize(t1);
    return t1;
}

f32 encode_diamond(in f32v2 p) {
    f32 x = p.x / (abs(p.x) + abs(p.y));
    f32 py_sign = sign(p.y);
    return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

f32v2 decode_diamond(in f32 p) {
    f32v2 v;
    f32 p_sign = sign(p - 0.5f);
    v.x = -p_sign * 4.f * p + 1.f + p_sign * 2.f;
    v.y = p_sign * (1.f - abs(v.x));
    return normalize(v);
}

f32v2 encode_octahedral_normal(in f32dir n) {
    f32v2 p = n.xy * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
    return n.z < 0.0f ? (1.0f - abs(p.yx)) * sign_nonzero(p) : p;
}

f32dir decode_octahedral_normal(in f32v2 o) {
    f32v3 v = f32v3(o.xy, 1.0f - abs(o.x) - abs(o.y));
    if(v.z < 0.0f) v.xy = (1.0f - abs(v.yx)) * sign_nonzero(v.xy);
    return normalize(v);
}

f32 encode_diamond_tangent(in f32dir normal, in f32dir tangent) {
    f32v3 t1;
    if(abs(normal.y) > abs(normal.z)) {
        t1 = f32v3(normal.y, -normal.x, 0.f);
    } else {
        t1 = f32v3(normal.z, 0.f, -normal.x);
    }
    t1 = normalize(t1);
    f32v3 t2 = cross(t1, normal);
    f32v2 packed_tangent = f32v2(dot(tangent, t1), dot(tangent, t2));
    return encode_diamond(packed_tangent);
}

f32dir decode_diamond_tangent(in f32dir normal, in f32 diamond_tangent) {
    f32v3 t1;
    if (abs(normal.y) > abs(normal.z)) {
        t1 = f32v3(normal.y, -normal.x, 0.f);
    } else {
        t1 = f32v3(normal.z, 0.f, -normal.x);
    }
    t1 = normalize(t1);
    f32v3 t2 = cross(t1, normal);
    f32v2 packed_tangent = decode_diamond(diamond_tangent);
    return packed_tangent.x * t1 + packed_tangent.y * t2;
}

template<i32 N>
vector<f32,N> decode_snorm16(in vector<int16_t,N> n) {
    return vector<f32,N>(n) / 32767.0;
}

template<i32 N>
vector<i16,N> encode_snorm16(in vector<f32,N> n) {
    return vector<i16,N>(n * 32767.0);
}

f32 decode_snorm16(in i16 n) {
    return f32(n) / 32767.0;
}

i16 encode_snorm16(in f32 n) {
    return i16(n * 32767.0);
}

#endif // _UTIL_HLSL