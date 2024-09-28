
#include <rpp/base.h>
#include <rpp/tuple.h>

#include "encode.h"
#include "immintrin.h"

static __m256 ZERO = _mm256_setzero_ps();
static __m256 P25 = _mm256_set1_ps(0.25f);
static __m256 NEG_P25 = _mm256_set1_ps(-0.25f);
static __m256 P5 = _mm256_set1_ps(0.5f);
static __m256 ONE = _mm256_set1_ps(1.0f);
static __m256 NEG_ONE = _mm256_set1_ps(-1.0f);
static __m256 SIGN_MASK = _mm256_set1_ps(-0.0f);
static __m256 MUL16 = _mm256_set1_ps(32767.0f);
static __m256i GATHER2X = _mm256_set_epi32(0, 2, 4, 6, 8, 10, 12, 14);
static __m256i GATHER2Y = _mm256_set_epi32(1, 3, 5, 7, 9, 11, 13, 15);
static __m256i GATHER3X = _mm256_set_epi32(0, 3, 6, 9, 12, 15, 18, 21);
static __m256i GATHER3Y = _mm256_set_epi32(1, 4, 7, 10, 13, 16, 19, 22);
static __m256i GATHER3Z = _mm256_set_epi32(2, 5, 8, 11, 14, 17, 20, 23);

static __m256 LOOP_MASKS[] = {
    _mm256_set_ps(-0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, -0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, -0.0f, -0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, -0.0f, -0.0f, -0.0f, 0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, 0.0f, 0.0f),
    _mm256_set_ps(-0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f, 0.0f),
};

RPP_FORCE_INLINE static i16 f32_to_snorm16(f32 f) {
    return static_cast<i16>(f * 32767.0f);
}

RPP_FORCE_INLINE static u16 f32_to_f16(f32 f) {
    u32 fu32 = *reinterpret_cast<u32*>(&f);
    u16 fu16 = (fu32 >> 31) << 5;
    u16 tmp = (fu32 >> 23) & 0xff;
    tmp = (tmp - 0x70) & (static_cast<u32>(static_cast<i32>(0x70 - tmp) >> 4) >> 27);
    fu16 = (fu16 | tmp) << 10;
    fu16 |= (fu32 >> 13) & 0x3ff;
    return fu16;
}

RPP_FORCE_INLINE static Vec2 sign_nonzero(Vec2 f) {
    return Vec2{f.x >= 0.0f ? 1.0f : -1.0f, f.y >= 0.0f ? 1.0f : -1.0f};
}

RPP_FORCE_INLINE static f32 diamond_encode(Vec2 p) {
    f32 x = p.x / (Math::abs(p.x) + Math::abs(p.y));
    f32 py_sign = Math::sign(p.y);
    return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

RPP_FORCE_INLINE static __m256 sign_25(__m256 x) {
    __m256 zero_mask = _mm256_cmp_ps(x, ZERO, _CMP_EQ_OQ);
    __m256 sign_mask = _mm256_and_ps(SIGN_MASK, x);
    __m256 sign = _mm256_blendv_ps(P25, NEG_P25, sign_mask);
    return _mm256_blendv_ps(sign, ZERO, zero_mask);
}

RPP_FORCE_INLINE static __m256 sign_nonzero(__m256 x) {
    __m256 sign_mask = _mm256_and_ps(SIGN_MASK, x);
    return _mm256_blendv_ps(ONE, NEG_ONE, sign_mask);
}

RPP_FORCE_INLINE static Pair<__m256, __m256> normal_octahedral(__m256 x, __m256 y, __m256 z) {

    __m256 abs_x = _mm256_andnot_ps(SIGN_MASK, x);
    __m256 abs_y = _mm256_andnot_ps(SIGN_MASK, y);
    __m256 abs_z = _mm256_andnot_ps(SIGN_MASK, z);

    __m256 sum = _mm256_add_ps(abs_x, _mm256_add_ps(abs_y, abs_z));
    __m256 inv_sum = _mm256_rcp_ps(sum);

    __m256 p_x = _mm256_mul_ps(x, inv_sum);
    __m256 p_y = _mm256_mul_ps(y, inv_sum);

    __m256 abs_p_x = _mm256_andnot_ps(SIGN_MASK, p_x);
    __m256 abs_p_y = _mm256_andnot_ps(SIGN_MASK, p_y);

    __m256 p2_x = _mm256_mul_ps(_mm256_sub_ps(ONE, abs_p_y), sign_nonzero(p_x));
    __m256 p2_y = _mm256_mul_ps(_mm256_sub_ps(ONE, abs_p_x), sign_nonzero(p_y));

    __m256 z_mask = _mm256_and_ps(SIGN_MASK, z);

    __m256 r_x = _mm256_blendv_ps(p_x, p2_x, z_mask);
    __m256 r_y = _mm256_blendv_ps(p_y, p2_y, z_mask);

    return Pair{r_x, r_y};
}

RPP_FORCE_INLINE static __m256 dot(__m256 x1, __m256 y1, __m256 z1, __m256 x2, __m256 y2,
                                   __m256 z2) {
    return _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(x1, x2), _mm256_mul_ps(y1, y2)),
                         _mm256_mul_ps(z1, z2));
}

RPP_FORCE_INLINE static __m256 length(__m256 x, __m256 y, __m256 z) {
    return _mm256_sqrt_ps(dot(x, y, z, x, y, z));
}

RPP_FORCE_INLINE static Tuple<__m256, __m256, __m256> cross(__m256 x1, __m256 y1, __m256 z1,
                                                            __m256 x2, __m256 y2, __m256 z2) {
    __m256 x = _mm256_sub_ps(_mm256_mul_ps(y1, z2), _mm256_mul_ps(z1, y2));
    __m256 y = _mm256_sub_ps(_mm256_mul_ps(z1, x2), _mm256_mul_ps(x1, z2));
    __m256 z = _mm256_sub_ps(_mm256_mul_ps(x1, y2), _mm256_mul_ps(y1, x2));
    return Tuple<__m256, __m256, __m256>{x, y, z};
}

RPP_FORCE_INLINE static __m256 diamond_encode(__m256 x, __m256 y) {

    __m256 abs_x = _mm256_andnot_ps(SIGN_MASK, x);
    __m256 abs_y = _mm256_andnot_ps(SIGN_MASK, y);
    __m256 sum = _mm256_add_ps(abs_x, abs_y);
    __m256 inv_sum = _mm256_rcp_ps(sum);

    __m256 p = _mm256_mul_ps(x, inv_sum);
    __m256 sign = sign_25(y);
    __m256 neg_sign = _mm256_xor_ps(sign, SIGN_MASK);

    __m256 l = _mm256_mul_ps(neg_sign, p);
    __m256 r = _mm256_add_ps(P5, sign);
    return _mm256_add_ps(l, r);
}

RPP_FORCE_INLINE static __m256 tangent_diamond(__m256 nx, __m256 ny, __m256 nz, __m256 tx,
                                               __m256 ty, __m256 tz) {

    __m256 neg_nx = _mm256_xor_ps(nx, SIGN_MASK);
    __m256 abs_ny = _mm256_andnot_ps(SIGN_MASK, ny);
    __m256 abs_nz = _mm256_andnot_ps(SIGN_MASK, nz);
    __m256 abs_mask = _mm256_cmp_ps(abs_ny, abs_nz, _CMP_GT_OQ);

    __m256 t1_x = _mm256_blendv_ps(nz, ny, abs_mask);
    __m256 t1_y = _mm256_blendv_ps(ZERO, neg_nx, abs_mask);
    __m256 t1_z = _mm256_blendv_ps(neg_nx, ZERO, abs_mask);

    __m256 t1_norm = _mm256_rcp_ps(length(t1_x, t1_y, t1_z));
    __m256 t1_norm_x = _mm256_mul_ps(t1_x, t1_norm);
    __m256 t1_norm_y = _mm256_mul_ps(t1_y, t1_norm);
    __m256 t1_norm_z = _mm256_mul_ps(t1_z, t1_norm);

    auto [t2_x, t2_y, t2_z] = cross(t1_norm_x, t1_norm_y, t1_norm_z, nx, ny, nz);

    __m256 pack_x = dot(tx, ty, tz, t1_norm_x, t1_norm_y, t1_norm_z);
    __m256 pack_y = dot(tx, ty, tz, t2_x, t2_y, t2_z);

    return diamond_encode(pack_x, pack_y);
}

RPP_FORCE_INLINE static __m256i f32_to_snorm(__m256 x) {
    return _mm256_cvtps_epi32(_mm256_mul_ps(x, MUL16));
}

RPP_FORCE_INLINE static int extract16(__m128i x, u64 i) {
    switch(i) {
    case 0: return _mm_extract_epi16(x, 7);
    case 1: return _mm_extract_epi16(x, 6);
    case 2: return _mm_extract_epi16(x, 5);
    case 3: return _mm_extract_epi16(x, 4);
    case 4: return _mm_extract_epi16(x, 3);
    case 5: return _mm_extract_epi16(x, 2);
    case 6: return _mm_extract_epi16(x, 1);
    case 7: return _mm_extract_epi16(x, 0);
    }
    RPP_UNREACHABLE;
}

RPP_FORCE_INLINE static int extract32(__m256i x, u64 i) {
    switch(i) {
    case 0: return _mm256_extract_epi32(x, 7);
    case 1: return _mm256_extract_epi32(x, 6);
    case 2: return _mm256_extract_epi32(x, 5);
    case 3: return _mm256_extract_epi32(x, 4);
    case 4: return _mm256_extract_epi32(x, 3);
    case 5: return _mm256_extract_epi32(x, 2);
    case 6: return _mm256_extract_epi32(x, 1);
    case 7: return _mm256_extract_epi32(x, 0);
    }
    RPP_UNREACHABLE;
}

RPP_FORCE_INLINE static Pair<__m256, __m256> gather_vec2(__m256 mask, const f32* base) {
    __m256 x = _mm256_mask_i32gather_ps(ZERO, base, GATHER2X, mask, 4);
    __m256 y = _mm256_mask_i32gather_ps(ZERO, base, GATHER2Y, mask, 4);
    return Pair{x, y};
}

RPP_FORCE_INLINE static Tuple<__m256, __m256, __m256> gather_vec3(__m256 mask, const f32* base) {
    __m256 x = _mm256_mask_i32gather_ps(ZERO, base, GATHER3X, mask, 4);
    __m256 y = _mm256_mask_i32gather_ps(ZERO, base, GATHER3Y, mask, 4);
    __m256 z = _mm256_mask_i32gather_ps(ZERO, base, GATHER3Z, mask, 4);
    return Tuple<__m256, __m256, __m256>{x, y, z};
}

RPP_FORCE_INLINE static __m256 loop_mask(u64 i, u64 n) {
    if(i + 8 <= n) return SIGN_MASK;
    int active = static_cast<int>(n - i) - 1;
    return LOOP_MASKS[active];
}

namespace Encode {

u32 uv_half(Vec2 uv) {
    return (static_cast<u32>(f32_to_f16(uv.y)) << 16) | static_cast<u32>(f32_to_f16(uv.x));
}

u32 normal_octahedral(Vec3 n) {
    Vec2 p = n.xy() * (1.0f / (Math::abs(n.x) + Math::abs(n.y) + Math::abs(n.z)));
    Vec2 ret =
        n.z < 0.0f ? Vec2{1.0f - Math::abs(p.y), 1.0f - Math::abs(p.x)} * sign_nonzero(p) : p;
    i16 y = f32_to_snorm16(ret.y);
    i16 x = f32_to_snorm16(ret.x);
    return (reinterpret_cast<u16&>(y) << 16) | reinterpret_cast<u16&>(x);
}

u16 tangent_diamond(Vec3 normal, Vec3 tangent) {
    Vec3 t1;
    if(Math::abs(normal.y) > Math::abs(normal.z)) {
        t1 = Vec3{normal.y, -normal.x, 0.f};
    } else {
        t1 = Vec3{normal.z, 0.f, -normal.x};
    }
    t1.normalize();
    Vec3 t2 = Math::cross(t1, normal);
    Vec2 packed_tangent = Vec2{Math::dot(tangent, t1), Math::dot(tangent, t2)};
    f32 ret = diamond_encode(packed_tangent);
    return f32_to_f16(ret);
}

#ifdef RPP_COMPILER_MSVC
#pragma warning(disable : 4701)
#endif

u64 mesh(u8* out, Slice<const f32> uvs, Slice<const f32> normals, Slice<const f32> tangents) {

    assert(uvs.length() % 2 == 0);
    assert(normals.length() % 3 == 0);
    assert(tangents.length() % 3 == 0);

    bool has_uvs = uvs.length() > 0;
    bool has_normals = normals.length() > 0;
    bool has_tangents = tangents.length() > 0;

    assert(!has_tangents || has_normals);

    if(!(has_uvs || has_normals || has_tangents)) {
        return 0;
    }

    u64 N = 0;
    if(has_uvs) {
        if(has_normals) {
            assert(uvs.length() / 2 == normals.length() / 3);
        }
        if(has_tangents) {
            assert(uvs.length() / 2 == tangents.length() / 3);
        }
        N = uvs.length() / 2;
    }
    if(has_normals) {
        if(has_tangents) {
            assert(normals.length() / 3 == tangents.length() / 3);
        }
        N = normals.length() / 3;
    }

    u64 offset = 0;

    for(u64 i = 0; i < N; i += 8) {

        __m256 mask = loop_mask(i, N);

        __m256i onormal_xi, onormal_yi, dtangent_i;
        if(has_normals) {
            auto [normal_x, normal_y, normal_z] = gather_vec3(mask, &normals[i * 3]);

            auto [onormal_x, onormal_y] = ::normal_octahedral(normal_x, normal_y, normal_z);

            onormal_xi = f32_to_snorm(onormal_x);
            onormal_yi = f32_to_snorm(onormal_y);

            if(has_tangents) {
                auto [tangent_x, tangent_y, tangent_z] = gather_vec3(mask, &tangents[i * 3]);

                __m256 dtangent = ::tangent_diamond(normal_x, normal_y, normal_z, tangent_x,
                                                    tangent_y, tangent_z);

                dtangent_i = f32_to_snorm(dtangent);
            }
        }

        __m128i uv_xi, uv_yi;
        if(has_uvs) {
            auto [uv_x, uv_y] = gather_vec2(mask, &uvs[i * 2]);

            uv_xi = _mm256_cvtps_ph(uv_x, _MM_FROUND_TO_NEAREST_INT);
            uv_yi = _mm256_cvtps_ph(uv_y, _MM_FROUND_TO_NEAREST_INT);
        }

        for(u64 j = 0; j < 8; j++) {
            if(i + j == N) break;
            if(has_normals) {
                int x = extract32(onormal_xi, j);
                int y = extract32(onormal_yi, j);
                *reinterpret_cast<u32*>(&out[offset]) =
                    reinterpret_cast<u16&>(y) << 16 | reinterpret_cast<u16&>(x);
                offset += 4;
            }
            if(has_tangents) {
                int x = extract32(dtangent_i, j);
                *reinterpret_cast<u16*>(&out[offset]) = reinterpret_cast<u16&>(x);
                offset += 2;
            }
            if(has_uvs) {
                int x = extract16(uv_xi, j);
                int y = extract16(uv_yi, j);
                *reinterpret_cast<u32*>(&out[offset]) =
                    reinterpret_cast<u16&>(y) << 16 | reinterpret_cast<u16&>(x);
                offset += 4;
            }
        }
    }

    return offset;
}

void rg8_to_rgba8(u8* out, Slice<const u8> in, u32 w, u32 h) {

    // TODO simd

    u64 N = w * h;
    for(u64 i = 0; i < N; i++) {
        out[i * 4 + 0] = in[i * 2 + 0];
        out[i * 4 + 1] = in[i * 2 + 1];
        out[i * 4 + 2] = 0;
        out[i * 4 + 3] = 255;
    }
}

void rgb8_to_rgba8(u8* out, Slice<const u8> in, u32 w, u32 h) {

    // TODO simd

    u64 N = w * h;
    for(u64 i = 0; i < N; i++) {
        out[i * 4 + 0] = in[i * 3 + 0];
        out[i * 4 + 1] = in[i * 3 + 1];
        out[i * 4 + 2] = in[i * 3 + 2];
        out[i * 4 + 3] = 255;
    }
}

void rg32f_to_rgba32f(u8* out, Slice<const f32> in, u32 w, u32 h) {

    // TODO simd

    u64 N = w * h;
    f32* outf = reinterpret_cast<f32*>(out);

    for(u64 i = 0; i < N; i++) {
        outf[i * 4 + 0] = in[i * 2 + 0];
        outf[i * 4 + 1] = in[i * 2 + 1];
        outf[i * 4 + 2] = 0.0f;
        outf[i * 4 + 3] = 1.0f;
    }
}

void rgb32f_to_rgba32f(u8* out, Slice<const f32> in, u32 w, u32 h) {

    // TODO simd

    u64 N = w * h;
    f32* outf = reinterpret_cast<f32*>(out);

    for(u64 i = 0; i < N; i++) {
        outf[i * 4 + 0] = in[i * 3 + 0];
        outf[i * 4 + 1] = in[i * 3 + 1];
        outf[i * 4 + 2] = in[i * 3 + 2];
        outf[i * 4 + 3] = 1.0f;
    }
}

} // namespace Encode