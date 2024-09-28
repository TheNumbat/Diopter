
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>

using namespace rpp;

namespace Encode {

u32 normal_octahedral(Vec3 n);
u32 uv_half(Vec2 uv);
u16 tangent_diamond(Vec3 normal, Vec3 tangent);

u64 mesh(u8* out, Slice<const f32> uvs, Slice<const f32> normals, Slice<const f32> tangents);

void rg8_to_rgba8(u8* out, Slice<const u8> in, u32 w, u32 h);
void rgb8_to_rgba8(u8* out, Slice<const u8> in, u32 w, u32 h);

void rg32f_to_rgba32f(u8* out, Slice<const f32> in, u32 w, u32 h);
void rgb32f_to_rgba32f(u8* out, Slice<const f32> in, u32 w, u32 h);

} // namespace Encode