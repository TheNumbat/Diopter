
#ifndef _MATERIALS_PBRT_CONDUCTOR_HLSL
#define _MATERIALS_PBRT_CONDUCTOR_HLSL

#include "interface.hlsl"

namespace Material {

struct PBRT_Conductor {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]);
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]);
        Scene::resolve_texture(parameters.texture_ids[3], shade.uv, parameters.spectra[3]);
        Scene::resolve_texture(parameters.texture_ids[4], shade.uv, parameters.spectra[4]);
        if(parameters.parameters[1] == 0) { // Determine eta, k via reflectance
            f32rgb r = clamp(parameters.spectra[3].rgb, 0.0f, 0.999f);
            parameters.spectra[3].rgb = 1.0f;
            parameters.spectra[4].rgb = 2.0f * sqrt(r) / sqrt(1.0f - r);
        }
    }

    static bool is_specular(in GPU_Material parameters) {
        f32 roughness = max(parameters.spectra[0].r, max(parameters.spectra[1].r, parameters.spectra[2].r));
        return roughness < 0.0001f;
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

        f32rgb eta = parameters.spectra[3].rgb;
        f32rgb k = parameters.spectra[4].rgb;

        delta.w_i = Scatter::reflect(shade.w_o, shade.N);
        delta.atten = Scatter::fresnel_reflectance_complex(abs(dot(delta.w_i, shade.N)), eta, k);

        return true;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return false;

        f32dir wo = shade.local(shade.w_o);
        if(wo.y == 0.0f) return false;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);
        f32dir wm = distribution.sample_H(wo, seed);
        f32dir wi = Scatter::reflect(wo, wm);

        if(wo.y * wi.y < 0.0f) return false;
        w_i = shade.global(wi);

        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return 0.0f;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);

        f32rgb eta = parameters.spectra[3].rgb;
        f32rgb k = parameters.spectra[4].rgb;

        f32dir wo = shade.local(shade.w_o);
        f32dir wi = shade.local(w_i);

        f32 cos_o = wo.y;
        f32 cos_i = wi.y;

        if(cos_i == 0.0f || cos_o == 0.0f) return 0.0f;

        f32dir wm = wi + wo;
        if(dot(wm, wm) == 0.0f) return 0.0f;
        wm = normalize(wm);

        f32rgb F = Scatter::fresnel_reflectance_complex(abs(dot(wo, wm)), eta, k);
        return F * distribution.D(wm) * distribution.G(wo, wi) / (4.0f * cos_o);
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return 0.0f;

        Scatter::Towbridge_Reitz distribution = get_distribution(parameters);

        f32dir wo = shade.local(shade.w_o);
        f32dir wi = shade.local(w_i);
        if(wo.y * wi.y <= 0.0f) return 0.0f;

        f32dir wm = wo + wi;
        if(dot(wm, wm) == 0.0f) return 0.0f;
        wm = face_forward(normalize(wm), f32dir(0.0f, 1.0f, 0.0f));

        return distribution.pdf(wo, wm) / (4.0f * dot(wo, wm));
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return abs(dot(shade.w_o, shade.N)) * shade.emission;
    }
};

}

#endif // _MATERIALS_PBRT_CONDUCTOR_HLSL
