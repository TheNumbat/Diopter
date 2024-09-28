
#define HIT_POSITIONS

#include "geometry.hlsl"
#include "../util.hlsl"
#include "../scene.hlsl"
#include "../khr_builtins.hlsl"

f32rgb id_to_color(u32 id) {
    return f32rgb(
        (id % 16.0f) / 16.0f,
        ((id / 16.0f) % 16.0f) / 16.0f,
        ((id / 256.0f) % 16.0f) / 16.0f
    );
}

[shader("closesthit")]
void main(inout Payload payload, in Attributes attribs) {

    payload.hit = true;

    if(constants.mode == MODE_TRAVERSE_TIME) {
        return;
    }

    if(constants.mode == MODE_GEOMETRY_INDEX) {
        payload.color = id_to_color(g_geometry_index);
        return;
    }

    if(constants.mode == MODE_INSTANCE_CUSTOM_INDEX) {
        payload.color = id_to_color(g_instance_custom_index);
        return;
    }

    if(constants.mode == MODE_PRIMITIVE_ID) {
        payload.color = id_to_color(g_primitive_id);
        return;
    }

    if(constants.mode == MODE_SHAPE_ID) {
        payload.color = id_to_color(g_geometry_index + g_instance_custom_index);
        return;
    }

    f32v3 b = f32v3(1 - attribs.bary.x - attribs.bary.y, attribs.bary.x, attribs.bary.y);

    if(constants.mode == MODE_BARYCENTRIC) {
	    payload.color = b;
        return;
    }

    f32point p0 = g_hit_triangle_vertex_positions[0];
    f32point p1 = g_hit_triangle_vertex_positions[1];
    f32point p2 = g_hit_triangle_vertex_positions[2];
    f32point p = b.x * p0 + b.y * p1 + b.z * p2;

    if(constants.mode == MODE_LOCAL_POSITION) {
        payload.color = abs(sin(p));
        return;
    }

    f32dir gn = normalize(cross(p1 - p0, p2 - p0));

    if(constants.mode == MODE_LOCAL_GEOMETRIC_NORMAL) {
        payload.color = gn * 0.5f + 0.5f;
        return;
    }

    f32point wp = mul(f32v4(p, 1.0f), g_object_to_world);

    if(constants.mode == MODE_WORLD_POSITION) {
        payload.color = abs(sin(wp));
        return;
    }

    f32m3x4 g_object_to_world_IT = transpose(g_world_to_object);
    f32dir wgn = normalize(mul(gn, g_object_to_world_IT).xyz);

    if(constants.mode == MODE_WORLD_GEOMETRIC_NORMAL) {
        payload.color = wgn * 0.5f + 0.5f;
        return;
    }

    u32 geometry_id = g_geometry_index + g_instance_custom_index;
    Scene::Material_Info material_info;
    Scene::GPU_Geometry_Flags flags = Scene::get_flags(geometry_id, material_info);
    Scene::Triangle tri = Scene::get_triangle(geometry_id, g_primitive_id);

    f32dir sn = 0.0f;
    f32dir v0n = 0.0f, v1n = 0.0f, v2n = 0.0f;
    if(flags.n) {
        v0n = decode_octahedral_normal(decode_snorm16(tri.v0.n));
        v1n = decode_octahedral_normal(decode_snorm16(tri.v1.n));
        v2n = decode_octahedral_normal(decode_snorm16(tri.v2.n));
        sn = b.x * v0n + b.y * v1n + b.z * v2n;
    } else if(constants.mode != MODE_WORLD_SHADING_NORMAL_ONLY) {
        sn = gn;
    }

    if(constants.mode == MODE_LOCAL_SHADING_NORMAL) {
        payload.color = sn * 0.5f + 0.5f;
        return;
    }

    f32v2 uv = 0.0f;
    if(flags.uv) {
        uv = b.x * tri.v0.uv + b.y * tri.v1.uv + b.z * tri.v2.uv;
    }

    if(constants.mode == MODE_UV) {
        payload.color = f32v3(uv, 0.0f);
        return;
    }

    f32v3 wsn = normalize(mul(sn, g_object_to_world_IT).xyz);

    if(constants.mode == MODE_WORLD_SHADING_NORMAL || constants.mode == MODE_WORLD_SHADING_NORMAL_ONLY) {
        payload.color = wsn * 0.5f + 0.5f;
        return;
    }

    f32dir t = 0.0f;
    if(flags.t) {
        f32dir v0t = decode_diamond_tangent(v0n, decode_snorm16(tri.v0.t));
        f32dir v1t = decode_diamond_tangent(v1n, decode_snorm16(tri.v1.t));
        f32dir v2t = decode_diamond_tangent(v2n, decode_snorm16(tri.v2.t));
        t = b.x * v0t + b.y * v1t + b.z * v2t;
    } else if(constants.mode != MODE_WORLD_TANGENT_ONLY) {
        t = make_tangent(sn);
    }

    if(constants.mode == MODE_LOCAL_TANGENT) {
        payload.color = t * 0.5f + 0.5f;
        return;
    }

    f32dir wt = normalize(mul(t, g_object_to_world_IT).xyz);
    wt = normalize(wt - dot(wt, wsn) * wsn);

    if(constants.mode == MODE_WORLD_TANGENT || constants.mode == MODE_WORLD_TANGENT_ONLY) {
        payload.color = wt * 0.5f + 0.5f;
        return;
    }

    if(constants.mode == MODE_FLIP_BITANGENT) {
        if(flags.flip_bt) {
            payload.color = 1.0f;
        } else {
            payload.color = 0.0f;
        }
        return;
    }

    f32dir bt = cross(sn, t);
    if(flags.flip_bt) {
        bt = -bt;
    }

    if(constants.mode == MODE_LOCAL_BITANGENT) {
        payload.color = bt * 0.5f + 0.5f;
        return;
    }

    f32dir wbt = cross(wsn, wt);
    if(flags.flip_bt) {
        wbt = -wbt;
    }

    if(constants.mode == MODE_WORLD_BITANGENT) {
        payload.color = wbt * 0.5f + 0.5f;
        return;
    }

    if(constants.mode == MODE_WORLD_N_WARP) {
        float w = 1.0f - abs(dot(gn, sn));
        payload.color = f32v3(pow(w, 0.3f), 0, 0);
        return;
    }

    if(constants.mode == MODE_WORLD_TNB_WARP) {
        float nt = abs(dot(wsn, wt));
        float nb = abs(dot(wsn, wbt));
        payload.color = f32v3(pow(nt, 0.3), pow(nb, 0.3), 0);
        return;
    }

    payload.color = 1.0f;
}
