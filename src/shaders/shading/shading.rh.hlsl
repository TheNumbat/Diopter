
#include "shading.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"
#include "../khr_builtins.hlsl"

[[vk::constant_id(0)]]
const u32 MATERIAL_TYPE = 0;

struct Attributes {
    f32v2 bary;
};

f32rgb id_to_color(u32 id) {
    return f32rgb(
        (id % 16.0f) / 16.0f,
        ((id / 16.0f) % 16.0f) / 16.0f,
        ((id / 256.0f) % 16.0f) / 16.0f
    );
}

u32 unique_values(u32 lanes, u32 val) {
    u32 idx = WaveGetLaneIndex();
    bool am_first = true;
    for(u32 i = 0; i < lanes; i++) {
        u32 check = WaveReadLaneAt(val, i);
        if(i >= idx) break;
        if(check == val) { am_first = false; break; }
    }
    return WaveActiveCountBits(am_first);
}

[shader("closesthit")]
void main(inout Payload payload, in Attributes attribs) {

    payload.hit = true;

    u32 geometry_id = g_geometry_index + g_instance_custom_index;
    Scene::Material_Info material_info;
    Scene::GPU_Geometry_Flags flags = Scene::get_flags(geometry_id, material_info);

    if(constants.mode == MODE_MATERIAL_ID) {
        payload.color = id_to_color(material_info.id);
        return;
    }

    if(constants.mode == MODE_MATERIAL_TYPE) {
        payload.color = id_to_color(MATERIAL_TYPE * 31);
        return;
    }

    u32 lanes = WaveGetLaneCount();

    if(constants.mode == MDOE_ACTIVE_LANES) {
        u32 active = WaveActiveCountBits(true);
        payload.color = temperature(1.0f - f32(active) / lanes);
        return;
    }

    if(constants.mode == MODE_MATERIAL_DIVERGENCE) {
        payload.color = temperature(f32(unique_values(lanes, material_info.id)) / lanes);
        return;
    }

    if(constants.mode == MODE_GEOMETRY_DIVERGENCE) {
        payload.color = temperature(f32(unique_values(lanes, geometry_id)) / lanes);
        return;
    }

    GPU_Material material = Scene::get_material(material_info.id);
    Scene::Triangle tri = Scene::get_triangle(geometry_id, g_primitive_id);
    f32v3 b = f32v3(1 - attribs.bary.x - attribs.bary.y, attribs.bary.x, attribs.bary.y);

    f32v2 uv = 0.0f;
    if(flags.uv) {
        uv = b.x * tri.v0.uv + b.y * tri.v1.uv + b.z * tri.v2.uv;
    }
    if(flags.flip_v) {
        uv.y = 1.0f - uv.y;
    }

    if(constants.mode == MODE_ALPHA_MASK) {
        u32 texture_type = (material_info.alpha_id & TEXTURE_TYPE_MASK) >> TEXTURE_TYPE_SHIFT;
        u32 texture_image = (material_info.alpha_id & TEXTURE_IMAGE_MASK) >> TEXTURE_IMAGE_SHIFT;
        u32 texture_sampler = (material_info.alpha_id & TEXTURE_SAMPLER_MASK) >> TEXTURE_SAMPLER_SHIFT;
        if(texture_type == TEXTURE_TYPE_IMAGE) {
            payload.color = Scene::sample_image(texture_image, texture_sampler, uv).a;
        } else {
            payload.color = f32rgb(1.0f, 0.0f, 1.0f);
        }
        return;
    }

    if(constants.mode >= MDOE_TEXTURE_START && constants.mode < MDOE_TEXTURE_START + MODE_TEXTURE_COUNT) {

        u32 texture_index = constants.mode - MDOE_TEXTURE_START;
        u32 texture_id = material.texture_ids[texture_index];
        u32 texture_type = (texture_id & TEXTURE_TYPE_MASK) >> TEXTURE_TYPE_SHIFT;
        u32 texture_image = (texture_id & TEXTURE_IMAGE_MASK) >> TEXTURE_IMAGE_SHIFT;
        u32 texture_sampler = (texture_id & TEXTURE_SAMPLER_MASK) >> TEXTURE_SAMPLER_SHIFT;

        if(texture_type == TEXTURE_TYPE_IMAGE) {
            payload.color = Scene::sample_image(texture_image, texture_sampler, uv).rgb;
        } else if(texture_type == TEXTURE_TYPE_CONST) {
            payload.color = material.spectra[texture_index].xyz;
        } else {
            payload.color = f32rgb(1.0f, 0.0f, 1.0f);
        }
        return;
    }

    if(constants.mode >= MODE_PARAM_START && constants.mode < MODE_PARAM_START + MODE_PARAM_COUNT) {
        u32 param_index = constants.mode - MODE_PARAM_START;
        payload.color = id_to_color(material.parameters[param_index] * 7);
        return;
    }
}
