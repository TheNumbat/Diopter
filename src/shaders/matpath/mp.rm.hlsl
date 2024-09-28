
#include "mp.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"
#include "../khr_builtins.hlsl"

[shader("miss")]
void main(inout Payload payload) {
    if(constants.environment < 0.0f) {
        payload.throughput *= Scene::sample_environment(payload.w_i).rgb;
    } else if(payload.w_i.y > 0.0f) {
        payload.throughput *= constants.environment;
    } else {
        payload.throughput = 0.0f;
    }
    payload.w_i = INVALID_POS;
}
