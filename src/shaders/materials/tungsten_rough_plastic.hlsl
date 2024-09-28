
#ifndef _MATERIALS_TUNGSTEN_ROUGH_PLASTIC_HLSL
#define _MATERIALS_TUNGSTEN_ROUGH_PLASTIC_HLSL

#include "interface.hlsl"
#include "pbrt_dielectric.hlsl"

#define DIFFUSE_FRESNEL 0.091397f
#define ROUGH_PLASTIC_IOR 1.5f

namespace Material {

struct Tungsten_Rough_Plastic {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]); // Roughness
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]); // Uroughness
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]); // Vroughness
        Scene::resolve_texture(parameters.texture_ids[3], shade.uv, parameters.spectra[3]); // Medium albedo
        Scene::resolve_texture(parameters.texture_ids[5], shade.uv, parameters.spectra[5]); // Thickness
        Scene::resolve_texture(parameters.texture_ids[6], shade.uv, parameters.spectra[6]); // Diffuse albedo
        if(parameters.spectra[0].r > 0.0f) {
            parameters.spectra[1].r = parameters.spectra[0].r;
            parameters.spectra[2].r = parameters.spectra[0].r;
        }
        parameters.spectra[1].r = max(parameters.spectra[1].r, 0.001f);
        parameters.spectra[2].r = max(parameters.spectra[2].r, 0.001f);
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        return false;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {

        f32 cos_o = dot(shade.w_o, shade.N);
        if(cos_o <= 0.0f) return false;

        f32 transmittance = exp(-2.0f * avg3(parameters.spectra[3].rgb) * parameters.spectra[5].r);

        f32 Fi = Scatter::fresnel_reflectance(cos_o, ROUGH_PLASTIC_IOR);
        f32 substrateWeight = transmittance * (1.0f - Fi);
        f32 specularWeight = Fi;
        f32 specularProbability = specularWeight / (specularWeight + substrateWeight);

        if (randf(seed) < specularProbability) {

            GPU_Material dielectric_material;
            dielectric_material.spectra[0] = parameters.spectra[0];
            dielectric_material.spectra[1] = parameters.spectra[1];
            dielectric_material.spectra[2] = parameters.spectra[2];
            dielectric_material.spectra[3].r = ROUGH_PLASTIC_IOR;

            return PBRT_Dielectric::distribution(dielectric_material, shade, w_i, seed);
        } else {

            w_i = sample_hemisphere_cosine_tnb(seed, shade.T, shade.N, shade.B);
            return true;
        }
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32dir wi = shade.local(w_i);
        f32dir wo = shade.local(shade.w_o);

        if (wo.y <= 0.0f || wi.y <= 0.0f) return 0.0f;

        GPU_Material dielectric_material;
        dielectric_material.spectra[0] = parameters.spectra[0];
        dielectric_material.spectra[1] = parameters.spectra[1];
        dielectric_material.spectra[2] = parameters.spectra[2];
        dielectric_material.spectra[3].r = ROUGH_PLASTIC_IOR;

        f32rgb glossyR = PBRT_Dielectric::evaluate(dielectric_material, shade, w_i, seed);

        f32 Fo = Scatter::fresnel_reflectance(wo.y, ROUGH_PLASTIC_IOR);
        f32 Fi = Scatter::fresnel_reflectance(wi.y, ROUGH_PLASTIC_IOR);

        f32rgb medium_albedo = parameters.spectra[3].rgb * parameters.spectra[5].r;
        f32rgb diffuse_albedo = parameters.spectra[6].rgb;

        f32rgb diffuseR = ((1.0f - Fi) * (1.0f - Fo) * wi.y / (PI * ROUGH_PLASTIC_IOR * ROUGH_PLASTIC_IOR)) *
                          (diffuse_albedo / (1.0f - diffuse_albedo * DIFFUSE_FRESNEL));

        if (max3(medium_albedo) > 0.0f) {
            diffuseR *= exp(medium_albedo * (-1.0f / wi.y - 1.0f / wo.y));
        }

        return glossyR + diffuseR;
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32dir wi = shade.local(w_i);
        f32dir wo = shade.local(shade.w_o);

        if(wo.y <= 0.0f || wi.y <= 0.0f) return 0.0f;

        GPU_Material dielectric_material;
        dielectric_material.spectra[0] = parameters.spectra[0];
        dielectric_material.spectra[1] = parameters.spectra[1];
        dielectric_material.spectra[2] = parameters.spectra[2];
        dielectric_material.spectra[3].r = ROUGH_PLASTIC_IOR;

        f32 glossyPdf = PBRT_Dielectric::pdf(dielectric_material, shade, w_i, seed);

        f32 diffusePdf = max(wi.y, 0.0f) / PI;

        f32 transmittance = exp(-2.0f * avg3(parameters.spectra[3].rgb) * parameters.spectra[5].r);
        f32 Fi = Scatter::fresnel_reflectance(wi.y, ROUGH_PLASTIC_IOR);
        f32 substrateWeight = transmittance * (1.0f - Fi);
        f32 specularWeight = Fi;
        f32 specularProbability = specularWeight / (specularWeight + substrateWeight);

        diffusePdf *= (1.0f - specularProbability);
        glossyPdf *= specularProbability;

        return glossyPdf + diffusePdf;
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return max(dot(shade.w_o, shade.N), 0.0f) * shade.emission;
    }
};

}

#endif // _MATERIALS_TUNGSTEN_ROUGH_PLASTIC_HLSL
