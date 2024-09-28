
#ifndef _GEOMETRY_GEOMETRY_HLSL
#define _GEOMETRY_GEOMETRY_HLSL

#include "../util.hlsl"

#define MODE_BARYCENTRIC 0
#define MODE_LOCAL_POSITION 1
#define MODE_LOCAL_GEOMETRIC_NORMAL 2
#define MODE_LOCAL_SHADING_NORMAL 3
#define MODE_GEOMETRY_INDEX 4
#define MODE_INSTANCE_CUSTOM_INDEX 5
#define MODE_PRIMITIVE_ID 6
#define MODE_SHAPE_ID 7
#define MODE_WORLD_POSITION 8
#define MODE_WORLD_GEOMETRIC_NORMAL 9
#define MODE_WORLD_SHADING_NORMAL 10
#define MODE_UV 11
#define MODE_LOCAL_TANGENT 12
#define MODE_WORLD_TANGENT 13
#define MODE_LOCAL_BITANGENT 14
#define MODE_WORLD_BITANGENT 15
#define MODE_WORLD_N_WARP 16
#define MODE_WORLD_TNB_WARP 17
#define MODE_WORLD_SHADING_NORMAL_ONLY 18
#define MODE_WORLD_TANGENT_ONLY 19
#define MODE_FLIP_BITANGENT 20
#define MODE_TRAVERSE_TIME 21

struct Payload {
    [[vk::location(0)]] f32rgb color;
    [[vk::location(1)]] bool hit;
};

struct Attributes {
    f32v2 bary;
};

struct Constants {
    f32m4x4 iV, iP;
    u32 mode;
    u32 prev_samples;
    f32 alpha_threshhold;
};

[[vk::push_constant]] Constants constants;

#endif // _GEOMETRY_GEOMETRY_HLSL