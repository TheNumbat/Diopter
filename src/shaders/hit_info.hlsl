
#ifndef _HIT_INFO_HLSL
#define _HIT_INFO_HLSL

#define HIT_POSITIONS
#include "khr_builtins.hlsl"

#include "util.hlsl"
#include "scene.hlsl"
#include "materials/interface.hlsl"

struct Hit_Info {
    f32point P;
    f32dir GN;
    f32dir ST;
    f32dir SN;
    f32dir SB;
    f32rgb emission;
    f32v2 uv;
    f32 offset;
    u32 id;

    Shade_Info shade(in f32dir w_o) {
        Shade_Info ret;
        ret.T = ST;
        ret.N = SN;
        ret.B = SB;
        ret.uv = uv;
        ret.w_o = w_o;
        ret.emission = emission;
        return ret;
    }

    static Hit_Info make(in f32v2 bary, in bool enable_shading_normals) {

        Hit_Info info;

        f32m4x3 o2w = g_object_to_world;
        f32m4x3 w2o = g_world_to_object;
        f32m3x4 o2w_IT = transpose(g_world_to_object);

        f32v3 b = f32v3(1 - bary.x - bary.y, bary.x, bary.y);
        f32point p0 = g_hit_triangle_vertex_positions[0];
        f32point p1 = g_hit_triangle_vertex_positions[1];
        f32point p2 = g_hit_triangle_vertex_positions[2];

        // Origin

        precise f32dir edge1 = p1 - p0;
        precise f32dir edge2 = p2 - p0;
        {
            precise f32point P = p0 + mad(bary.x, edge1, mul(bary.y, edge2));
            precise f32point Pw;
            Pw.x = o2w._m30 + mad(o2w._m00, P.x, mad(o2w._m10, P.y, mul(o2w._m20, P.z)));
            Pw.y = o2w._m31 + mad(o2w._m01, P.x, mad(o2w._m11, P.y, mul(o2w._m21, P.z)));
            Pw.z = o2w._m32 + mad(o2w._m02, P.x, mad(o2w._m12, P.y, mul(o2w._m22, P.z)));
            info.P = Pw;
        }

        // Query geometry data

        Scene::Triangle tri;
        Scene::GPU_Geometry_Flags flags;
        {
            u32 geometry_id = g_geometry_index + g_instance_custom_index;
            Scene::Material_Info material_info;
            flags = Scene::get_flags(geometry_id, material_info);

            info.id = material_info.id;
            info.emission = material_info.emission;
            if(flags.n || flags.t || flags.uv) {
                tri = Scene::get_triangle(geometry_id, g_primitive_id);
            }
        }

        // Geometric normal & error bounds
        // https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
        {
            f32dir obj_GN = cross(edge1, edge2);
            info.GN = mul(obj_GN, (f32m3x3)o2w_IT);

            f32 world_GN_scale = rsqrt(dot(info.GN, info.GN));
            info.GN *= world_GN_scale;

            if(flags.double_sided && dot(info.GN, g_world_ray_direction) > 0) {
                info.GN = -info.GN;
            }

            const f32 c0 = 5.9604644775390625E-8f;
            const f32 c1 = 1.788139769587360206060111522674560546875E-7f;
            const f32 c2 = 1.19209317972490680404007434844970703125E-7f;

            f32v3 extent3 = abs(edge1) + abs(edge2) + abs(abs(edge1) - abs(edge2));
            f32 extent = max(max(extent3.x, extent3.y), extent3.z);

            f32v3 obj_space_error = mad(c0, abs(p0), mul(c1, extent));
            f32v3 world_space_error = mad(c1, mul(abs(info.P), abs((f32m3x3)o2w)), mul(c2, abs(o2w[3])));
            obj_space_error = mad(c2, mul(f32v4(abs(info.P), 1), abs(w2o)), obj_space_error);

            f32 obj_space_offset = dot(obj_space_error, abs(obj_GN));
            f32 world_space_offset = dot(world_space_error, abs(info.GN));
            info.offset = mad(world_GN_scale, obj_space_offset, world_space_offset);
        }

        // UV

        if(flags.uv) {
            info.uv = b.x * tri.v0.uv + b.y * tri.v1.uv + b.z * tri.v2.uv;
        }
        if(flags.flip_v) {
            info.uv.y = 1.0f - info.uv.y;
        }

        // Shading normal

        f32dir v0n, v1n, v2n;
        if(enable_shading_normals && flags.n) {
            v0n = decode_octahedral_normal(decode_snorm16(tri.v0.n));
            v1n = decode_octahedral_normal(decode_snorm16(tri.v1.n));
            v2n = decode_octahedral_normal(decode_snorm16(tri.v2.n));

            info.SN = b.x * v0n + b.y * v1n + b.z * v2n;
            info.SN = normalize(mul(info.SN, o2w_IT).xyz);
            if(flags.double_sided && dot(info.SN, g_world_ray_direction) > 0) {
                info.SN = -info.SN;
            }
        } else {
            info.SN = info.GN;
        }

        // Shading tangent

        if(enable_shading_normals && flags.t) {
            f32dir v0t = decode_diamond_tangent(v0n, decode_snorm16(tri.v0.t));
            f32dir v1t = decode_diamond_tangent(v1n, decode_snorm16(tri.v1.t));
            f32dir v2t = decode_diamond_tangent(v2n, decode_snorm16(tri.v2.t));
            info.ST = b.x * v0t + b.y * v1t + b.z * v2t;
            info.ST = normalize(mul(info.ST, o2w_IT).xyz);
            info.ST = normalize(info.ST - dot(info.ST, info.SN) * info.SN);
        } else {
            info.ST = make_tangent(info.SN);
        }

        // Shading bitangent

        info.SB = cross(info.SN, info.ST);
        if(flags.flip_bt) {
            info.SB = -info.SB;
        }

        return info;
    }
};

#endif // _HIT_INFO_HLSL