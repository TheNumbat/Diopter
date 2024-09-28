
#pragma once

#include <rpp/base.h>
#include <rvk/rvk.h>

#include "gltf.h"
#include "pbrt.h"

using namespace rpp;

namespace GPU_Scene {

struct Scene;
Async::Task<Scene> load(Async::Pool<>& pool, const PBRT::Scene& cpu, u32 parallelism);
Async::Task<Scene> load(Async::Pool<>& pool, const GLTF::Scene& cpu, u32 parallelism);

using Alloc = Mallocator<"GPU Scene">;

enum class Table_Type : u8 {
    geometry_to_single,
    geometry_to_material,
    geometry_to_id,
};

enum class Material_Type : u32 {
    none,
    gltf,
    pbrt_conductor,
    pbrt_dielectric,
    pbrt_diffuse,
    pbrt_diffuse_transmission,
    pbrt_mix,
    pbrt_hair,
    pbrt_subsurface,
    pbrt_thin_dielectric,
    pbrt_interface,
    pbrt_measured,
    tungsten_rough_plastic,
    tungsten_smooth_coat,
};

struct GPU_Texture_ID {
    u32 id : 22 = 0;
    u32 sampler : 8 = 0;
    u32 type : 2 = 0;

    static GPU_Texture_ID image(u64 id, u64 sampler) {
        return GPU_Texture_ID{
            .id = static_cast<u32>(id), .sampler = static_cast<u32>(sampler), .type = 1};
    }
    static GPU_Texture_ID constant() {
        return GPU_Texture_ID{.id = 0, .sampler = 0, .type = 2};
    }
    static GPU_Texture_ID proc(u64 id) {
        return GPU_Texture_ID{.id = static_cast<u32>(id), .sampler = 0, .type = 3};
    }
};
static_assert(sizeof(GPU_Texture_ID) == sizeof(u32));

struct GPU_Material {
    GPU_Texture_ID textures[12];
    Vec4 spectra[12];
    u32 parameters[4] = {};
};
static_assert(sizeof(GPU_Material) == 16 * sizeof(Vec4));

struct Geometry_Reference_Flags {
    u32 n : 1;
    u32 t : 1;
    u32 uv : 1;
    u32 flip_bt : 1;
    u32 double_sided : 1;
    u32 flip_v : 1;
};
static_assert(sizeof(Geometry_Reference_Flags) == sizeof(u32));

struct GPU_Geometry_Reference {
    u64 vertex_address;
    u64 index_address;
    u32 material_id;
    GPU_Texture_ID alpha_texture_id;
    f32 alpha_cutoff;
    Geometry_Reference_Flags flags;
    Vec4 emission;
};
static_assert(sizeof(GPU_Geometry_Reference) == 6 * sizeof(u64));

struct CPU_Geometry_Reference {
    u64 vertex_address = 0;
    u64 index_address = 0;
    Geometry_Reference_Flags flags;
    Material_Type material_type = Material_Type::none;
    u32 material_id = UINT32_MAX;
    u32 alpha_texture_id = UINT32_MAX;
    f32 alpha_cutoff = 1.0f;
    Vec4 emission;
};

struct GPU_Image {
    rvk::Image image;
    rvk::Image_View view;
};

struct GPU_Delta_Light {
    enum class Type : u32 {
        none,
        point,
        spot,
        directional,
    };
    Type type = Type::none;
    Vec3 power = Vec3{1.0f};
    Vec4 params[2];
};

struct Geometry_Result {
    rvk::Buffer geometry;
    Vec<CPU_Geometry_Reference, Alloc> references;
};

struct Scene {

    Scene();
    ~Scene() = default;

    Scene(Scene& src) = delete;
    Scene& operator=(const Scene& src) = delete;

    Scene(Scene&& src) = default;
    Scene& operator=(Scene&& src) = default;

    rvk::Descriptor_Set& set();
    rvk::Descriptor_Set_Layout& layout();

    rvk::Binding_Table table(Table_Type type, rvk::Commands& cmds, rvk::Pipeline& pipeline);

    bool has_environment_map() const;

    static constexpr u32 SCENE_STAGES =
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;

    using Layout = List<rvk::Bind::TLAS<VK_SHADER_STAGE_RAYGEN_BIT_KHR>, // TLAS
                        rvk::Bind::TLAS<VK_SHADER_STAGE_RAYGEN_BIT_KHR>, // Emissive TLAS
                        rvk::Bind::Buffer_Storage<SCENE_STAGES>,         // geometry
                        rvk::Bind::Buffer_Storage<SCENE_STAGES>,         // materials
                        rvk::Bind::Image_Sampled<SCENE_STAGES>,          // environment map
                        rvk::Bind::Image_Sampled_Array<SCENE_STAGES>,    // images
                        rvk::Bind::Sampler_Array<SCENE_STAGES>           // samplers
                        >;

private:
    rvk::Descriptor_Set_Layout descriptor_set_layout;
    rvk::Descriptor_Set descriptor_set;
    void recreate_set();

    // Acceleration structures

    rvk::TLAS tlas;
    rvk::TLAS emissive_tlas;
    Vec<rvk::BLAS, Alloc> object_blases;

    // Other Data

    rvk::Buffer materials;
    rvk::Buffer lights;

    // Textures

    GPU_Image environment_map;
    Vec<GPU_Image, Alloc> images;
    Vec<u64, Alloc> texture_to_image_index;
    Vec<u64, Alloc> texture_to_sampler_index;
    Vec<rvk::Sampler, Alloc> samplers;
    Map<rvk::Sampler::Config, u64> sampler_configs;

    // Geometry

    rvk::Buffer gpu_geometry_references;
    Vec<rvk::Buffer, Alloc> geometry_buffers;

    Vec<u64, Alloc> object_to_geometry_index;
    Vec<CPU_Geometry_Reference, Alloc> cpu_geometry_references;

    /////////////

    Async::Task<void> upload(Async::Pool<>& pool, const PBRT::Scene& cpu, u32 parallelism);
    Async::Task<void> upload(Async::Pool<>& pool, const GLTF::Scene& cpu, u32 parallelism);

    struct Traversal_Result {
        Vec<rvk::TLAS::Instance, Alloc> instances;
        Vec<rvk::TLAS::Instance, Alloc> emissive_instances;
        Vec<Pair<Mat4, u32>, Alloc> gltf_lights;
    };

    void traverse(Traversal_Result& out, const PBRT::Scene& cpu, const PBRT::Instance& instance,
                  Mat4 parent_to_world);
    void traverse(Traversal_Result& out, const GLTF::Scene& cpu, const GLTF::Node& node,
                  Mat4 parent_to_world);

    Traversal_Result traverse(const PBRT::Scene& cpu);
    Traversal_Result traverse(const GLTF::Scene& cpu);

    Async::Task<void> await_all(Vec<Async::Task<GPU_Image>, Alloc>& image_tasks);
    Async::Task<void> await_all(Vec<Async::Task<rvk::BLAS>, Alloc>& blas_tasks,
                                Vec<Async::Task<Geometry_Result>, Alloc>& geom_tasks);

    friend Async::Task<Scene> load(Async::Pool<>& pool, const PBRT::Scene& cpu, u32 parallelism);
    friend Async::Task<Scene> load(Async::Pool<>& pool, const GLTF::Scene& cpu, u32 parallelism);
};

} // namespace GPU_Scene

RPP_ENUM(GPU_Scene::Table_Type, geometry_to_single, RPP_CASE(geometry_to_single),
         RPP_CASE(geometry_to_material), RPP_CASE(geometry_to_id));

RPP_ENUM(GPU_Scene::Material_Type, none, RPP_CASE(none), RPP_CASE(gltf), RPP_CASE(pbrt_conductor),
         RPP_CASE(pbrt_dielectric), RPP_CASE(pbrt_diffuse), RPP_CASE(pbrt_diffuse_transmission),
         RPP_CASE(pbrt_mix), RPP_CASE(pbrt_hair), RPP_CASE(pbrt_subsurface),
         RPP_CASE(pbrt_thin_dielectric), RPP_CASE(pbrt_interface), RPP_CASE(pbrt_measured),
         RPP_CASE(tungsten_rough_plastic), RPP_CASE(tungsten_smooth_coat));

namespace rpp::Hash {
template<>
struct Hash<GPU_Scene::Table_Type> {
    static constexpr u64 hash(GPU_Scene::Table_Type value) {
        return rpp::hash(static_cast<u8>(value));
    }
};
} // namespace rpp::Hash
