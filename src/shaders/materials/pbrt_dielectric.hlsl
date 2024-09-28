
#ifndef _MATERIALS_PBRT_DIELECTRIC_HLSL
#define _MATERIALS_PBRT_DIELECTRIC_HLSL

#include "interface.hlsl"

namespace Material {

struct PBRT_Dielectric {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]);
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]);
        Scene::resolve_texture(parameters.texture_ids[3], shade.uv, parameters.spectra[3]);
    }

    static bool is_specular(in GPU_Material parameters) {
        f32 eta = parameters.spectra[3].r;
        f32 roughness = max(parameters.spectra[0].r, max(parameters.spectra[1].r, parameters.spectra[2].r));
        return eta == 1.0f || roughness < 0.0001f;
    }

    static Scatter::Towbridge_Reitz get_distribution(in GPU_Material parameters) {
        Scatter::Towbridge_Reitz distribution;
        bool remap = parameters.parameters[0] == 1;
        distribution.alpha_x = parameters.spectra[1].r;
        distribution.alpha_z = parameters.spectra[2].r;
        if(parameters.spectra[0].r > 0.0f) {
            distribution.alpha_x = distribution.alpha_z = parameters.spectra[0].r;
        }
        if(remap) {
            distribution.alpha_x = Scatter::Towbridge_Reitz::remap(distribution.alpha_x);
            distribution.alpha_z = Scatter::Towbridge_Reitz::remap(distribution.alpha_z);
        }
        return distribution;
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        if(!is_specular(parameters)) return false;

        f32 eta = parameters.spectra[3].r;
        f32 R = Scatter::fresnel_reflectance(dot(shade.w_o, shade.N), eta);

        if(randf(seed) < R) {
            delta.w_i = Scatter::reflect(shade.w_o, shade.N);
            delta.atten = 1.0f;
        } else {
            f32 etap;
            f32dir w_t;
            if(Scatter::refract(shade.w_o, shade.N, eta, etap, w_t)) {
                delta.w_i = w_t;
                delta.atten = 1.0f / sqr(etap);
            } else {
                delta.w_i = INVALID_POS;
                delta.atten = 1.0f;
            }
        }
        return true;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return false;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);

        f32dir wo = shade.local(shade.w_o);
        f32dir wm = distribution.sample_H(wo, seed);

        f32 eta = parameters.spectra[3].r;
        f32 R = Scatter::fresnel_reflectance(dot(wo, wm), eta);

        if(randf(seed) < R) {
            f32dir wi = Scatter::reflect(wo, wm);
            if(wo.y * wi.y < 0.0f) return false;
            w_i = shade.global(wi);
        } else {
            f32 etap;
            f32dir wi;
            bool tir = !Scatter::refract(wo, wm, eta, etap, wi);
            if(wo.y * wi.y > 0.0f || wi.y == 0.0f || tir) return false;
            w_i = shade.global(wi);
        }

        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return 0.0f;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);

        f32 eta = parameters.spectra[3].r;
        f32dir wo = shade.local(shade.w_o);
        f32dir wi = shade.local(w_i);

        f32 cos_o = wo.y;
        f32 cos_i = wi.y;
        bool reflect = cos_o * cos_i > 0.0f;

        f32 etap = 1.0f;
        if(!reflect) etap = cos_o > 0.0f ? eta : 1.0f / eta;

        f32dir wm = wi * etap + wo;
        if(cos_i == 0.0f || cos_o == 0.0f || dot(wm, wm) == 0.0f) return 0.0f;

        wm = face_forward(normalize(wm), f32dir(0.0f, 1.0f, 0.0f));

        if(dot(wm, wi) * cos_i < 0.0f || dot(wm, wo) * cos_o < 0.0f) return 0.0f;

        f32 R = Scatter::fresnel_reflectance(dot(wo, wm), eta);
        if(reflect) {
            return R * distribution.D(wm) * distribution.G(wo, wi) / abs(4.0f * cos_o);
        } else {
            f32 denom = sqr(dot(wi, wm) + dot(wo, wm) / etap) * cos_i * cos_o;
            f32 ft = (1.0f - R) * distribution.D(wm) * distribution.G(wo, wi) * abs(dot(wi, wm) * dot(wo, wm) / denom);
            return ft * abs(cos_i) / sqr(etap);
        }
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return 0.0f;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);

        f32 eta = parameters.spectra[3].r;
        f32dir wo = shade.local(shade.w_o);
        f32dir wi = shade.local(w_i);

        f32 cos_o = wo.y;
        f32 cos_i = wi.y;
        bool reflect = cos_o * cos_i > 0.0f;

        f32 etap = 1.0f;
        if(!reflect) etap = cos_o > 0.0f ? eta : 1.0f / eta;

        f32dir wm = wi * etap + wo;
        if(cos_i == 0.0f || cos_o == 0.0f || dot(wm, wm) == 0.0f) return 0.0f;

        wm = face_forward(normalize(wm), f32dir(0.0f, 1.0f, 0.0f));

        if(dot(wm, wi) * cos_i < 0.0f || dot(wm, wo) * cos_o < 0.0f) return 0.0f;

        f32 R = Scatter::fresnel_reflectance(dot(wo, wm), eta);
        if(reflect) {
            return R * distribution.pdf(wo, wm) / (4.0f * abs(dot(wo, wm)));
        } else {
            f32 denom = sqr(dot(wi, wm) + dot(wo, wm) / etap);
            f32 dwm_dwi = abs(dot(wi, wm)) / denom;
            return (1.0f - R) * distribution.pdf(wo, wm) * dwm_dwi;
        }
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return abs(dot(shade.w_o, shade.N)) * shade.emission;
    }
};

}

#endif // _MATERIALS_PBRT_DIELECTRIC_HLSL
