
#ifndef _MATERIALS_NONE_HLSL
#define _MATERIALS_NONE_HLSL

#include "interface.hlsl"

namespace Material {

static f32rgb MAGENTA = f32rgb(0.5f, 0.0f, 0.5f);

struct None {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        return false;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        w_i = sample_hemisphere_cosine_tnb(seed, shade.T, shade.N, shade.B);
        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return max(dot(w_i, shade.N), 0.0f) * MAGENTA / PI;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return max(dot(w_i, shade.N), 0.0f) / PI;
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return 0.0f;
    }
};

}

#endif // _MATERIALS_NONE_HLSL
