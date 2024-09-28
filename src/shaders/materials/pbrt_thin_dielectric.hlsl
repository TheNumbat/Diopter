
#ifndef _MATERIALS_PBRT_THIN_DIELECTRIC_HLSL
#define _MATERIALS_PBRT_THIN_DIELECTRIC_HLSL

#include "interface.hlsl"

namespace Material {

struct PBRT_Thin_Dielectric {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {

        f32 eta = parameters.spectra[0].r;

        f32 R = Scatter::fresnel_reflectance(abs(dot(shade.w_o, shade.N)), eta);
        f32 T = 1.0f - R;

        if(R < 1.0f) {
            R += sqr(T) * R / (1.0f - sqr(R));
            T = 1.0f - R;
        }

        if(randf(seed) < R) {
            delta.w_i = Scatter::reflect(shade.w_o, shade.N);
            delta.atten = R;
        } else {
            delta.w_i = -shade.w_o;
            delta.atten = T;
        }

        return true;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        return false;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return 0.0f;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        return 0.0f;
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return 0.0f;
    }
};

}

#endif // _MATERIALS_PBRT_THIN_DIELECTRIC_HLSL
