
#include "mp.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"

[[vk::binding(0, 1)]] RWTexture2D<f32rgba> out_image;
[[vk::binding(1, 1)]] RWTexture2D<f32rgba> prev_image;

void trace_ray(inout Payload payload, in u32 flags) {
	RayDesc desc;
	desc.Origin = payload.o;
	desc.Direction = payload.w_i;
	desc.TMin = 0.0f;
	desc.TMax = LARGE_DIST;
    TraceRay(Scene::TLAS,      // acceleration structure
             flags,            // rayFlags
             0xff,             // cullMask
             0,                // sbtRecordOffset
             1,                // sbtRecordStride
             0,                // missIndex
             desc,             // rayDesc
             payload           // payload
    );
}

Payload camera_ray(in u32v3 LaunchID, in u32v3 LaunchSize, inout u32 seed) {
    f32v2 jitter = f32v2(randf(seed), randf(seed));
    const f32v2 pixelCenter = f32v2(LaunchID.xy) + jitter;
    const f32v2 inUV = pixelCenter / f32v2(LaunchSize.xy);
    f32v4 target = mul(constants.iP, f32v4(inUV * 2.0 - 1.0, 0, 1));
    f32v4 direction = mul(constants.iV, f32v4(target.xyz, 0));

    Payload payload;
    payload.throughput = 1.0f;
    payload.w_i = normalize(direction.xyz);
    payload.o = mul(constants.iV, f32v4(0, 0, 0, 1)).xyz;
    return payload;
}

[shader("raygeneration")]
void main() {
    u32v3 LaunchID = DispatchRaysIndex();
	u32v3 LaunchSize = DispatchRaysDimensions();

    u32v2 pixel = u32v2(LaunchID.xy);
    u64 clock = vk::ReadClock(vk::SubgroupScope);
    u32 seed = tea(LaunchID.y * LaunchSize.x + LaunchID.x, u32(clock));

    f32rgb new_color = 0.0f;
    f32rgb prev_color = prev_image[pixel].rgb;

    for(u32 i = 0; i < constants.samples_per_frame; i++) {
        Payload trace = camera_ray(LaunchID, LaunchSize, seed);

        for(u32 depth = 0; depth < constants.max_depth; depth++) {
            trace_ray(trace, 0);
            if(any(trace.w_i == INVALID_POS)) {
                break;
            }

            if(constants.roulette == 1 && depth > 1) {
                f32 luma = luminance(trace.throughput);
                f32 p_continue = clamp(luma, 0.05f, 0.95f);
                if(randf(seed) >= p_continue) break;
                trace.throughput /= p_continue;
            }
        }

        if(any(trace.w_i != INVALID_POS)) {
            trace.throughput = 0.0f;
        }
        if(constants.suppress_fireflies > 0) {
            trace.throughput = clamp(trace.throughput, 0.0f, max(prev_color, 0.5f) * constants.suppress_fireflies);
        }

        new_color += trace.throughput;
    }
    new_color /= f32(constants.samples_per_frame);

    if(constants.stationary_frames > 0) {
        f32 a = 1.0f / f32(constants.stationary_frames + 1);
        out_image[pixel] = f32rgba(lerp(prev_color, new_color, a), 1.0f);
    } else {
        out_image[pixel] = f32rgba(new_color, 1.0f);
    }
}
