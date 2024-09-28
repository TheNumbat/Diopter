
#include "geometry.hlsl"
#include "../util.hlsl"

[shader("miss")]
void main(inout Payload payload) {
    payload.hit = false;
}
