
#define MATERIALS_IMPL

#include "mp.hlsl"

#include "../hit_info.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"
#include "../materials/materials.hlsl"

[[vk::constant_id(0)]]
const u32 MATERIAL_TYPE = 0;

struct Attributes {
    f32v2 bary;
};

[shader("closesthit")]
void main(inout Payload payload, in Attributes attribs) {

    u32v3 LaunchID = DispatchRaysIndex();
	u32v3 LaunchSize = DispatchRaysDimensions();

    u64 clock = vk::ReadClock(vk::SubgroupScope);
    u32 seed = tea(LaunchID.y * LaunchSize.x + LaunchID.x, u32(clock));

    Hit_Info hit = Hit_Info::make(attribs.bary, constants.shading_normals == 1);

    GPU_Material material = Scene::get_material(hit.id);
    Shade_Info shade = hit.shade(-payload.w_i);

    Material::resolve(MATERIAL_TYPE, material, shade);

    f32rgb emission = Material::emission(MATERIAL_TYPE, material, shade);
    if(any(emission != 0.0f)) {
        payload.throughput *= emission;
        payload.w_i = INVALID_POS;
        return;
    }

    Delta_Sample delta;
    if(Material::specular(MATERIAL_TYPE, material, shade, delta, seed)) {
        payload.w_i = delta.w_i;
        payload.throughput *= delta.atten;
        payload.o = hit.P + sign(dot(payload.w_i, hit.GN)) * hit.offset * hit.GN;
        return;
    }

    if(!Material::distribution(MATERIAL_TYPE, material, shade, payload.w_i, seed)) {
        payload.throughput = 0.0f;
        payload.w_i = INVALID_POS;
        return;
    }

    f32 pdf = Material::pdf(MATERIAL_TYPE, material, shade, payload.w_i, seed);
    if(pdf) {
        f32rgb atten = Material::evaluate(MATERIAL_TYPE, material, shade, payload.w_i, seed);
        payload.throughput *= atten / pdf;
        payload.o = hit.P + sign(dot(payload.w_i, hit.GN)) * hit.offset * hit.GN;
    } else {
        payload.throughput = 0.0f;
        payload.w_i = INVALID_POS;
    }
}
