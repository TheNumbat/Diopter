
#ifndef _POST_POST_HLSL
#define _POST_POST_HLSL

#include "../util.hlsl"

#define POST_OP_NONE      0
#define POST_OP_TO_LINEAR 1
#define POST_OP_TO_SRGB   2

#define POST_OP_TONEMAP_U2  3
#define POST_OP_TONEMAP_UT  4
#define POST_OP_TONEMAP_EXP 5

#define POST_OP_TONEMAP_U2_SRGB  6
#define POST_OP_TONEMAP_UT_SRGB  7
#define POST_OP_TONEMAP_EXP_SRGB 8

struct Constants {
    u32 operation;
    u32 width;
    u32 height;
    f32 gamma;
    f32 exposure;
};

[[vk::push_constant]] Constants constants;

[[vk::binding(0, 0)]] RWTexture2D<float4> input;
[[vk::binding(1, 0)]] RWTexture2D<float4> output;

f32rgb tonemap_uncharted2(in f32rgb color) {
	f32 A = 0.15f;
	f32 B = 0.50f;
	f32 C = 0.10f;
	f32 D = 0.20f;
	f32 E = 0.02f;
	f32 F = 0.30f;
	f32 W = 11.2f;
	return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

f32rgb tonemap_unrealtournament(in f32rgb color) {
	f32rgb out_ = tonemap_uncharted2(color.rgb * constants.exposure);
	return out_ * (1.0f / tonemap_uncharted2(11.2f));
}

f32rgb tonemap_exponential(in f32rgb color) {
    return 1.0f - exp(-color * constants.exposure);
}

[numthreads(8, 8, 1)]
void main(uint3 ID : SV_DISPATCHTHREADID) {

    if(ID.x >= constants.width || ID.y >= constants.height) {
        return;
    }

    if(constants.operation == POST_OP_TO_LINEAR) {
        f32rgb color = srgb_to_linear(input[ID.xy].rgb, constants.gamma);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TO_SRGB) {
        f32rgb color = linear_to_srgb(input[ID.xy].rgb, constants.gamma);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_EXP) {
        f32rgb color = tonemap_exponential(input[ID.xy].rgb);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_UT) {
        f32rgb color = tonemap_unrealtournament(input[ID.xy].rgb);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_U2) {
        f32rgb color = tonemap_uncharted2(input[ID.xy].rgb);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_EXP_SRGB) {
        f32rgb color = tonemap_exponential(input[ID.xy].rgb);
        color = linear_to_srgb(color, constants.gamma);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_UT_SRGB) {
        f32rgb color = tonemap_unrealtournament(input[ID.xy].rgb);
        color = linear_to_srgb(color, constants.gamma);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    if(constants.operation == POST_OP_TONEMAP_U2_SRGB) {
        f32rgb color = tonemap_uncharted2(input[ID.xy].rgb);
        color = linear_to_srgb(color, constants.gamma);
        output[ID.xy] = f32rgba(color.r, color.g, color.b, 1.0f);
        return;
    }

    output[ID.xy] = input[ID.xy];
}

#endif
