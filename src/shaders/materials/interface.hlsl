
#ifndef _MATERIALS_INTERFACE_HLSL
#define _MATERIALS_INTERFACE_HLSL

#define MAT_NONE 0
#define MAT_GLTF 1
#define MAT_PBRT_CONDUCTOR 2
#define MAT_PBRT_DIELECTRIC 3
#define MAT_PBRT_DIFFUSE 4
#define MAT_PBRT_DIFFUSE_TRANSMISSION 5
#define MAT_PBRT_MIX 6
#define MAT_PBRT_HAIR 7
#define MAT_PBRT_SUBSURFACE 8
#define MAT_PBRT_THIN_DIELECTRIC 9
#define MAT_PBRT_INTERFACE 10
#define MAT_PBRT_MEASURED 11
#define MAT_TUNGSTEN_ROUGH_PLASTIC 12
#define MAT_TUNGSTEN_SMOOTH_COAT 13

#define TEXTURE_TYPE_SHIFT 30
#define TEXTURE_TYPE_MASK (0x3 << TEXTURE_TYPE_SHIFT)

#define TEXTURE_SAMPLER_SHIFT 22
#define TEXTURE_SAMPLER_MASK (0xff << TEXTURE_SAMPLER_SHIFT)

#define TEXTURE_IMAGE_SHIFT 0
#define TEXTURE_IMAGE_MASK (0x3fffff << TEXTURE_IMAGE_SHIFT)

#define TEXTURE_TYPE_NONE 0x0
#define TEXTURE_TYPE_IMAGE 0x1
#define TEXTURE_TYPE_CONST 0x2
#define TEXTURE_TYPE_PROC 0x3

struct GPU_Material {
    u32 texture_ids[12];
    f32rgba spectra[12];
    u32 parameters[4];
};

struct Shade_Info {
    f32dir T, N, B;
    f32rgb emission;
    f32dir w_o;
    f32v2 uv;

    f32dir local(in f32dir w) {
        return f32dir(dot(w, T), dot(w, N), dot(w, B));
    }

    f32dir global(in f32dir w) {
        return T * w.x + N * w.y + B * w.z;
    }
};

typedef bool Layer;

struct Delta_Sample {
    f32dir w_i;
    f32rgb atten;
};

namespace Scatter {

static f32dir reflect(in f32dir w_o, in f32dir N) {
    return -w_o + 2.0f * dot(w_o, N) * N;
}

static bool refract(in f32dir w_i, in f32dir N, in f32 eta, inout f32 etap, out f32dir w_t) {
    f32 cos_i = dot(N, w_i);
    if (cos_i < 0.0f) {
        eta = 1.0f / eta;
        cos_i = -cos_i;
        N = -N;
    }
    f32 sin2_i = max(0.0f, 1.0f - sqr(cos_i));
    f32 sin2_t = sin2_i / sqr(eta);
    if (sin2_t >= 1.0f) return false;

    f32 cos_t = sqrt(1.0f - sin2_t);
    w_t = -w_i / eta + (cos_i / eta - cos_t) * N;
    etap = eta;
    return true;
}

static f32 fresnel_reflectance(in f32 cos_i, in f32 eta, inout f32 cos_t) {
    cos_i = clamp(cos_i, -1.0f, 1.0f);
    if (cos_i < 0.0f) {
        eta = 1 / eta;
        cos_i = -cos_i;
    }

    f32 sin2_i = 1.0f - sqr(cos_i);
    f32 sin2_t = sin2_i / sqr(eta);
    if (sin2_t >= 1.0f) return 1.0f;

    cos_t = sqrt(1.0f - sin2_t);

    f32 r_parl = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
    f32 r_perp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
    return (sqr(r_parl) + sqr(r_perp)) / 2.0f;
}

static f32 fresnel_reflectance(in f32 cos_i, in f32 eta) {
    f32 cos_t;
    return fresnel_reflectance(cos_i, eta, cos_t);
}

static f32 fresnel_reflectance_complex(in f32 cos_i, in f32c eta) {
    f32c cos_ic = f32c(cos_i, 0.0f);
    f32c sin2_ic = f32c(1.0f - sqr(cos_i), 0.0f);

    f32c sin2_tc = complex_div(sin2_ic, complex_sqr(eta));
    f32c cos_tc = complex_sqrt(f32c(1.0f, 0.0f) - sin2_tc);

    f32c r_parl = complex_div(complex_mul(eta, cos_ic) - cos_tc, complex_mul(eta, cos_ic) + cos_tc);
    f32c r_perp = complex_div(cos_ic - complex_mul(eta, cos_tc), cos_ic + complex_mul(eta, cos_tc));
    return (complex_norm2(r_parl) + complex_norm2(r_perp)) / 2.0f;
}

static f32rgb fresnel_reflectance_complex(in f32 cos_i, in f32rgb eta, in f32rgb k) {
    f32rgb ret;
    ret.r = fresnel_reflectance_complex(cos_i, f32c(eta.r, k.r));
    ret.g = fresnel_reflectance_complex(cos_i, f32c(eta.g, k.g));
    ret.b = fresnel_reflectance_complex(cos_i, f32c(eta.b, k.b));
    return ret;
}

struct Towbridge_Reitz {
    f32 alpha_x, alpha_z;

    // Directions in local space

    static f32 remap(f32 roughness) {
        return sqrt(roughness);
    }

    f32 lambda(in f32dir w) {
        f32 cos2 = w.y * w.y;
        f32 sin2 = 1.0f - cos2;
        f32 tan2 = sin2 / cos2;
        if(isinf(tan2)) return 0.0f;

        f32 sin_ = sqrt(sin2);
        f32 cos_p = sin_ == 0.0f ? 1.0f : clamp(w.x / sin_, -1.0f, 1.0f);
        f32 sin_p = sin_ == 0.0f ? 0.0f : clamp(w.z / sin_, -1.0f, 1.0f);

        f32 alpha2 = sqr(cos_p * alpha_x) + sqr(sin_p * alpha_z);
        return (sqrt(1.0f + alpha2 * tan2) - 1.0f) / 2.0f;
    }

    f32 D(in f32dir H) {
        f32 cos2 = H.y * H.y;
        f32 sin2 = 1.0f - cos2;
        f32 tan2 = sin2 / cos2;
        if(isinf(tan2)) return 0.0f;

        f32 cos4 = cos2 * cos2;
        if (cos4 < 1e-16f) return 0.0f;

        f32 sin_ = sqrt(sin2);
        f32 cos_p = sin_ == 0.0f ? 1.0f : clamp(H.x / sin_, -1.0f, 1.0f);
        f32 sin_p = sin_ == 0.0f ? 0.0f : clamp(H.z / sin_, -1.0f, 1.0f);

        f32 e = tan2 * (sqr(cos_p / alpha_x) + sqr(sin_p / alpha_z));
        return 1.0f / (PI * alpha_x * alpha_z * cos4 * sqr(1.0f + e));
    }

    f32 G1(in f32dir w) {
        return 1.0f / (1.0f + lambda(w));
    }

    f32 G(in f32dir w_i, in f32dir w_o) {
        return 1.0f / (1.0f + lambda(w_i) + lambda(w_o));
    }

    f32 D(in f32dir w, in f32dir H) {
        return G1(w) / abs(w.y) * D(H) * abs(dot(w, H));
    }

    f32 pdf(in f32dir w, in f32dir H) {
        return D(w, H);
    }

    f32dir sample_H(in f32dir w, inout u32 seed) {
        f32dir wh = normalize(f32dir(alpha_x * w.x, w.y, alpha_z * w.z));
        if(wh.y < 0.0f) wh = -wh;

        f32dir T1 = make_tangent(wh);
        f32dir T2 = cross(wh, T1);

        f32v2 p = sample_disk(seed);

        f32 h = sqrt(1.0f - sqr(p.x));
        p.y = lerp(h, p.y, (1.0f + wh.y) / 2.0f);

        f32 pz = sqrt(max(0.0f, 1.0f - dot(p, p)));
        f32dir nh = p.x * T1 + p.y * T2 + pz * wh;

        return normalize(f32dir(alpha_x * nh.x, max(1e-6f, nh.y), alpha_z * nh.z));
    }
};

struct Henyey_Greenstein {
    struct Sample {
        f32 p;
        f32dir wi;
        f32 pdf;
    };

    f32 g;

    f32 eval(in f32 cos_i) {
        f32 denom = 1.0f + sqr(g) + 2.0f * g * cos_i;
        return (1.0f / (4.0f * PI)) * (1.0f - sqr(g)) / (denom * sqrt(denom));
    }

    f32 p(in f32dir wo, in f32dir wi) {
        return eval(dot(wo, wi));
    }

    Sample distribution(in f32dir w, inout u32 seed) {
        f32 xi0 = randf(seed);
        f32 xi1 = randf(seed);

        f32 cos_t;
        if(abs(g) < 1e-3f) cos_t = 1.0f - 2.0f * xi0;
        else cos_t = -1.0f / (2.0f * g) * (1.0f + sqr(g) - sqr((1.0f - sqr(g)) / (1.0f + g - 2.0f * g * xi0)));

        f32 sin_t = sqrt(1.0f - sqr(cos_t));
        f32 phi = 2.0f * PI * xi1;

        f32dir T0 = make_tangent(w);
        f32dir T1 = cross(w, T0);
        f32dir wi = f32dir(cos(phi) * sin_t, cos_t, sin(phi) * sin_t);

        Sample ret;
        ret.pdf = eval(cos_t);
        ret.wi = T0 * wi.x + w * wi.y + T1 * wi.z;
        ret.p = ret.pdf;
        return ret;
    }

    f32 pdf(in f32dir wo, in f32dir wi) {
        return p(wo, wi);
    }
};

}

#endif // _MATERIALS_INTERFACE_HLSL
