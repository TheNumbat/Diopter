
#ifndef _SHADING_SHADING_HLSL
#define _SHADING_SHADING_HLSL

#include "../util.hlsl"

#define MODE_MATERIAL_ID 0
#define MODE_MATERIAL_TYPE 1
#define MODE_GEOMETRY_DIVERGENCE 2
#define MODE_MATERIAL_DIVERGENCE 3
#define MDOE_ACTIVE_LANES 4
#define MODE_ALPHA_MASK 5

#define MDOE_TEXTURE_START 6
#define MODE_TEXTURE_COUNT 12

#define MODE_PARAM_START (MDOE_TEXTURE_START + MODE_TEXTURE_COUNT)
#define MODE_PARAM_COUNT 4

struct Payload {
    [[vk::location(0)]] f32rgb color;
    [[vk::location(1)]] bool hit;
};

struct Constants {
    f32m4x4 iV, iP;
    u32 mode;
};

[[vk::push_constant]] Constants constants;

#endif // _SHADING_SHADING_HLSL