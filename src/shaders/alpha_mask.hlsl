
#ifndef _ALPHA_MASK_HLSL
#define _ALPHA_MASK_HLSL

#include "../util.hlsl"
#include "../scene.hlsl"
#include "../khr_builtins.hlsl"

struct Any_Hit_Attributes {
    f32v2 bary;
};

[shader("anyhit")]
void main(inout Payload payload, in Any_Hit_Attributes attribs) {

    u32 geometry_id = g_geometry_index + g_instance_custom_index;
    Scene::Material_Info material_info;
    Scene::GPU_Geometry_Flags flags = Scene::get_flags(geometry_id, material_info);

    if(material_info.alpha_cutoff == 0.0f) return;

    u32 texture_type = (material_info.alpha_id & TEXTURE_TYPE_MASK) >> TEXTURE_TYPE_SHIFT;

    if(texture_type == TEXTURE_TYPE_IMAGE) {
        u32 texture_image = (material_info.alpha_id & TEXTURE_IMAGE_MASK) >> TEXTURE_IMAGE_SHIFT;
        u32 texture_sampler = (material_info.alpha_id & TEXTURE_SAMPLER_MASK) >> TEXTURE_SAMPLER_SHIFT;

        Scene::Triangle tri = Scene::get_triangle(geometry_id, g_primitive_id);

        f32v3 b = f32v3(1 - attribs.bary.x - attribs.bary.y, attribs.bary.x, attribs.bary.y);
        f32v2 uv = 0.0f;
        if(flags.uv) {
            uv = b.x * tri.v0.uv + b.y * tri.v1.uv + b.z * tri.v2.uv;
        }
        if(flags.flip_v) {
            uv.y = 1.0f - uv.y;
        }

        f32 alpha = Scene::sample_image(texture_image, texture_sampler, uv).a;
        if(alpha < material_info.alpha_cutoff) {
            IgnoreHit();
        }
    }
}

#endif