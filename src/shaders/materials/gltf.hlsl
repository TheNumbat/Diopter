
#ifndef _MATERIALS_GLTF_HLSL
#define _MATERIALS_GLTF_HLSL

#include "interface.hlsl"
#include "../scene.hlsl"

namespace Material {

namespace GGX {

f32dir sample_H(in f32 a, inout u32 seed) {
    f32v2 xi = f32v2(randf(seed), randf(seed));
    f32 phi = 2.0f * PI * xi.x;
    f32 cos_theta = sqrt((1.0f - xi.y) / (1.0f + (a - 1.0f) * xi.y));
    f32 sin_theta = sqrt(1.0f - cos_theta * cos_theta);

    f32dir dir;
    dir.x = cos(phi) * sin_theta;
    dir.y = cos_theta;
    dir.z = sin(phi) * sin_theta;
    return dir;
}

f32 D(in f32 N_H, in f32 a) {
    f32 b = N_H * N_H * (a - 1.0f) + 1.0f;
    return a / (PI * b * b);
}

f32 V(in f32 N_L, in f32 N_V, in f32 a) {
    f32 G_L = N_L + sqrt(a + (1.0f - a) * N_L * N_L);
    f32 G_V = N_V + sqrt(a + (1.0f - a) * N_V * N_V);
    return 1.0f / (G_L * G_V);
}

f32 pdf(in f32 N_H, in f32 L_H, in f32 a) {
    return D(N_H, a) * N_H / (4.0f * L_H);
}

f32rgb F(in f32rgb f0, in f32 L_H) {
    f32 cos5 = pow(1.0f - L_H, 5.0f);
    return f0 + (1.0f - f0) * cos5;
}

}

struct GLTF {

    static bool is_specular(in GPU_Material parameters) {
        return parameters.spectra[2].y < 0.001f;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        if(is_specular(parameters)) return 0.0f;

        f32 N_L = dot(shade.N, w_i);
        f32 N_V = dot(shade.N, shade.w_o);
        if(N_L <= 0.0f || N_V <= 0.0f) return 0.0f;

        f32dir H = normalize(w_i + shade.w_o);
        f32 L_H = dot(w_i, H);
        f32 N_H = dot(shade.N, H);
        if(N_H <= 0.0f) return 0.0f;

        f32rgb base_color = parameters.spectra[0].rgb;
        f32 metalness = parameters.spectra[2].x;
        f32 roughness = parameters.spectra[2].y;

        f32rgb diffuse_color = lerp(base_color, 0.0f, metalness);
        f32rgb f0 = lerp(0.04f, base_color, metalness);
        f32 a = roughness * roughness;

        f32rgb F = GGX::F(f0, L_H);
        f32rgb f_diffuse = (1.0f - F) * diffuse_color / PI;
        f32rgb f_specular = F * GGX::D(N_H, a) * GGX::V(N_L, N_V, a);

        return N_L * (f_diffuse + f_specular);
    }

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]);
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]);
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]);
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return abs(dot(shade.w_o, shade.N)) * (parameters.spectra[1].rgb + shade.emission);
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        if(is_specular(parameters)) {
            delta.w_i = Scatter::reflect(shade.w_o, shade.N);
            delta.atten = parameters.spectra[0].rgb;
            return true;
        }
        return false;
    }

    static bool sample_specular(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        f32 roughness = parameters.spectra[2].y;
        f32 a = roughness * roughness;

        f32dir H = GGX::sample_H(a, seed);
        H = shade.T * H.x + shade.N * H.y + shade.B * H.z;

        w_i = Scatter::reflect(shade.w_o, H);
        return dot(w_i, shade.N) > 0.0f;
    }

    static bool sample_diffuse(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        w_i = sample_hemisphere_cosine_tnb(seed, shade.T, shade.N, shade.B);
        return true;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return false;

        f32 metalness = max(parameters.spectra[2].x, 0.5f);
        if(randf(seed) < metalness) {
            return sample_specular(parameters, shade, w_i, seed);
        } else {
            return sample_diffuse(parameters, shade, w_i, seed);
        }
    }

    static f32 pdf_specular(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i) {
        f32 N_V = dot(shade.N, shade.w_o);
        f32 N_L = dot(shade.N, w_i);
        if(N_V <= 0.0f || N_L <= 0.0f) return 0.0f;

        f32dir H = normalize(w_i + shade.w_o);
        f32 N_H = dot(shade.N, H);
        f32 L_H = dot(w_i, H);

        f32 roughness = parameters.spectra[2].y;
        f32 a = roughness * roughness;

        return GGX::pdf(N_H, L_H, a);
    }

    static f32 pdf_diffuse(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i) {
        return max(dot(shade.N, w_i), 0.0f) / PI;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {
        if(is_specular(parameters)) return 0.0f;

        f32 metalness = max(parameters.spectra[2].x, 0.5f);
        return lerp(pdf_diffuse(parameters, shade, w_i), pdf_specular(parameters, shade, w_i), metalness);
    }
};

}

#endif // _MATERIALS_GLTF_HLSL
