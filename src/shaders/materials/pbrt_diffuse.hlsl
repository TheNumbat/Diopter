
#ifndef _MATERIALS_PBRT_DIFFUSE_HLSL
#define _MATERIALS_PBRT_DIFFUSE_HLSL

#include "interface.hlsl"

namespace Material {

struct PBRT_Diffuse {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        return false;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        w_i = sample_hemisphere_cosine_tnb(seed, shade.T, shade.N, shade.B);
        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return max(dot(w_i, shade.N), 0.0f) * parameters.spectra[0].rgb / PI;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return max(dot(w_i, shade.N), 0.0f) / PI;
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return max(dot(shade.w_o, shade.N), 0.0f) * shade.emission;
    }
};

}

#endif // _MATERIALS_PBRT_DIFFUSE_HLSL
