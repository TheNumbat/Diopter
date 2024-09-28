

#pragma once

#include <rpp/base.h>
#include <rpp/pool.h>
#include <rpp/vmath.h>

using namespace rpp;

namespace GLTF {

using Alloc = Mallocator<"GLTF Parser">;

struct Node {
    Mat4 node_to_parent;
    i32 mesh = -1;
    i32 light = -1;
    Vec<u32, Alloc> children;
};

struct Primitive {
    Vec<f32, Alloc> positions;
    Vec<f32, Alloc> normals;
    Vec<f32, Alloc> tangents;
    Vec<f32, Alloc> uvs;
    Vec<u32, Alloc> indices;
    i32 material = -1;
    bool flip_bitangent = false;
};

struct Mesh {
    Vec<Primitive, Alloc> primitives;
};

struct Light {
    enum class Type : u8 {
        point,
        spot,
        directional,
    };

    Type type = Type::point;
    Vec3 color = Vec3{1.0f};
    f32 intensity = 1.0f;
    f32 range = 0.0f;
    f32 inner_cone_angle = 0.0f;
    f32 outer_cone_angle = 0.0f;
};

struct Material {
    f32 alpha_cutoff = 0.0f;
    bool double_sided = false;

    Vec4 base_color{1.0f};
    i32 base_color_texture = -1;

    f32 metallic = 1.0f;
    f32 roughness = 1.0f;
    i32 metallic_roughness_texture = -1;

    f32 normal_scale = 1.0f;
    i32 normal_texture = -1;

    Vec3 emissive;
    i32 emissive_texture = -1;
};

struct Texture {
    Vec<u8, Alloc> data;
    u32 width = 0;
    u32 height = 0;
    u32 components = 0;
};

struct Scene {
    Vec<Mesh, Alloc> meshes;
    Vec<Light, Alloc> lights;
    Vec<Texture, Alloc> textures;
    Vec<Material, Alloc> materials;
    Vec<Node, Alloc> nodes;
    Vec<u32, Alloc> top_level_nodes;
};

Async::Task<Scene> load(Async::Pool<>& pool, String_View file);

} // namespace GLTF
