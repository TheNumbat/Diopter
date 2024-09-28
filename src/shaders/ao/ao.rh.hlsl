

#include "ao.hlsl"

#include "../hit_info.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"

struct Attributes {
    f32v2 bary;
};

[shader("closesthit")]
void main(inout Payload payload, in Attributes attribs) {

    u32v3 LaunchID = DispatchRaysIndex();
	u32v3 LaunchSize = DispatchRaysDimensions();

    u64 clock = vk::ReadClock(vk::SubgroupScope);
    u32 seed = tea(LaunchID.y * LaunchSize.x + LaunchID.x, u32(clock));

    Hit_Info info = Hit_Info::make(attribs.bary, constants.shading_normals == 1);

	payload.o = info.P + info.offset * info.GN;
    payload.d = sample_hemisphere_cosine_tnb(seed, info.ST, info.SN, info.SB);

    // Hack to prevent self-intersection due to shading normals
    if(dot(payload.d, info.GN) < 0.0f) payload.d = -payload.d;
}
