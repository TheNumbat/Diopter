
#ifndef _MATERIALS_TUNGSTEN_SMOOTH_COAT_HLSL
#define _MATERIALS_TUNGSTEN_SMOOTH_COAT_HLSL

#include "interface.hlsl"

#include "interface.hlsl"
#include "pbrt_dielectric.hlsl"
#include "pbrt_conductor.hlsl"

#define SMOOTH_COAT_IOR 1.3f

namespace Material {

struct Tungsten_Smooth_Coat {

    static void resolve(inout GPU_Material parameters, in Shade_Info shade) {
        Scene::resolve_texture(parameters.texture_ids[0], shade.uv, parameters.spectra[0]); // Conductor Roughness
        Scene::resolve_texture(parameters.texture_ids[1], shade.uv, parameters.spectra[1]); // Conductor Uroughness
        Scene::resolve_texture(parameters.texture_ids[2], shade.uv, parameters.spectra[2]); // Conductor Vroughness
        Scene::resolve_texture(parameters.texture_ids[6], shade.uv, parameters.spectra[6]); // Medium Albedo
        Scene::resolve_texture(parameters.texture_ids[8], shade.uv, parameters.spectra[8]); // Medium thickness
        Scene::resolve_texture(parameters.texture_ids[9], shade.uv, parameters.spectra[9]); // Conductor eta / Reflectance
        Scene::resolve_texture(parameters.texture_ids[10], shade.uv, parameters.spectra[10]); // Conductor k
        if(parameters.parameters[1] == 0) { // Determine eta, k via reflectance
            f32rgb r = clamp(parameters.spectra[9].rgb, 0.0f, 0.999f);
            parameters.spectra[9].rgb = 1.0f;
            parameters.spectra[10].rgb = 2.0f * sqrt(r) / sqrt(1.0f - r);
        }
    }

    static bool specular(in GPU_Material parameters, in Shade_Info shade, out Delta_Sample delta, inout u32 seed) {
        return false;
    }

    static bool distribution(in GPU_Material parameters, in Shade_Info shade, out f32dir w_i, inout u32 seed) {

        f32 cos_o = dot(shade.w_o, shade.N);
        if(cos_o <= 0.0f) return false;

        f32 cosThetaTi;
        f32 Fi = Scatter::fresnel_reflectance(cos_o, SMOOTH_COAT_IOR, cosThetaTi);

        f32 transmittance = exp(-2.0f * avg3(parameters.spectra[6].rgb) * parameters.spectra[8].r);

        f32 substrateWeight = transmittance * (1.0f - Fi);
        f32 specularWeight = Fi;
        f32 specularProbability = specularWeight/(specularWeight + substrateWeight);

        if(randf(seed) < specularProbability) {
            w_i = Scatter::reflect(shade.w_o, shade.N);
        } else {
            f32 etap;

            Shade_Info substrate = shade;
            if(!Scatter::refract(shade.w_o, shade.N, SMOOTH_COAT_IOR, etap, substrate.w_o)) return false;
            substrate.w_o = -substrate.w_o;

            GPU_Material conductor_material;
            conductor_material.spectra[0] = parameters.spectra[0];
            conductor_material.spectra[1] = parameters.spectra[1];
            conductor_material.spectra[2] = parameters.spectra[2];
            conductor_material.spectra[3] = parameters.spectra[9];
            conductor_material.spectra[4] = parameters.spectra[10];
            conductor_material.parameters[0] = parameters.parameters[0];
            conductor_material.parameters[1] = 1;

            if(!PBRT_Conductor::distribution(conductor_material, substrate, w_i, seed)) return false;

            w_i = f32dir(w_i.x / SMOOTH_COAT_IOR, cosThetaTi, w_i.z / SMOOTH_COAT_IOR);
        }

        return true;
    }

    static f32rgb evaluate(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32dir wi = shade.local(w_i);
        f32dir wo = shade.local(shade.w_o);

        if (wo.y <= 0.0f || wi.y <= 0.0f) return 0.0f;

        f32 cosThetaTi, cosThetaTo;
        f32 Fo = Scatter::fresnel_reflectance(wo.y, ROUGH_PLASTIC_IOR, cosThetaTo);
        f32 Fi = Scatter::fresnel_reflectance(wi.y, ROUGH_PLASTIC_IOR, cosThetaTi);

        if(same_hemisphere(wo, wi)) {
            return Fi;
        } else {
            f32dir woSubstrate = f32dir(wo.x / SMOOTH_COAT_IOR, copy_sign(cosThetaTi, wo.y), wo.z / SMOOTH_COAT_IOR);
            f32dir wiSubstrate = f32dir(wi.x / SMOOTH_COAT_IOR, copy_sign(cosThetaTo, wi.y), wi.z / SMOOTH_COAT_IOR);

            f32 laplacian = wi.y / (cosThetaTo * SMOOTH_COAT_IOR * SMOOTH_COAT_IOR);

            GPU_Material conductor_material;
            conductor_material.spectra[0] = parameters.spectra[0];
            conductor_material.spectra[1] = parameters.spectra[1];
            conductor_material.spectra[2] = parameters.spectra[2];
            conductor_material.spectra[3] = parameters.spectra[9];
            conductor_material.spectra[4] = parameters.spectra[10];
            conductor_material.parameters[0] = parameters.parameters[0];
            conductor_material.parameters[1] = 1;

            f32rgb substrateF = PBRT_Conductor::evaluate(conductor_material, shade, wiSubstrate, seed);

            f32rgb medium_albedo = parameters.spectra[6].rgb * parameters.spectra[8].r;
            if(max3(medium_albedo) > 0.0f) {
                substrateF *= exp(medium_albedo * (-1.0f / cosThetaTo - 1.0f / cosThetaTi));
            }

            return laplacian * (1.0f - Fi) * (1.0f - Fo) * substrateF;
        }
    }

    static f32 pdf(in GPU_Material parameters, in Shade_Info shade, in f32dir w_i, inout u32 seed) {

        f32dir wi = shade.local(w_i);
        f32dir wo = shade.local(shade.w_o);

        if(wo.y <= 0.0f || wi.y <= 0.0f) return 0.0f;

        f32 cosThetaTi, cosThetaTo;
        f32 Fi = Scatter::fresnel_reflectance(wi.y, SMOOTH_COAT_IOR, cosThetaTi);
        Scatter::fresnel_reflectance(wo.y, SMOOTH_COAT_IOR, cosThetaTo);

        f32dir woSubstrate = f32dir(wo.x / SMOOTH_COAT_IOR, copy_sign(cosThetaTi, wo.y), wo.z / SMOOTH_COAT_IOR);
        f32dir wiSubstrate = f32dir(wi.x / SMOOTH_COAT_IOR, copy_sign(cosThetaTo, wi.y), wi.z / SMOOTH_COAT_IOR);

        f32 transmittance = exp(-2.0f * avg3(parameters.spectra[6].rgb) * parameters.spectra[8].r);

        f32 substrateWeight = transmittance * (1.0f - Fi);
        f32 specularWeight = Fi;
        f32 specularProbability = specularWeight / (specularWeight + substrateWeight);

        if(same_hemisphere(wi, wo)) {
            return specularProbability;
        } else {

            GPU_Material conductor_material;
            conductor_material.spectra[0] = parameters.spectra[0];
            conductor_material.spectra[1] = parameters.spectra[1];
            conductor_material.spectra[2] = parameters.spectra[2];
            conductor_material.spectra[3] = parameters.spectra[9];
            conductor_material.spectra[4] = parameters.spectra[10];
            conductor_material.parameters[0] = parameters.parameters[0];
            conductor_material.parameters[1] = 1;

            return PBRT_Conductor::pdf(conductor_material, shade, wiSubstrate, seed) *
                    (1.0f - specularProbability) * abs(wi.y / cosThetaTo);
        }
    }

    static f32rgb emission(in GPU_Material parameters, in Shade_Info shade) {
        return max(dot(shade.w_o, shade.N), 0.0f) * shade.emission;
    }
};

}

#endif // _MATERIALS_TUNGSTEN_SMOOTH_COAT_HLSL
