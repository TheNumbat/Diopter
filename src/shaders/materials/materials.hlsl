
#ifndef _MATERIALS_HLSL
#define _MATERIALS_HLSL

#include "gltf.hlsl"
#include "none.hlsl"
#include "pbrt_diffuse.hlsl"
#include "pbrt_dielectric.hlsl"
#include "pbrt_conductor.hlsl"
#include "pbrt_diffuse_transmission.hlsl"
#include "pbrt_thin_dielectric.hlsl"
#include "tungsten_rough_plastic.hlsl"
#include "tungsten_smooth_coat.hlsl"

namespace Material {

void resolve(in u32 type, inout GPU_Material parameters, in Shade_Info info) {
    switch(type) {
    case MAT_GLTF: return GLTF::resolve(parameters, info);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::resolve(parameters, info);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::resolve(parameters, info);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::resolve(parameters, info);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::resolve(parameters, info);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::resolve(parameters, info);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::resolve(parameters, info);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::resolve(parameters, info);
    }
    return None::resolve(parameters, info);
}

bool specular(in u32 type, in GPU_Material parameters, in Shade_Info info, out Delta_Sample delta, inout u32 seed) {
    switch(type) {
    case MAT_GLTF: return GLTF::specular(parameters, info, delta, seed);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::specular(parameters, info, delta, seed);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::specular(parameters, info, delta, seed);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::specular(parameters, info, delta, seed);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::specular(parameters, info, delta, seed);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::specular(parameters, info, delta, seed);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::specular(parameters, info, delta, seed);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::specular(parameters, info, delta, seed);
    }
    return None::specular(parameters, info, delta, seed);
}

bool distribution(in u32 type, in GPU_Material parameters, in Shade_Info info, out f32dir w_i, inout u32 seed) {
    switch(type) {
    case MAT_GLTF: return GLTF::distribution(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::distribution(parameters, info, w_i, seed);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::distribution(parameters, info, w_i, seed);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::distribution(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::distribution(parameters, info, w_i, seed);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::distribution(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::distribution(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::distribution(parameters, info, w_i, seed);
    }
    return None::distribution(parameters, info, w_i, seed);
}

f32rgb evaluate(in u32 type, in GPU_Material parameters, in Shade_Info info, in f32dir w_i, inout u32 seed) {
    switch(type) {
    case MAT_GLTF: return GLTF::evaluate(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::evaluate(parameters, info, w_i, seed);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::evaluate(parameters, info, w_i, seed);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::evaluate(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::evaluate(parameters, info, w_i, seed);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::evaluate(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::evaluate(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::evaluate(parameters, info, w_i, seed);
    }
    return None::evaluate(parameters, info, w_i, seed);
}

f32rgb emission(in u32 type, in GPU_Material parameters, in Shade_Info info) {
    switch(type) {
    case MAT_GLTF: return GLTF::emission(parameters, info);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::emission(parameters, info);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::emission(parameters, info);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::emission(parameters, info);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::emission(parameters, info);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::emission(parameters, info);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::emission(parameters, info);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::emission(parameters, info);
    }
    return None::emission(parameters, info);
}

f32 pdf(in u32 type, in GPU_Material parameters, in Shade_Info info, in f32dir w_i, inout u32 seed) {
    switch(type) {
    case MAT_GLTF: return GLTF::pdf(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE: return PBRT_Diffuse::pdf(parameters, info, w_i, seed);
    case MAT_PBRT_DIELECTRIC: return PBRT_Dielectric::pdf(parameters, info, w_i, seed);
    case MAT_PBRT_CONDUCTOR: return PBRT_Conductor::pdf(parameters, info, w_i, seed);
    case MAT_PBRT_DIFFUSE_TRANSMISSION: return PBRT_Diffuse_Transmission::pdf(parameters, info, w_i, seed);
    case MAT_PBRT_THIN_DIELECTRIC: return PBRT_Thin_Dielectric::pdf(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_ROUGH_PLASTIC: return Tungsten_Rough_Plastic::pdf(parameters, info, w_i, seed);
    case MAT_TUNGSTEN_SMOOTH_COAT: return Tungsten_Smooth_Coat::pdf(parameters, info, w_i, seed);
    }
    return None::pdf(parameters, info, w_i, seed);
}

} // namespace Material

#endif // _MATERIALS_HLSL
