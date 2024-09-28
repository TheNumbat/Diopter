
#include "shading.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"

[[vk::binding(0, 1)]] RWTexture2D<f32rgba> out_image;

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
             1,                // sbtRecordStride
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

[shader("raygeneration")]
void main() {
    u32v3 LaunchID = DispatchRaysIndex();
    u32v3 LaunchSize = DispatchRaysDimensions();

    Ray ray = camera_ray(LaunchID, LaunchSize);

    Payload ray_result = trace_ray(ray, RAY_FLAG_FORCE_OPAQUE);

    f32rgb color = 0.0f;
    if(ray_result.hit) {
		color = ray_result.color;
    }

    u32v2 pixel = u32v2(LaunchID.xy);
    out_image[pixel] = f32rgba(color, 1.0f);
}
