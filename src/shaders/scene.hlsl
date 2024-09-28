
#ifndef _SCENE_HLSL
#define _SCENE_HLSL

#include "util.hlsl"
#include "materials/interface.hlsl"

#define GEOM_HAS_NORMALS    0x1
#define GEOM_HAS_TANGENTS   0x2
#define GEOM_HAS_UVS        0x4
#define GEOM_FLIP_BITANGENT 0x8
#define GEOM_DOUBLE_SIDED   0x10
#define GEOM_FLIP_V         0x20

namespace Scene {

struct Vertex {
    i16v2 n;
    f16v2 uv;
    i16 t;
};

struct Triangle {
    Vertex v0;
    Vertex v1;
    Vertex v2;
};

struct GPU_Geometry_Flags {
    u32 n            : 1;
    u32 t            : 1;
    u32 uv           : 1;
    u32 flip_bt      : 1;
    u32 double_sided : 1;
    u32 flip_v       : 1;
    u32 pad          : 26;
};

struct Material_Info {
    u32 id;
    u32 alpha_id;
    f32 alpha_cutoff;
    f32rgb emission;
};

struct GPU_Geometry {
    uptr vertices;
    uptr indices;
    u32 material_id;
    u32 alpha_texture_id;
    f32 alpha_cutoff;
    u32 flags;
    f32v4 emission;

    u32v3 index(u32 id) {
        uptr addr = indices + id * sizeof(u32v3);
        return vk::RawBufferLoad<u32v3>(addr, 4);
    }

    template<typename T>
    T attrib16(u32 index, u32 offset, u32 stride) {
        uptr addr = vertices + offset + (index * stride);
        return vk::RawBufferLoad<T>(addr, 2);
    }
};

[[vk::binding(0, 0)]] RaytracingAccelerationStructure TLAS;
[[vk::binding(1, 0)]] RaytracingAccelerationStructure Emissive_TLAS;
[[vk::binding(2, 0)]] StructuredBuffer<GPU_Geometry> Geometry;
[[vk::binding(3, 0)]] StructuredBuffer<GPU_Material> Materials;
[[vk::binding(4, 0)]] Texture2D<float4> Environment_Map;
[[vk::binding(5, 0)]] Texture2D<float4> Images[];
[[vk::binding(6, 0)]] SamplerState Samplers[];

f32rgba sample_environment(in f32dir w_i) {
    f32v2 uv = sphere_equal_area(w_i);
    return Environment_Map.SampleLevel(Samplers[0], uv, 0);
}

f32rgba sample_image(in u32 image_id, in u32 sampler_id, in f32v2 uv) {
    return Images[NonUniformResourceIndex(image_id)].SampleLevel(Samplers[NonUniformResourceIndex(sampler_id)], uv, 0);
}

void resolve_texture(in u32 id, in f32v2 uv, out f32rgba color) {
    u32 type = (id & TEXTURE_TYPE_MASK) >> TEXTURE_TYPE_SHIFT;
    if(type == TEXTURE_TYPE_IMAGE) {
        u32 image = (id & TEXTURE_IMAGE_MASK) >> TEXTURE_IMAGE_SHIFT;
        u32 sampler_ = (id & TEXTURE_SAMPLER_MASK) >> TEXTURE_SAMPLER_SHIFT;
        color = sample_image(image, sampler_, uv);
    } else if(type == TEXTURE_TYPE_PROC) {
        color = f32rgba(0.5f, 0.0f, 0.5f, 1.0f);
    }
}

GPU_Material get_material(in u32 material_id) {
    return Materials[NonUniformResourceIndex(material_id)];
}

GPU_Geometry_Flags get_flags(in u32 geometry_id, out Material_Info material) {
    GPU_Geometry_Flags output;
    material.id = Geometry[NonUniformResourceIndex(geometry_id)].material_id;
    material.alpha_id = Geometry[NonUniformResourceIndex(geometry_id)].alpha_texture_id;
    material.alpha_cutoff = Geometry[NonUniformResourceIndex(geometry_id)].alpha_cutoff;
    material.emission = Geometry[NonUniformResourceIndex(geometry_id)].emission.rgb;
    u32 flags = Geometry[NonUniformResourceIndex(geometry_id)].flags;
    output.n = (flags & GEOM_HAS_NORMALS) == GEOM_HAS_NORMALS;
    output.t = (flags & GEOM_HAS_TANGENTS) == GEOM_HAS_TANGENTS;
    output.uv = (flags & GEOM_HAS_UVS) == GEOM_HAS_UVS;
    output.flip_bt = (flags & GEOM_FLIP_BITANGENT) == GEOM_FLIP_BITANGENT;
    output.double_sided = (flags & GEOM_DOUBLE_SIDED) == GEOM_DOUBLE_SIDED;
    output.flip_v = (flags & GEOM_FLIP_V) == GEOM_FLIP_V;
    return output;
}

Triangle get_triangle(in u32 geometry_id, in u32 primitive_id) {

    GPU_Geometry geom = Geometry[NonUniformResourceIndex(geometry_id)];

    Triangle t;
    if(!geom.indices) return t;

    bool has_normals = (geom.flags & GEOM_HAS_NORMALS) == GEOM_HAS_NORMALS;
    bool has_tangents = (geom.flags & GEOM_HAS_TANGENTS) == GEOM_HAS_TANGENTS;
    bool has_uvs = (geom.flags & GEOM_HAS_UVS) == GEOM_HAS_UVS;

    u32v3 vertex_id = geom.index(primitive_id);

    u32 n_stride = has_normals ? 4 : 0;
    u32 t_stride = has_tangents ? 2 : 0;
    u32 uv_stride = has_uvs ? 4 : 0;
    u32 stride = n_stride + t_stride + uv_stride;

    u32 n_offset = 0;
    u32 t_offset = n_stride;
    u32 uv_offset = n_stride + t_stride;

    if(has_normals) {
        t.v0.n = geom.attrib16<i16v2>(vertex_id.x, n_offset, stride);
        t.v1.n = geom.attrib16<i16v2>(vertex_id.y, n_offset, stride);
        t.v2.n = geom.attrib16<i16v2>(vertex_id.z, n_offset, stride);
    }
    if(has_tangents) {
        t.v0.t = geom.attrib16<i16>(vertex_id.x, t_offset, stride);
        t.v1.t = geom.attrib16<i16>(vertex_id.y, t_offset, stride);
        t.v2.t = geom.attrib16<i16>(vertex_id.z, t_offset, stride);
    }
    if(has_uvs) {
        t.v0.uv = geom.attrib16<f16v2>(vertex_id.x, uv_offset, stride);
        t.v1.uv = geom.attrib16<f16v2>(vertex_id.y, uv_offset, stride);
        t.v2.uv = geom.attrib16<f16v2>(vertex_id.z, uv_offset, stride);
    }

    return t;
}

} // namespace GPU_SCENE

#endif // _SCENE_HLSL