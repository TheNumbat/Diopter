
#include "ao.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"

[[vk::binding(0, 1)]] RWTexture2D<f32rgba> out_image;
[[vk::binding(1, 1)]] RWTexture2D<f32rgba> prev_image;

Payload trace_ray(in Ray ray, in u32 flags, in u32 offset) {
	RayDesc desc;
	desc.Origin = ray.o;
	desc.Direction = ray.d;
	desc.TMin = 0.0f;
	desc.TMax = LARGE_DIST;
    Payload payload;
    payload.o = 0.0f;
    TraceRay(Scene::TLAS,      // acceleration structure
             flags,            // rayFlags
             0xff,             // cullMask
             offset,           // sbtRecordOffset
             0,                // sbtRecordStride
             0,                // missIndex
             desc,             // rayDesc
             payload           // payload
    );
    return payload;
}

Ray camera_ray(in u32v3 LaunchID, in u32v3 LaunchSize, inout u32 seed) {
    f32v2 jitter = f32v2(randf(seed), randf(seed));
    const f32v2 pixelCenter = f32v2(LaunchID.xy) + jitter;
    const f32v2 inUV = pixelCenter / f32v2(LaunchSize.xy);
    f32v4 target = mul(constants.iP, f32v4(inUV * 2.0 - 1.0, 0, 1));
    f32v4 direction = mul(constants.iV, f32v4(target.xyz, 0));

    Ray ray;
    ray.d = normalize(direction.xyz);
    ray.o = mul(constants.iV, f32v4(0, 0, 0, 1)).xyz;
    return ray;
}

f32rgb miss(in Ray ray) {
    if(ray.d.y > 0.0f) {
        return constants.sun;
    } else {
        return 0.0f;
    }
}

[shader("raygeneration")]
void main() {
    u32v3 LaunchID = DispatchRaysIndex();
	u32v3 LaunchSize = DispatchRaysDimensions();

    u64 clock = vk::ReadClock(vk::SubgroupScope);
    u32 seed = tea(LaunchID.y * LaunchSize.x + LaunchID.x, u32(clock));

    Ray ray = camera_ray(LaunchID, LaunchSize, seed);

    f32rgb color = 0.0f;
    for(u32 i = 0; i < constants.samples_per_frame; i++) {
        Payload ray_result = trace_ray(ray, 0, 0);

        if(any(ray_result.o != INVALID_POS)) {
            Ray shadow = {ray_result.o, ray_result.d};
            Payload shadow_result = trace_ray(shadow, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                                                        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 1);
            if(all(shadow_result.o == INVALID_POS)) {
                color += miss(shadow);
            }
        } else {
            color += miss(ray);
        }
    }
    color /= f32(constants.samples_per_frame);

    u32v2 pixel = u32v2(LaunchID.xy);
    if(constants.stationary_frames > 0) {
        f32 a = 1.0f / f32(constants.stationary_frames + 1);
        f32rgb old_color = prev_image[pixel].xyz;
        out_image[pixel] = f32rgba(lerp(old_color, color, a), 1.0f);
    } else {
        out_image[pixel] = f32rgba(color, 1.0f);
    }
}
