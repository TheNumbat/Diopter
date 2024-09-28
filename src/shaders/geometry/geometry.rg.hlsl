
#include "geometry.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"

[[vk::binding(0, 1)]] RWTexture2D<f32rgba> out_image;
[[vk::binding(1, 1)]] RWTexture2D<f32rgba> prev_image;

Payload trace_ray(in Ray ray, in u32 flags) {
	RayDesc desc;
	desc.Origin = ray.o;
	desc.Direction = ray.d;
	desc.TMin = EPSILON;
	desc.TMax = LARGE_DIST;
    Payload payload;
    TraceRay(Scene::TLAS,      // acceleration structure
             flags,            // rayFlags
             0xff,             // cullMask
             0,                // sbtRecordOffset
             0,                // sbtRecordStride
             0,                // missIndex
             desc,             // rayDesc
             payload           // payload
    );
    return payload;
}

Ray camera_ray(in u32v3 LaunchID, in u32v3 LaunchSize) {
    const f32v2 pixelCenter = f32v2(LaunchID.xy) + 0.5f;
    const f32v2 inUV = pixelCenter / f32v2(LaunchSize.xy);
    f32v4 target = mul(constants.iP, f32v4(inUV * 2.0 - 1.0, 0, 1));
    f32v4 direction = mul(constants.iV, f32v4(target.xyz, 0));

    Ray ray;
    ray.d = normalize(direction.xyz);
    ray.o = mul(constants.iV, f32v4(0, 0, 0, 1)).xyz;
    return ray;
}

u64 timediff(in u64 start, in u64 end) {
    return end >= start ? (end - start) : (~0ull - (start - end));
}

[shader("raygeneration")]
void main() {
    u32v3 LaunchID = DispatchRaysIndex();
    u32v3 LaunchSize = DispatchRaysDimensions();

    Ray ray = camera_ray(LaunchID, LaunchSize);

    u64 start = vk::ReadClock(vk::SubgroupScope);
    Payload ray_result = trace_ray(ray, 0);
    u64 end = vk::ReadClock(vk::SubgroupScope);

    f32rgb color = 0.0f;
    if(ray_result.hit) {
		color = ray_result.color;
    }

    u32v2 pixel = u32v2(LaunchID.xy);

    if(constants.mode == MODE_TRAVERSE_TIME) {
        u64 elapsed = timediff(start, end);
        color = temperature(f32(elapsed / 500) / 1000.0f);

        u32 samples = min(constants.prev_samples, 64);
        if(samples > 0) {
            f32 a = 1.0f / f32(samples + 1);
            f32rgb old_color = prev_image[pixel].xyz;
            out_image[pixel] = f32rgba(lerp(old_color, color, a), 1.0f);
        } else {
            out_image[pixel] = f32rgba(color, 1.0f);
        }
    } else {
        out_image[pixel] = f32rgba(color, 1.0f);
    }
}
