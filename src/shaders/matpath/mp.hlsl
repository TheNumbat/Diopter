
#ifndef _AO_AO_HLSL
#define _AO_AO_HLSL

#include "../util.hlsl"

struct Constants {
    f32m4x4 iV, iP;
    u32 stationary_frames;
    u32 samples_per_frame;
    u32 max_depth;
    u32 roulette;
    u32 suppress_fireflies;
    u32 shading_normals;
    f32 environment;
};

struct Payload {
    [[vk::location(0)]] f32point o;
    [[vk::location(1)]] f32dir w_i;
    [[vk::location(2)]] f32rgb throughput;
};

[[vk::push_constant]] Constants constants;

#endif // _AO_AO_HLSL