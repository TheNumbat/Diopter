
#include "ao.hlsl"
#include "../util.hlsl"

[shader("miss")]
void main(inout Payload payload) {
    payload.o = INVALID_POS;
}
