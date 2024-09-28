
#ifndef _MATERIALS_PBRT_DIFFUSE_TRANSMISSION_HLSL
#define _MATERIALS_PBRT_DIFFUSE_TRANSMISSION_HLSL

#include "interface.hlsl"

namespace Material {

struct PBRT_Diffuse_Transmission {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]);
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]);
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        return false;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {

        f32rgb R = parameters.spectra[0].rgb * parameters.spectra[2].r;
        f32rgb T = parameters.spectra[1].rgb * parameters.spectra[2].r;

        f32dir wi = sample_hemisphere_cosine(seed);

        f32 pR = max(R.r, max(R.g, R.b));
        f32 pT = max(T.r, max(T.g, T.b));

        if(randf(seed) < pR / (pR + pT)) {
            if(dot(shade.w_o, shade.N) < 0.0f) wi.y = -wi.y;
        } else {
            if(dot(shade.w_o, shade.N) > 0.0f) wi.y = -wi.y;
        }

        w_i = shade.global(wi);
        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32rgb R = parameters.spectra[0].rgb * parameters.spectra[2].r;
        f32rgb T = parameters.spectra[1].rgb * parameters.spectra[2].r;

        f32 cos_o = dot(shade.N, shade.w_o);
        f32 cos_i = dot(shade.N, w_i);

        if(cos_o * cos_i > 0.0f) return R * cos_i / PI;
        else return T * cos_i / PI;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32rgb R = parameters.spectra[0].rgb * parameters.spectra[2].r;
        f32rgb T = parameters.spectra[1].rgb * parameters.spectra[2].r;

        f32 pR = max(R.r, max(R.g, R.b));
        f32 pT = max(T.r, max(T.g, T.b));

        f32 cos_o = dot(shade.N, shade.w_o);
        f32 cos_i = dot(shade.N, w_i);

        if(cos_o * cos_i > 0.0f) return (pR / (pR + pT)) * cos_i / PI;
        else return (pT / (pR + pT)) * cos_i / PI;
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return abs(dot(shade.w_o, shade.N)) * shade.emission;
    }
};

}

#endif // _MATERIALS_PBRT_DIFFUSE_TRANSMISSION_HLSL
