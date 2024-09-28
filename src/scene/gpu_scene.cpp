
#include "gpu_scene.h"
#include "encode.h"

static VkTransformMatrixKHR to_transform(Mat4 m) {
    VkTransformMatrixKHR ret = {};
    ret.matrix[0][0] = m[0][0];
    ret.matrix[0][1] = m[1][0];
    ret.matrix[0][2] = m[2][0];
    ret.matrix[0][3] = m[3][0];
    ret.matrix[1][0] = m[0][1];
    ret.matrix[1][1] = m[1][1];
    ret.matrix[1][2] = m[2][1];
    ret.matrix[1][3] = m[3][1];
    ret.matrix[2][0] = m[0][2];
    ret.matrix[2][1] = m[1][2];
    ret.matrix[2][2] = m[2][2];
    ret.matrix[2][3] = m[3][2];
    return ret;
}

static void transfer_build_barrier(rvk::Commands& cmds) {

    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask =
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT,
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmds, &dep);
}

namespace GPU_Scene {

static constexpr u32 MAX_IMAGES = 2048;
static constexpr u32 MAX_SAMPLERS = 64;

static Material_Type convert_material_type(PBRT::Materials::Type type) {
    switch(type) {
    case PBRT::Materials::Type::conductor: {
        return Material_Type::pbrt_conductor;
    } break;
    case PBRT::Materials::Type::dielectric: {
        return Material_Type::pbrt_dielectric;
    } break;
    case PBRT::Materials::Type::diffuse: {
        return Material_Type::pbrt_diffuse;
    } break;
    case PBRT::Materials::Type::diffuse_transmission: {
        return Material_Type::pbrt_diffuse_transmission;
    } break;
    case PBRT::Materials::Type::mix: {
        return Material_Type::pbrt_mix;
    } break;
    case PBRT::Materials::Type::coated_diffuse: {
        return Material_Type::tungsten_rough_plastic;
    } break;
    case PBRT::Materials::Type::coated_conductor: {
        return Material_Type::tungsten_smooth_coat;
    } break;
    case PBRT::Materials::Type::hair: {
        return Material_Type::pbrt_hair;
    } break;
    case PBRT::Materials::Type::interface: {
        return Material_Type::pbrt_interface;
    } break;
    case PBRT::Materials::Type::measured: {
        return Material_Type::pbrt_measured;
    } break;
    case PBRT::Materials::Type::subsurface: {
        return Material_Type::pbrt_subsurface;
    } break;
    case PBRT::Materials::Type::thin_dielectric: {
        return Material_Type::pbrt_thin_dielectric;
    } break;
    }
    RPP_UNREACHABLE;
}

static GPU_Material convert_material(const PBRT::Scene& cpu, const PBRT::Material& mat,
                                     Slice<const u64> texture_to_image_index,
                                     Slice<const u64> texture_to_sampler_index) {
    GPU_Material ret;

    auto convert_texture = [&](const PBRT::ID<PBRT::Texture>& id) {
        if(id.invalid()) {
            return Pair{Vec4{}, GPU_Texture_ID{}};
        }
        const PBRT::Texture& tex = cpu.textures[id.id];
        if(tex.type == PBRT::Textures::Type::constant) {
            switch(tex.data_type) {
            case PBRT::Textures::Data::scalar: {
                return Pair{Vec4{tex.scalar, 0.0f, 0.0f, 0.0f}, GPU_Texture_ID::constant()};
            } break;
            case PBRT::Textures::Data::spectrum: {
                return Pair{Vec4{tex.spectrum, 0.0f}, GPU_Texture_ID::constant()};
            } break;
            }
            RPP_UNREACHABLE;
        } else if(tex.type == PBRT::Textures::Type::imagemap) {
            u64 idx = texture_to_image_index[id.id];
            u64 sampler_idx = texture_to_sampler_index[id.id];
            auto tid = idx >= MAX_IMAGES || sampler_idx >= MAX_SAMPLERS
                           ? GPU_Texture_ID{}
                           : GPU_Texture_ID::image(idx, sampler_idx);
            return Pair{Vec4{}, tid};
        } else {
            return Pair{Vec4{}, GPU_Texture_ID::proc(id.id)};
        }
    };

    auto roughness_params = [&](u32 rough_, u32 urough_, u32 vrough_, u32 remap_) {
        auto [rough, rough_tex] = convert_texture(mat.roughness);
        auto [urough, urough_tex] = convert_texture(mat.uroughness);
        auto [vrough, vrough_tex] = convert_texture(mat.vroughness);
        ret.spectra[rough_] = rough;
        ret.textures[rough_] = rough_tex;
        ret.spectra[urough_] = urough;
        ret.textures[urough_] = urough_tex;
        ret.spectra[vrough_] = vrough;
        ret.textures[vrough_] = vrough_tex;
        ret.parameters[remap_] = mat.remap_roughness;
    };

    auto coated_params = [&](u32 albedo_, u32 g_, u32 thickness_, u32 samples_, u32 depth_) {
        auto [albedo, albedo_tex] = convert_texture(mat.albedo);
        auto [g, g_tex] = convert_texture(mat.g);
        auto [thickness, thickness_tex] = convert_texture(mat.thickness);
        ret.spectra[albedo_] = albedo;
        ret.textures[albedo_] = albedo_tex;
        ret.spectra[g_] = g;
        ret.textures[g_] = g_tex;
        ret.spectra[thickness_] = thickness;
        ret.textures[thickness_] = thickness_tex;
        ret.parameters[samples_] = static_cast<u32>(mat.n_samples);
        ret.parameters[depth_] = static_cast<u32>(mat.max_depth);
    };

    auto diffuse_params = [&](u32 refl_) {
        auto [refl, refl_tex] = convert_texture(mat.reflectance);
        ret.spectra[refl_] = refl;
        ret.textures[refl_] = refl_tex;
    };

    switch(mat.type) {
    case PBRT::Materials::Type::conductor: {
        roughness_params(0, 1, 2, 0);
        bool use_eta_k = mat.reflectance.invalid();
        if(use_eta_k) {
            auto [eta, eta_tex] = convert_texture(mat.eta);
            auto [k, k_tex] = convert_texture(mat.k);
            ret.spectra[3] = eta;
            ret.textures[3] = eta_tex;
            ret.spectra[4] = k;
            ret.textures[4] = k_tex;
        } else {
            auto [refl, refl_tex] = convert_texture(mat.reflectance);
            ret.spectra[3] = refl;
            ret.textures[3] = refl_tex;
        }
        ret.parameters[1] = use_eta_k;
    } break;
    case PBRT::Materials::Type::dielectric: {
        roughness_params(0, 1, 2, 0);
        auto [eta, eta_tex] = convert_texture(mat.eta);
        ret.spectra[3] = eta;
        ret.textures[3] = eta_tex;
    } break;
    case PBRT::Materials::Type::thin_dielectric: {
        auto [eta, eta_tex] = convert_texture(mat.eta);
        ret.spectra[0] = eta;
        ret.textures[0] = eta_tex;
    } break;
    case PBRT::Materials::Type::diffuse: {
        diffuse_params(0);
    } break;
    case PBRT::Materials::Type::diffuse_transmission: {
        diffuse_params(0);
        auto [trans, trans_tex] = convert_texture(mat.transmittance);
        auto [scale, scale_tex] = convert_texture(mat.scale);
        ret.spectra[1] = trans;
        ret.textures[1] = trans_tex;
        ret.spectra[2] = scale;
        ret.textures[2] = scale_tex;
    } break;
    case PBRT::Materials::Type::mix: {
        auto [amount, amount_tex] = convert_texture(mat.amount);
        ret.spectra[0] = amount;
        ret.textures[0] = amount_tex;
        ret.parameters[0] = static_cast<u32>(mat.a.id);
        ret.parameters[1] = static_cast<u32>(mat.b.id);
    } break;
    case PBRT::Materials::Type::coated_diffuse: {
        roughness_params(0, 1, 2, 0);
        coated_params(3, 4, 5, 1, 2);
        diffuse_params(6);
    } break;
    case PBRT::Materials::Type::coated_conductor: {
        {
            auto [rough, rough_tex] = convert_texture(mat.conductor_roughness);
            auto [urough, urough_tex] = convert_texture(mat.conductor_uroughness);
            auto [vrough, vrough_tex] = convert_texture(mat.conductor_vroughness);
            ret.spectra[0] = rough;
            ret.textures[0] = rough_tex;
            ret.spectra[1] = urough;
            ret.textures[1] = urough_tex;
            ret.spectra[2] = vrough;
            ret.textures[2] = vrough_tex;
        }
        {
            auto [rough, rough_tex] = convert_texture(mat.interface_roughness);
            auto [urough, urough_tex] = convert_texture(mat.interface_uroughness);
            auto [vrough, vrough_tex] = convert_texture(mat.interface_vroughness);
            ret.spectra[3] = rough;
            ret.textures[3] = rough_tex;
            ret.spectra[4] = urough;
            ret.textures[4] = urough_tex;
            ret.spectra[5] = vrough;
            ret.textures[5] = vrough_tex;
        }
        ret.parameters[0] = mat.remap_roughness;
        coated_params(6, 7, 8, 1, 2);

        bool use_eta_k = mat.reflectance.invalid();
        if(use_eta_k) {
            auto [conductor_eta, conductor_eta_tex] = convert_texture(mat.conductor_eta);
            auto [conductor_k, conductor_k_tex] = convert_texture(mat.conductor_k);
            ret.spectra[9] = conductor_eta;
            ret.textures[9] = conductor_eta_tex;
            ret.spectra[10] = conductor_k;
            ret.textures[10] = conductor_k_tex;
        } else {
            auto [refl, refl_tex] = convert_texture(mat.reflectance);
            ret.spectra[9] = refl;
            ret.textures[9] = refl_tex;
        }
        ret.parameters[1] = use_eta_k;
    } break;
    case PBRT::Materials::Type::hair: {
        if(!mat.sigma_a.invalid()) {
            auto [sigma_a, sigma_a_tex] = convert_texture(mat.sigma_a);
            ret.spectra[0] = sigma_a;
            ret.textures[0] = sigma_a_tex;
            ret.parameters[0] = 0;
        } else if(!mat.reflectance.invalid()) {
            auto [refl, refl_tex] = convert_texture(mat.reflectance);
            ret.spectra[0] = refl;
            ret.textures[0] = refl_tex;
            ret.parameters[0] = 1;
        } else {
            auto [eumelanin, eumelanin_tex] = convert_texture(mat.eumelanin);
            auto [pheomelanin, pheomelanin_tex] = convert_texture(mat.pheomelanin);
            ret.spectra[0] = eumelanin;
            ret.textures[0] = eumelanin_tex;
            ret.spectra[1] = pheomelanin;
            ret.textures[1] = pheomelanin_tex;
            ret.parameters[0] = 2;
        }
        auto [eta, eta_tex] = convert_texture(mat.eta);
        auto [beta_m, beta_m_tex] = convert_texture(mat.beta_m);
        auto [beta_n, beta_n_tex] = convert_texture(mat.beta_n);
        auto [alpha, alpha_tex] = convert_texture(mat.alpha);
        ret.spectra[2] = eta;
        ret.textures[2] = eta_tex;
        ret.spectra[3] = beta_m;
        ret.textures[3] = beta_m_tex;
        ret.spectra[4] = beta_n;
        ret.textures[4] = beta_n_tex;
        ret.spectra[5] = alpha;
        ret.textures[5] = alpha_tex;
    } break;
    case PBRT::Materials::Type::subsurface: {
        auto [eta, eta_tex] = convert_texture(mat.eta);
        auto [g, g_tex] = convert_texture(mat.g);
        auto [mfp, mfp_tex] = convert_texture(mat.mfp);
        auto [refl, refl_tex] = convert_texture(mat.reflectance);
        auto [sigma_a, sigma_a_tex] = convert_texture(mat.sigma_a);
        auto [sigma_s, sigma_s_tex] = convert_texture(mat.sigma_s);
        auto [scale, scale_tex] = convert_texture(mat.scale);
        ret.spectra[0] = eta;
        ret.textures[0] = eta_tex;
        ret.spectra[1] = g;
        ret.textures[1] = g_tex;
        ret.spectra[2] = mfp;
        ret.textures[2] = mfp_tex;
        ret.spectra[3] = refl;
        ret.textures[3] = refl_tex;
        ret.spectra[4] = sigma_a;
        ret.textures[4] = sigma_a_tex;
        ret.spectra[5] = sigma_s;
        ret.textures[5] = sigma_s_tex;
        ret.spectra[6] = scale;
        ret.textures[6] = scale_tex;
    } break;
    case PBRT::Materials::Type::interface: {
    } break;
    case PBRT::Materials::Type::measured: {
    } break;
    }

    return ret;
}

struct Mesh_Ref {

    Mat4 T;
    u64 id = 0;

    struct Flags {
        GPU_Scene::Material_Type material_type = GPU_Scene::Material_Type::none;
        u32 material_id = RPP_UINT32_MAX;
        u32 alpha_id = RPP_UINT32_MAX;
        f32 alpha_cutoff = 0.0f;
        bool flip_v = false;
        bool flip_bt = false;
        bool double_sided = false;
        Vec4 emission;
    };

    Flags flags;
    Slice<const f32> positions;
    Slice<const f32> normals;
    Slice<const f32> tangents;
    Slice<const f32> uvs;
    Slice<const u32> indices;

    explicit Mesh_Ref(const PBRT::Scene& cpu, PBRT::Mesh_ID id) : id(id.id) {
        const auto& mesh = cpu.meshes[id.id];
        T = mesh.mesh_to_instance;
        positions = mesh.positions.slice();
        normals = mesh.normals.slice();
        tangents = mesh.tangents.slice();
        uvs = mesh.uvs.slice();
        indices = mesh.indices.slice();

        flags.emission = Vec4{mesh.emission, 0.0f};
        flags.material_type = mesh.material.invalid()
                                  ? GPU_Scene::Material_Type::none
                                  : convert_material_type(cpu.materials[mesh.material.id].type);
        flags.material_id = static_cast<u32>(mesh.material.id);
        if(!mesh.alpha.invalid() &&
           cpu.textures[mesh.alpha.id].type == PBRT::Textures::Type::imagemap) {
            flags.alpha_id = static_cast<u32>(mesh.alpha.id);
            flags.alpha_cutoff = 0.25f;
        }
        flags.flip_v = true;
        flags.flip_bt = false;
        flags.double_sided = true;

        if(flags.emission != Vec4{0.0f} ||
           flags.material_type == GPU_Scene::Material_Type::pbrt_dielectric ||
           flags.material_type == GPU_Scene::Material_Type::pbrt_thin_dielectric ||
           flags.material_type == GPU_Scene::Material_Type::pbrt_interface) {
            flags.double_sided = false;
        }
    }

    explicit Mesh_Ref(const GLTF::Scene& cpu, const GLTF::Primitive& primitive, u64 id) : id(id) {

        positions = primitive.positions.slice();
        normals = primitive.normals.slice();
        tangents = primitive.tangents.slice();
        uvs = primitive.uvs.slice();
        indices = primitive.indices.slice();

        flags.material_type = GPU_Scene::Material_Type::gltf;
        if(primitive.material != -1) {
            const auto& material = cpu.materials[primitive.material];

            flags.material_id = static_cast<u32>(primitive.material);
            flags.double_sided = material.double_sided;

            if(material.alpha_cutoff != 0.0f) {
                flags.alpha_cutoff = material.alpha_cutoff;
                if(material.base_color_texture != -1) {
                    flags.alpha_id = static_cast<u32>(material.base_color_texture);
                }
            }
        }
        flags.flip_v = false;
        flags.flip_bt = primitive.flip_bitangent;
    }

    bool check() const {

        if(positions.empty()) return false;
        if(normals.length()) {
            if(normals.length() != positions.length()) {
                warn("Mesh % has a different number of normals and positions, skipping.", id);
                return false;
            }
        }
        if(tangents.length()) {
            if(tangents.length() != positions.length()) {
                warn("Mesh % has a different number of tangents and positions, skipping.", id);
                return false;
            }
            if(tangents.length() != normals.length()) {
                warn("Mesh % has a different number of tangents and normals, skipping.", id);
                return false;
            }
        }
        if(uvs.length()) {
            if(uvs.length() / 2 != positions.length() / 3) {
                warn("Mesh % has a different number of uvs and positions, skipping.", id);
                return false;
            }
        }

        return true;
    }
};

struct Staging_Full {};
struct Device_Full {};

template<typename T>
using Result = Variant<T, Staging_Full, Device_Full>;

template<typename Material>
struct Materials_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    Slice<const Material> materials;
    Slice<const u64> texture_to_image_index;
    Slice<const u64> texture_to_sampler_index;
};

template<typename Light>
struct Lights_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    Slice<const Pair<Mat4, u32>> lights;
};

struct Geometry_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    Slice<const Mesh_Ref> meshes;
};

struct Geometry_Reference_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    Slice<const CPU_Geometry_Reference> geometry;
    Slice<const u64> texture_to_image_index;
    Slice<const u64> texture_to_sampler_index;
};

struct BLAS_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    rvk::BLAS::Buffers blas;
    Slice<const Mesh_Ref> meshes;
};

struct TLAS_Buffers {
    rvk::Buffer staging;
    rvk::Buffer device;
    rvk::TLAS::Buffers tlas;
    Slice<const rvk::TLAS::Instance> instances;
};

struct Image_Buffers {
    rvk::Buffer staging;
    rvk::Image image;
    rvk::Image_View view;
    Variant<Slice<const u8>, Slice<const f32>> data = Slice<const u8>{};
    u32 width = 0, height = 0, channels = 0;
};

#define BIND_STAGING(name, size)                                                                   \
    rvk::Buffer name;                                                                              \
    {                                                                                              \
        if(auto b = rvk::make_staging(size); b.ok()) {                                             \
            name = move(*b);                                                                       \
        } else {                                                                                   \
            return Staging_Full{};                                                                 \
        }                                                                                          \
    }

#define BIND_DEVICE(name, size, usage)                                                             \
    rvk::Buffer name;                                                                              \
    {                                                                                              \
        if(auto b = rvk::make_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage); b.ok()) {    \
            name = move(*b);                                                                       \
        } else {                                                                                   \
            return Device_Full{};                                                                  \
        }                                                                                          \
    }

template<typename Material>
static Result<Materials_Buffers<Material>>
allocate_materials(Slice<const Material> materials, Slice<const u64> texture_to_image_index,
                   Slice<const u64> texture_to_sampler_index) {
    u64 size = materials.length() * sizeof(GPU_Material);

    if(size == 0) return Materials_Buffers<Material>{};

    BIND_STAGING(staging, size);
    BIND_DEVICE(device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return Materials_Buffers{move(staging), move(device), materials, texture_to_image_index,
                             texture_to_sampler_index};
}

template<typename Scene>
static rvk::Buffer write_materials(
    rvk::Commands& cmds, const Scene& cpu,
    Materials_Buffers<If<Same<Scene, PBRT::Scene>, PBRT::Material, GLTF::Material>> buffers) {

    if(buffers.materials.length() == 0) return rvk::Buffer{};

    GPU_Material* map = reinterpret_cast<GPU_Material*>(buffers.staging.map());
    u64 offset = 0;

    if constexpr(Same<Scene, PBRT::Scene>) {
        for(auto& material : buffers.materials) {
            map[offset++] = convert_material(cpu, material, buffers.texture_to_image_index,
                                             buffers.texture_to_sampler_index);
        }
    } else {
        auto remap = [&](i32 id, bool has_const) {
            if(id == -1) return has_const ? GPU_Texture_ID::constant() : GPU_Texture_ID{};
            u64 idx = buffers.texture_to_image_index[id];
            u64 sampler_idx = buffers.texture_to_sampler_index[id];
            return idx >= MAX_IMAGES || sampler_idx >= MAX_SAMPLERS
                       ? GPU_Texture_ID{}
                       : GPU_Texture_ID::image(idx, sampler_idx);
        };

        for(auto& material : buffers.materials) {
            GPU_Material convert;
            convert.textures[0] = remap(material.base_color_texture, true);
            convert.textures[1] = remap(material.emissive_texture, true);
            convert.textures[2] = remap(material.metallic_roughness_texture, true);
            convert.textures[3] = remap(material.normal_texture, false);
            convert.spectra[0] = material.base_color;
            convert.spectra[1] = Vec4{material.emissive, 0.0f};
            convert.spectra[2] = Vec4{material.metallic, material.roughness, 0.0f, 0.0f};
            convert.spectra[3] = Vec4{material.normal_scale, material.alpha_cutoff, 0.0f, 0.0f};
            map[offset++] = convert;
        }
    }

    buffers.device.move_from(cmds, move(buffers.staging));
    return move(buffers.device);
}

template<typename Scene>
static Async::Task<rvk::Buffer> write_materials_async(
    Async::Pool<>& pool, const Scene& cpu,
    Materials_Buffers<If<Same<Scene, PBRT::Scene>, PBRT::Material, GLTF::Material>> buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_materials(cmds, cpu, move(buffers)); });
}

template<typename Light>
static Result<Lights_Buffers<Light>> allocate_lights(Slice<const Pair<Mat4, u32>> lights) {
    u64 size = lights.length() * sizeof(GPU_Delta_Light);

    if(size == 0) return Lights_Buffers<Light>{};

    BIND_STAGING(staging, size);
    BIND_DEVICE(device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return Lights_Buffers<Light>{move(staging), move(device), lights};
}

template<typename Scene>
static rvk::Buffer
write_lights(rvk::Commands& cmds, const Scene& cpu,
             Lights_Buffers<If<Same<Scene, PBRT::Scene>, PBRT::Light, GLTF::Light>> buffers) {

    if(buffers.lights.length() == 0) return rvk::Buffer{};

    GPU_Delta_Light* map = reinterpret_cast<GPU_Delta_Light*>(buffers.staging.map());
    u64 offset = 0;

    if constexpr(Same<Scene, PBRT::Scene>) {
        for(auto& light_idx : buffers.lights) {
            const auto& light = cpu.lights[light_idx.second];

            GPU_Delta_Light convert;
            convert.power = light.scale * light.L;
            if(light.type == PBRT::Lights::Type::point) {
                convert.type = GPU_Delta_Light::Type::point;
                convert.params[0] = Vec4{light.from, 0.0f};
            } else if(light.type == PBRT::Lights::Type::spot) {
                convert.type = GPU_Delta_Light::Type::spot;
                convert.params[0] = Vec4{light.from, light.cone_angle};
                convert.params[1] = Vec4{light.to - light.from, light.cone_delta_angle};
            } else if(light.type == PBRT::Lights::Type::distant) {
                convert.type = GPU_Delta_Light::Type::directional;
                convert.params[0] = Vec4{light.to - light.from, 0.0f};
            } else {
                warn("Unsupported light type %, skipping.", light.type);
            }
            map[offset++] = convert;
        }
    } else {
        for(auto& light_idx : buffers.lights) {
            const auto& light = cpu.lights[light_idx.second];
            Vec3 location = light_idx.first.columns[3].xyz();
            Vec3 direction = light_idx.first.rotate(Vec3{0.0f, 0.0f, -1.0f});

            GPU_Delta_Light convert;
            convert.power = light.color * light.intensity;
            if(light.type == GLTF::Light::Type::point) {
                convert.type = GPU_Delta_Light::Type::point;
                convert.params[0] = Vec4{location, 0.0f};
            } else if(light.type == GLTF::Light::Type::spot) {
                convert.type = GPU_Delta_Light::Type::spot;
                convert.params[0] = Vec4{location, light.inner_cone_angle};
                convert.params[1] = Vec4{direction, light.outer_cone_angle};
            } else {
                convert.type = GPU_Delta_Light::Type::directional;
                convert.params[0] = Vec4{direction, 0.0f};
            }
            map[offset++] = convert;
        }
    }

    buffers.device.move_from(cmds, move(buffers.staging));
    return move(buffers.device);
}

template<typename Scene>
static Async::Task<rvk::Buffer>
write_lights_async(Async::Pool<>& pool, const Scene& cpu,
                   Lights_Buffers<If<Same<Scene, PBRT::Scene>, PBRT::Light, GLTF::Light>> buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_lights<Scene>(cmds, cpu, move(buffers)); });
}

static Result<Geometry_Buffers> allocate_geometry(Slice<const Mesh_Ref> meshes) {

    u64 size = 0;
    for(auto& mesh : meshes) {

        if(!mesh.check()) continue;

        u64 normals_size = (mesh.normals.length() / 3) * sizeof(u16) * 2;
        u64 tangents_size = normals_size ? (mesh.tangents.length() / 3) * sizeof(u16) : 0;
        u64 uvs_size = (mesh.uvs.length() / 2) * sizeof(u16) * 2;

        if(normals_size || uvs_size || tangents_size) {
            size += normals_size + uvs_size + tangents_size;
            size = Math::align(size, 16);
            size += mesh.indices.bytes();
            size = Math::align(size, 16);
        }
    }

    if(size == 0) return Geometry_Buffers{rvk::Buffer{}, rvk::Buffer{}, meshes};

    BIND_STAGING(staging, size);
    BIND_DEVICE(device, size,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return Geometry_Buffers{move(staging), move(device), meshes};
}

static Geometry_Result write_geometry(rvk::Commands& cmds, Geometry_Buffers buffers) {

    if(buffers.meshes.empty()) return Geometry_Result{};

    u8* map = buffers.staging.map();
    u64 offset = 0;
    u64 device_addr = buffers.device.gpu_address();

    Vec<CPU_Geometry_Reference, Alloc> out(buffers.meshes.length());
    for(auto& mesh : buffers.meshes) {

        Geometry_Reference_Flags flags;
        flags.n = mesh.normals.length() != 0;
        flags.t = mesh.tangents.length() != 0;
        flags.uv = mesh.uvs.length() != 0;
        flags.flip_bt = mesh.flags.flip_bt;
        flags.double_sided = mesh.flags.double_sided;
        flags.flip_v = mesh.flags.flip_v;

        if(!mesh.check()) {
            out.push(CPU_Geometry_Reference{
                .vertex_address = 0,
                .index_address = 0,
                .flags = flags,
                .material_type = mesh.flags.material_type,
                .material_id = mesh.flags.material_id,
                .alpha_texture_id = mesh.flags.alpha_id,
                .alpha_cutoff = mesh.flags.alpha_cutoff,
                .emission = mesh.flags.emission,
            });
            continue;
        }

        u64 v_start = offset;
        u64 v_size = Encode::mesh(map + offset, mesh.uvs, mesh.normals, mesh.tangents);
        offset += v_size;

        offset = Math::align(offset, 16);

        u64 i_start = offset;
        u64 i_size = 0;
        if(v_size) {
            Libc::memcpy(map + offset, mesh.indices.data(), mesh.indices.bytes());
            i_size = mesh.indices.bytes();
            offset += i_size;
        }

        offset = Math::align(offset, 16);

        out.push(CPU_Geometry_Reference{
            .vertex_address = device_addr + v_start,
            .index_address = device_addr + i_start,
            .flags = flags,
            .material_type = mesh.flags.material_type,
            .material_id = mesh.flags.material_id,
            .alpha_texture_id = mesh.flags.alpha_id,
            .alpha_cutoff = mesh.flags.alpha_cutoff,
            .emission = mesh.flags.emission,
        });
    }

    if(buffers.staging) buffers.device.move_from(cmds, move(buffers.staging));
    return Geometry_Result{move(buffers.device), move(out)};
}

static Async::Task<Geometry_Result> write_geometry_async(Async::Pool<>& pool,
                                                         Geometry_Buffers buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_geometry(cmds, move(buffers)); });
}

static Result<Geometry_Reference_Buffers>
allocate_geometry_references(Slice<const CPU_Geometry_Reference> geometry,
                             Slice<const u64> texture_to_image_index,
                             Slice<const u64> texture_to_sampler_index) {

    u64 size = geometry.length() * sizeof(GPU_Geometry_Reference);

    if(size == 0) return Geometry_Reference_Buffers{};

    BIND_STAGING(staging, size);
    BIND_DEVICE(device, size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    return Geometry_Reference_Buffers{move(staging), move(device), geometry, texture_to_image_index,
                                      texture_to_sampler_index};
}

static rvk::Buffer write_geometry_references(rvk::Commands& cmds,
                                             Geometry_Reference_Buffers buffers) {

    if(buffers.geometry.length() == 0) return rvk::Buffer{};

    u8* map = buffers.staging.map();
    u64 offset = 0;

    for(u64 i = 0; i < buffers.geometry.length(); i++) {

        GPU_Texture_ID gpu_alpha_texture_id;
        u32 alpha_texture_id = buffers.geometry[i].alpha_texture_id;

        if(alpha_texture_id != RPP_UINT32_MAX) {
            u64 idx = buffers.texture_to_image_index[alpha_texture_id];
            u64 sampler_idx = buffers.texture_to_sampler_index[alpha_texture_id];
            gpu_alpha_texture_id = idx >= MAX_IMAGES || sampler_idx >= MAX_SAMPLERS
                                       ? GPU_Texture_ID{}
                                       : GPU_Texture_ID::image(idx, sampler_idx);
        }

        GPU_Geometry_Reference g = {
            .vertex_address = buffers.geometry[i].vertex_address,
            .index_address = buffers.geometry[i].index_address,
            .material_id = buffers.geometry[i].material_id,
            .alpha_texture_id = gpu_alpha_texture_id,
            .alpha_cutoff = buffers.geometry[i].alpha_cutoff,
            .flags = buffers.geometry[i].flags,
            .emission = buffers.geometry[i].emission,
        };
        Libc::memcpy(map + offset, &g, sizeof(GPU_Geometry_Reference));
        offset += sizeof(GPU_Geometry_Reference);
    }

    buffers.device.move_from(cmds, move(buffers.staging));
    return move(buffers.device);
}

static Async::Task<rvk::Buffer>
write_geometry_references_async(Async::Pool<>& pool, Geometry_Reference_Buffers buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_geometry_references(cmds, move(buffers)); });
}

template<typename Texture>
rvk::Sampler::Config sampler_config(const Texture& texture) {
    rvk::Sampler::Config sampler;
    if constexpr(Same<Texture, PBRT::Textures::Texture>) {
        sampler.mag = sampler.min =
            texture.filter == PBRT::Textures::Filter::point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.u = sampler.v = sampler.w =
            texture.wrap == PBRT::Textures::Wrap::repeat  ? VK_SAMPLER_ADDRESS_MODE_REPEAT
            : texture.wrap == PBRT::Textures::Wrap::clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    } else {
        sampler.mag = sampler.min = VK_FILTER_LINEAR;
        sampler.u = sampler.v = sampler.w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    return sampler;
}

static Result<Image_Buffers> allocate_envmap(const PBRT::Lights::Light& light) {

    if(light.type != PBRT::Lights::Type::infinite) return Image_Buffers{};

    u32 width = light.map.w;
    u32 height = light.map.h;
    u32 src_channels = light.map.channels;
    u32 dst_channels = src_channels == 1 ? 1 : 4;

    u64 staging_size = width * height * dst_channels * sizeof(f32);

    if(staging_size == 0) return Image_Buffers{};

    BIND_STAGING(staging, staging_size);

    VkFormat format = dst_channels == 1 ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;

    auto image = rvk::make_image(VkExtent3D{.width = width, .height = height, .depth = 1}, format,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if(!image.ok()) return Device_Full{};

    rvk::Image_View view = image->view(VK_IMAGE_ASPECT_COLOR_BIT);

    return Image_Buffers{move(staging), move(*image), move(view),  light.map.data.slice(),
                         width,         height,       src_channels};
}

template<typename Texture>
static Result<Image_Buffers> allocate_image(const Texture& texture) {

    bool is_srgb;
    Variant<Slice<const u8>, Slice<const f32>> pixel_data = Slice<const u8>{};
    u32 width, height, src_channels;

    if constexpr(Same<Texture, PBRT::Textures::Texture>) {
        texture.image.match(Overload{
            [&](const PBRT::Image_Data<u8>& data) {
                pixel_data = data.data.slice();
                src_channels = data.channels;
                width = data.w;
                height = data.h;
            },
            [&](const PBRT::Image_Data<f32>& data) {
                pixel_data = data.data.slice();
                src_channels = data.channels;
                width = data.w;
                height = data.h;
            },
        });
        is_srgb = texture.encoding == PBRT::Textures::Encoding::sRGB;
    } else {
        pixel_data = texture.data.slice();
        src_channels = texture.components;
        width = texture.width;
        height = texture.height;
        is_srgb = true;
    }
    if(src_channels < 1 || src_channels > 4) {
        warn("Image texture has bad channels (%).", src_channels);
        return Image_Buffers{};
    }

    bool no_data = pixel_data.match([](const auto& data) { return data.empty(); });
    bool is_hdr = pixel_data.match(Overload{
        [](const Slice<const u8>&) { return false; },
        [](const Slice<const f32>&) { return true; },
    });

    u32 dst_channels = src_channels == 1 ? 1 : 4;
    u64 staging_size = width * height * dst_channels * (is_hdr ? sizeof(f32) : sizeof(u8));

    if(staging_size == 0 || no_data) return Image_Buffers{};

    VkFormat format = {};
    if(is_hdr && dst_channels == 1) {
        format = VK_FORMAT_R32_SFLOAT;
    } else if(is_hdr && dst_channels == 4) {
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
    } else if(dst_channels == 1) {
        format = is_srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
    } else if(dst_channels == 4) {
        format = is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    } else {
        RPP_UNREACHABLE;
    }

    BIND_STAGING(staging, staging_size);

    auto image = rvk::make_image(VkExtent3D{.width = width, .height = height, .depth = 1}, format,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if(!image.ok()) {
        return Device_Full{};
    }

    rvk::Image_View view = image->view(VK_IMAGE_ASPECT_COLOR_BIT);

    return Image_Buffers{move(staging), move(*image), move(view),  move(pixel_data),
                         width,         height,       src_channels};
}

static GPU_Image write_image(rvk::Commands& cmds, Image_Buffers buffers) {

    if(buffers.data.match([](const auto& data) { return data.empty(); })) return GPU_Image{};

    if(buffers.channels == 1 || buffers.channels == 4) {
        buffers.staging.write(buffers.data.match([](const auto& data) { return data.to_bytes(); }));
    } else if(buffers.channels == 2) {
        buffers.data.match(Overload{
            [&](const Slice<const f32>& data) {
                Encode::rg32f_to_rgba32f(buffers.staging.map(), data, buffers.width,
                                         buffers.height);
            },
            [&](const Slice<const u8>& data) {
                Encode::rg8_to_rgba8(buffers.staging.map(), data, buffers.width, buffers.height);
            },
        });
    } else if(buffers.channels == 3) {
        buffers.data.match(Overload{
            [&](const Slice<const f32>& data) {
                Encode::rgb32f_to_rgba32f(buffers.staging.map(), data, buffers.width,
                                          buffers.height);
            },
            [&](const Slice<const u8>& data) {
                Encode::rgb8_to_rgba8(buffers.staging.map(), data, buffers.width, buffers.height);
            },
        });
    }

    buffers.image.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                             VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    buffers.image.from_buffer(cmds, move(buffers.staging));
    buffers.image.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT_KHR);

    return GPU_Image{
        .image = move(buffers.image),
        .view = move(buffers.view),
    };
}

static Async::Task<GPU_Image> write_image_async(Async::Pool<>& pool, Image_Buffers buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_image(cmds, move(buffers)); });
}

static Result<BLAS_Buffers> allocate_blas(Slice<Mesh_Ref> meshes) {

    u64 size = 0;
    Region(R) {
        Vec<rvk::BLAS::Size, Mregion<R>> sizes(meshes.length());

        for(auto mesh : meshes) {
            if(!mesh.check()) continue;

            u64 v = mesh.positions.bytes();
            u64 i = mesh.indices.bytes();
            u64 vi = Math::align_pow2(v + i, 16);

            sizes.push({mesh.positions.length() / 3, mesh.indices.length(), true,
                        mesh.flags.alpha_cutoff == 0.0f});
            size += vi + sizeof(VkTransformMatrixKHR);
        }

        if(size == 0) return BLAS_Buffers{};

        BIND_STAGING(staging, size);
        BIND_DEVICE(geometry, size,
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

        auto blas = rvk::make_blas(sizes.slice());
        if(!blas.ok()) {
            return Device_Full{};
        }

        return BLAS_Buffers{move(staging), move(geometry), move(*blas), meshes};
    }
}

static rvk::BLAS write_blas(rvk::Commands& cmds, BLAS_Buffers buffers) {

    if(buffers.meshes.empty()) return rvk::BLAS{};

    Region(R) {
        Vec<rvk::BLAS::Offset, Mregion<R>> offsets(buffers.meshes.length());

        u64 offset = 0;
        for(auto mesh : buffers.meshes) {
            if(!mesh.check()) continue;

            u64 v_size = mesh.positions.bytes();
            u64 i_size = mesh.indices.bytes();
            u64 vi_size_aligned = Math::align_pow2(v_size + i_size, 16);

            offsets.push({offset, offset + v_size, Opt{offset + vi_size_aligned},
                          mesh.positions.length() / 3, mesh.indices.length(),
                          mesh.flags.alpha_cutoff == 0.0f});
            offset += vi_size_aligned + sizeof(VkTransformMatrixKHR);
        }

        u8* map = buffers.staging.map();
        u64 i = 0;
        for(auto& mesh : buffers.meshes) {
            if(!mesh.check()) continue;

            auto T = to_transform(mesh.T);
            Libc::memcpy(map + offsets[i].vertex, mesh.positions.data(), mesh.positions.bytes());
            Libc::memcpy(map + offsets[i].index, mesh.indices.data(), mesh.indices.bytes());
            Libc::memcpy(map + *offsets[i].transform, &T, sizeof(VkTransformMatrixKHR));
            i++;
        }

        buffers.device.move_from(cmds, move(buffers.staging));

        transfer_build_barrier(cmds);

        return rvk::build_blas(cmds, move(buffers.blas), move(buffers.device), offsets.slice());
    }
}

static Async::Task<rvk::BLAS> write_blas_async(Async::Pool<>& pool, BLAS_Buffers buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_blas(cmds, move(buffers)); });
}

static Result<TLAS_Buffers> allocate_tlas(Slice<rvk::TLAS::Instance> instances) {

    u64 size = instances.bytes();

    if(size == 0) return TLAS_Buffers{};

    BIND_STAGING(staging, size);
    BIND_DEVICE(device, size,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    auto tlas = rvk::make_tlas(static_cast<u32>(instances.length()));
    if(!tlas.ok()) {
        return Device_Full{};
    }

    return TLAS_Buffers{move(staging), move(device), move(*tlas), instances};
}

static rvk::TLAS write_tlas(rvk::Commands& cmds, TLAS_Buffers buffers) {

    if(buffers.instances.empty()) return rvk::TLAS{};

    buffers.staging.write(buffers.instances.to_bytes());
    buffers.device.move_from(cmds, move(buffers.staging));

    transfer_build_barrier(cmds);

    return rvk::build_tlas(cmds, move(buffers.tlas), move(buffers.device), buffers.instances);
}

static Async::Task<rvk::TLAS> write_tlas_async(Async::Pool<>& pool, TLAS_Buffers buffers) {
    co_await pool.suspend();
    co_return co_await rvk::async(
        pool, [&](rvk::Commands& cmds) { return write_tlas(cmds, move(buffers)); });
}

Async::Task<void> Scene::await_all(Vec<Async::Task<GPU_Image>, Alloc>& image_tasks) {
    for(auto& t : image_tasks) {
        images.push(co_await t);
    }
    image_tasks.clear();
}

Async::Task<void> Scene::await_all(Vec<Async::Task<rvk::BLAS>, Alloc>& blas_tasks,
                                   Vec<Async::Task<Geometry_Result>, Alloc>& geom_tasks) {

    for(auto& t : blas_tasks) {
        object_blases.push(co_await t);
    }
    blas_tasks.clear();

    for(auto& t : geom_tasks) {
        object_to_geometry_index.push(cpu_geometry_references.length());
        auto [geometry_buffer, geometry_references] = co_await t;
        geometry_buffers.push(move(geometry_buffer));
        for(auto& ref : geometry_references) {
            cpu_geometry_references.push(ref);
        }
    }
    geom_tasks.clear();
}

template<typename T>
static bool out_of_memory(const Result<T>& result) {
    return result.match(Overload{
        [](const Staging_Full&) {
            warn("Out of staging memory, draining tasks...");
            return true;
        },
        [](const Device_Full&) {
            warn("Out of device memory, draining tasks...");
            return true;
        },
        [](const auto&) { return false; },
    });
}

Async::Task<void> Scene::upload(Async::Pool<>& pool, const PBRT::Scene& cpu, u32 parallelism) {

    co_await pool.suspend();

    { // Phase 1: top level BLAS
        Profile::Time_Point start = Profile::timestamp();

        Vec<Mesh_Ref, Alloc> non_emissive_meshes(cpu.top_level_meshes.length());
        Vec<Mesh_Ref, Alloc> emissive_meshes(cpu.top_level_meshes.length());

        for(auto mesh_id : cpu.top_level_meshes) {
            if(cpu.meshes[mesh_id.id].emission != Vec3{0.0f}) {
                emissive_meshes.push(Mesh_Ref{cpu, mesh_id});
            } else {
                non_emissive_meshes.push(Mesh_Ref{cpu, mesh_id});
            }
        }

        auto blas_task =
            allocate_blas(non_emissive_meshes.slice())
                .match(Overload{
                    [&](BLAS_Buffers buffers) { return write_blas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::BLAS> {
                        warn("Top level BLAS too large for staging heap.");
                        co_return rvk::BLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::BLAS> {
                        warn("Top level BLAS too large for device heap.");
                        co_return rvk::BLAS{};
                    },
                });
        auto emissive_blas_task =
            allocate_blas(emissive_meshes.slice())
                .match(Overload{
                    [&](BLAS_Buffers buffers) { return write_blas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::BLAS> {
                        warn("Emissive BLAS too large for staging heap.");
                        co_return rvk::BLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::BLAS> {
                        warn("Emissive BLAS too large for device heap.");
                        co_return rvk::BLAS{};
                    },
                });

        object_blases.push(co_await blas_task);
        object_blases.push(co_await emissive_blas_task);

        auto geom_task = allocate_geometry(non_emissive_meshes.slice())
                             .match(Overload{
                                 [&](Geometry_Buffers buffers) {
                                     return write_geometry_async(pool, move(buffers));
                                 },
                                 [&](Staging_Full) -> Async::Task<Geometry_Result> {
                                     warn("Top level geometry too large for staging heap.");
                                     co_return Geometry_Result{};
                                 },
                                 [&](Device_Full) -> Async::Task<Geometry_Result> {
                                     warn("Top level geometry too large for device heap.");
                                     co_return Geometry_Result{};
                                 },
                             });
        auto emissive_geom_task = allocate_geometry(emissive_meshes.slice())
                                      .match(Overload{
                                          [&](Geometry_Buffers buffers) {
                                              return write_geometry_async(pool, move(buffers));
                                          },
                                          [&](Staging_Full) -> Async::Task<Geometry_Result> {
                                              warn("Emissive geometry too large for staging heap.");
                                              co_return Geometry_Result{};
                                          },
                                          [&](Device_Full) -> Async::Task<Geometry_Result> {
                                              warn("Emissive geometry too large for device heap.");
                                              co_return Geometry_Result{};
                                          },
                                      });

        {
            object_to_geometry_index.push(cpu_geometry_references.length());

            auto [geometry_buffer, geometry_references] = co_await geom_task;
            geometry_buffers.push(move(geometry_buffer));
            for(auto& ref : geometry_references) {
                cpu_geometry_references.push(ref);
            }
        }
        {
            object_to_geometry_index.push(cpu_geometry_references.length());

            auto [emissive_geometry_buffer, emissive_geometry_references] =
                co_await emissive_geom_task;
            geometry_buffers.push(move(emissive_geometry_buffer));
            for(auto& ref : emissive_geometry_references) {
                cpu_geometry_references.push(ref);
            }
        }

        Profile::Time_Point end = Profile::timestamp();
        info("Built top level BLASes for % meshes in % ms.", cpu.top_level_meshes.length(),
             Profile::ms(end - start));
    }

    { // Phase 2: instance BLASes
        Profile::Time_Point start = Profile::timestamp();

        Vec<Vec<Mesh_Ref, Alloc>, Alloc> mesh_refs(cpu.objects.length());
        Vec<Async::Task<rvk::BLAS>, Alloc> blas_tasks(parallelism);
        Vec<Async::Task<Geometry_Result>, Alloc> geom_tasks(parallelism);

        u64 mesh_count = 0;

        for(u64 obj_idx = 0; obj_idx < cpu.objects.length(); obj_idx++) {

            auto& obj = cpu.objects[obj_idx];

            Vec<Mesh_Ref, Alloc> refs(obj.meshes.length());
            for(auto mesh_id : obj.meshes) {
                refs.push(Mesh_Ref{cpu, mesh_id});
                mesh_count++;
            }

            if(blas_tasks.full() || geom_tasks.full()) {
                co_await await_all(blas_tasks, geom_tasks);
            }

            auto blas = allocate_blas(refs.slice());
            if(out_of_memory(blas)) {
                co_await await_all(blas_tasks, geom_tasks);
                blas = allocate_blas(refs.slice());
            }

            blas_tasks.push(move(blas).match(Overload{
                [&](BLAS_Buffers buffers) { return write_blas_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<rvk::BLAS> {
                    warn("BLAS too large for staging heap.");
                    co_return rvk::BLAS{};
                },
                [&](Device_Full) -> Async::Task<rvk::BLAS> {
                    warn("BLAS too large for device heap.");
                    co_return rvk::BLAS{};
                },
            }));

            auto geometry = allocate_geometry(refs.slice());
            if(out_of_memory(geometry)) {
                co_await await_all(blas_tasks, geom_tasks);
                geometry = allocate_geometry(refs.slice());
            }

            geom_tasks.push(move(geometry).match(Overload{
                [&](Geometry_Buffers buffers) { return write_geometry_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<Geometry_Result> {
                    warn("Geometry too large for staging heap.");
                    co_return Geometry_Result{};
                },
                [&](Device_Full) -> Async::Task<Geometry_Result> {
                    warn("Geometry too large for device heap.");
                    co_return Geometry_Result{};
                },
            }));

            mesh_refs.push(move(refs));
        }

        co_await await_all(blas_tasks, geom_tasks);

        Profile::Time_Point end = Profile::timestamp();
        info("Built % instance BLASes for % meshes in % ms.", cpu.objects.length(), mesh_count,
             Profile::ms(end - start));
    }

    Traversal_Result traversal;
    {
        Profile::Time_Point start = Profile::timestamp();
        traversal = traverse(cpu);
        Profile::Time_Point end = Profile::timestamp();
        info("Traversed scene in % ms.", Profile::ms(end - start));
    }

    { // Phase 3: TLASes (depend on BLASes)
        Profile::Time_Point start = Profile::timestamp();

        auto tlas_task =
            allocate_tlas(traversal.instances.slice())
                .match(Overload{
                    [&](TLAS_Buffers buffers) { return write_tlas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::TLAS> {
                        warn("TLAS too large for staging heap.");
                        co_return rvk::TLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::TLAS> {
                        warn("TLAS too large for device heap.");
                        co_return rvk::TLAS{};
                    },
                });

        tlas = co_await tlas_task;

        auto emissive_tlas_task =
            allocate_tlas(traversal.emissive_instances.slice())
                .match(Overload{
                    [&](TLAS_Buffers buffers) { return write_tlas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::TLAS> {
                        warn("Emissive TLAS too large for staging heap.");
                        co_return rvk::TLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::TLAS> {
                        warn("Emissive TLAS too large for device heap.");
                        co_return rvk::TLAS{};
                    },
                });

        emissive_tlas = co_await emissive_tlas_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built TLASes from % instances (% emissive) in % ms.", traversal.instances.length(),
             traversal.emissive_instances.length(), Profile::ms(end - start));
    }

    { // Phase 4: textures
        Profile::Time_Point start = Profile::timestamp();

        Vec<Async::Task<GPU_Image>, Alloc> image_tasks(parallelism);

        u64 image_count = 0;

        // First sampler is for environment map
        {
            rvk::Sampler::Config config{
                .min = VK_FILTER_LINEAR,
                .mag = VK_FILTER_LINEAR,
                .u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            };
            sampler_configs.insert(config, 0);
            samplers.push(rvk::make_sampler(config));
        }

        for(u64 tex_idx = 0; tex_idx < cpu.textures.length(); tex_idx++) {

            auto& tex = cpu.textures[tex_idx];

            { // Find sampler
                auto config = sampler_config(tex);
                if(auto sampler_idx = sampler_configs.try_get(config); sampler_idx.ok()) {
                    texture_to_sampler_index.push(**sampler_idx);
                } else {
                    u64 idx = samplers.length();
                    texture_to_sampler_index.push(idx);
                    sampler_configs.insert(config, idx);
                    samplers.push(rvk::make_sampler(config));
                }
            }

            { // Find image
                if(tex.type == PBRT::Textures::Type::imagemap) {
                    texture_to_image_index.push(image_count++);
                } else {
                    texture_to_image_index.push(RPP_UINT64_MAX);
                    continue;
                }
            }

            if(image_tasks.full()) {
                co_await await_all(image_tasks);
            }

            auto image = allocate_image(tex);
            if(out_of_memory(image)) {
                co_await await_all(image_tasks);
                image = allocate_image(tex);
            }

            image_tasks.push(move(image).match(Overload{
                [&](Image_Buffers buffers) { return write_image_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<GPU_Image> {
                    warn("Image too large for staging heap.");
                    co_return GPU_Image{};
                },
                [&](Device_Full) -> Async::Task<GPU_Image> {
                    warn("Image too large for device heap.");
                    co_return GPU_Image{};
                },
            }));
        }

        co_await await_all(image_tasks);

        if(image_count >= MAX_IMAGES) {
            warn("Too many images, only the first % of % will be present.", MAX_IMAGES,
                 image_count);
        }
        if(samplers.length() >= MAX_SAMPLERS) {
            warn("Too many samplers, only the first % of % will be present.", MAX_SAMPLERS,
                 samplers.length());
        }

        Profile::Time_Point end = Profile::timestamp();
        info("Built % images from % textures in % ms.", image_count, cpu.textures.length(),
             Profile::ms(end - start));
    }

    { // Phase 5: geometry references (depends on BLASes and textures)
        Profile::Time_Point start = Profile::timestamp();

        auto geom_ref_task =
            allocate_geometry_references(cpu_geometry_references.slice(),
                                         texture_to_image_index.slice(),
                                         texture_to_sampler_index.slice())
                .match(Overload{
                    [&](Geometry_Reference_Buffers buffers) {
                        return write_geometry_references_async(pool, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Geometry references too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Geometry references too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        gpu_geometry_references = co_await geom_ref_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % geometry references in % ms.", cpu_geometry_references.length(),
             Profile::ms(end - start));
    }

    { // Phase 6: materials (depends on textures)
        Profile::Time_Point start = Profile::timestamp();

        auto materials_task =
            allocate_materials(cpu.materials.slice(), texture_to_image_index.slice(),
                               texture_to_sampler_index.slice())
                .match(Overload{
                    [&](Materials_Buffers<PBRT::Material> buffers) {
                        return write_materials_async<PBRT::Scene>(pool, cpu, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Materials too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Materials too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        materials = co_await materials_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % materials in % ms.", cpu.materials.length(), Profile::ms(end - start));
    }

    { // Phase 7: lights
        Profile::Time_Point start = Profile::timestamp();

        Vec<Pair<Mat4, u32>, Alloc> t_lights(cpu.lights.length());
        for(u32 i = 0; i < cpu.lights.length(); i++) {
            if(cpu.lights[i].type == PBRT::Lights::Type::infinite) {
                if(environment_map.image) {
                    warn("Multiple environment maps detected, only the first one will be used.");
                    continue;
                }
                auto image_task = allocate_envmap(cpu.lights[i])
                                      .match(Overload{
                                          [&](Image_Buffers buffers) {
                                              return write_image_async(pool, move(buffers));
                                          },
                                          [&](Staging_Full) -> Async::Task<GPU_Image> {
                                              warn("Envmap too large for staging heap.");
                                              co_return GPU_Image{};
                                          },
                                          [&](Device_Full) -> Async::Task<GPU_Image> {
                                              warn("Envmap too large for device heap.");
                                              co_return GPU_Image{};
                                          },
                                      });
                environment_map = co_await image_task;
            } else {
                t_lights.push(Pair{Mat4::I, i});
            }
        }

        auto lights_task =
            allocate_lights<PBRT::Light>(t_lights.slice())
                .match(Overload{
                    [&](Lights_Buffers<PBRT::Light> buffers) {
                        return write_lights_async<PBRT::Scene>(pool, cpu, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Lights too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Lights too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        lights = co_await lights_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % lights in % ms.", cpu.lights.length(), Profile::ms(end - start));
    }

    recreate_set();
}

Async::Task<void> Scene::upload(Async::Pool<>& pool, const GLTF::Scene& cpu, u32 parallelism) {
    co_await pool.suspend();

    { // Phase 1: mesh BLASes
        Profile::Time_Point start = Profile::timestamp();

        Vec<Vec<Mesh_Ref, Alloc>, Alloc> mesh_refs(cpu.meshes.length());
        Vec<Async::Task<rvk::BLAS>, Alloc> blas_tasks(parallelism);
        Vec<Async::Task<Geometry_Result>, Alloc> geom_tasks(parallelism);

        for(u64 mesh_idx = 0; mesh_idx < cpu.meshes.length(); mesh_idx++) {

            auto& mesh = cpu.meshes[mesh_idx];
            Vec<Mesh_Ref, Alloc> refs(mesh.primitives.length());
            for(auto& prim : mesh.primitives) {
                refs.push(Mesh_Ref{cpu, prim, mesh_idx});
            }

            if(blas_tasks.full() || geom_tasks.full()) {
                co_await await_all(blas_tasks, geom_tasks);
            }

            auto blas = allocate_blas(refs.slice());
            if(out_of_memory(blas)) {
                co_await await_all(blas_tasks, geom_tasks);
                blas = allocate_blas(refs.slice());
            }

            blas_tasks.push(move(blas).match(Overload{
                [&](BLAS_Buffers buffers) { return write_blas_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<rvk::BLAS> {
                    warn("BLAS too large for staging heap.");
                    co_return rvk::BLAS{};
                },
                [&](Device_Full) -> Async::Task<rvk::BLAS> {
                    warn("BLAS too large for device heap.");
                    co_return rvk::BLAS{};
                },
            }));

            auto geometry = allocate_geometry(refs.slice());
            if(out_of_memory(geometry)) {
                co_await await_all(blas_tasks, geom_tasks);
                geometry = allocate_geometry(refs.slice());
            }

            geom_tasks.push(move(geometry).match(Overload{
                [&](Geometry_Buffers buffers) { return write_geometry_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<Geometry_Result> {
                    warn("Geometry too large for staging heap.");
                    co_return Geometry_Result{};
                },
                [&](Device_Full) -> Async::Task<Geometry_Result> {
                    warn("Geometry too large for device heap.");
                    co_return Geometry_Result{};
                },
            }));

            mesh_refs.push(move(refs));
        }

        co_await await_all(blas_tasks, geom_tasks);

        Profile::Time_Point end = Profile::timestamp();
        info("Built % mesh BLASes in % ms.", cpu.meshes.length(), Profile::ms(end - start));
    }

    Traversal_Result traversal;
    {
        Profile::Time_Point start = Profile::timestamp();
        traversal = traverse(cpu);
        Profile::Time_Point end = Profile::timestamp();
        info("Traversed scene in % ms.", Profile::ms(end - start));
    }

    { // Phase 2: TLAS (depends on BLASes)
        Profile::Time_Point start = Profile::timestamp();

        auto tlas_task =
            allocate_tlas(traversal.instances.slice())
                .match(Overload{
                    [&](TLAS_Buffers buffers) { return write_tlas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::TLAS> {
                        warn("TLAS too large for staging heap.");
                        co_return rvk::TLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::TLAS> {
                        warn("TLAS too large for device heap.");
                        co_return rvk::TLAS{};
                    },
                });

        auto emissive_tlas_task =
            allocate_tlas(traversal.emissive_instances.slice())
                .match(Overload{
                    [&](TLAS_Buffers buffers) { return write_tlas_async(pool, move(buffers)); },
                    [&](Staging_Full) -> Async::Task<rvk::TLAS> {
                        warn("Emissive TLAS too large for staging heap.");
                        co_return rvk::TLAS{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::TLAS> {
                        warn("Emissive TLAS too large for device heap.");
                        co_return rvk::TLAS{};
                    },
                });

        tlas = co_await tlas_task;
        emissive_tlas = co_await emissive_tlas_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built TLASes from % instances (% emissive) in % ms.", traversal.instances.length(),
             traversal.emissive_instances.length(), Profile::ms(end - start));
    }

    { // Phase 3: textures
        Profile::Time_Point start = Profile::timestamp();

        Vec<Async::Task<GPU_Image>, Alloc> image_tasks(parallelism);

        for(u64 tex_idx = 0; tex_idx < cpu.textures.length(); tex_idx++) {

            auto& tex = cpu.textures[tex_idx];

            { // Find sampler
                auto config = sampler_config(tex);
                if(auto sampler_idx = sampler_configs.try_get(config); sampler_idx.ok()) {
                    texture_to_sampler_index.push(**sampler_idx);
                } else {
                    u64 idx = samplers.length();
                    texture_to_sampler_index.push(idx);
                    sampler_configs.insert(config, idx);
                    samplers.push(rvk::make_sampler(config));
                }
            }

            { // Find image
                texture_to_image_index.push(tex_idx);
            }

            if(image_tasks.full()) {
                co_await await_all(image_tasks);
            }

            auto image = allocate_image(tex);
            if(out_of_memory(image)) {
                co_await await_all(image_tasks);
                image = allocate_image(tex);
            }

            image_tasks.push(move(image).match(Overload{
                [&](Image_Buffers buffers) { return write_image_async(pool, move(buffers)); },
                [&](Staging_Full) -> Async::Task<GPU_Image> {
                    warn("Image too large for staging heap.");
                    co_return GPU_Image{};
                },
                [&](Device_Full) -> Async::Task<GPU_Image> {
                    warn("Image too large for device heap.");
                    co_return GPU_Image{};
                },
            }));
        }

        co_await await_all(image_tasks);

        if(cpu.textures.length() >= MAX_IMAGES) {
            warn("Too many images, only the first % of % will be present.", MAX_IMAGES,
                 cpu.textures.length());
        }
        if(samplers.length() >= MAX_SAMPLERS) {
            warn("Too many samplers, only the first % of % will be present.", MAX_SAMPLERS,
                 samplers.length());
        }

        Profile::Time_Point end = Profile::timestamp();
        info("Built % textures in % ms.", cpu.textures.length(), Profile::ms(end - start));
    }

    { // Phase 4: geometry references (depends on BLASes & textures)
        Profile::Time_Point start = Profile::timestamp();

        auto geom_ref_task =
            allocate_geometry_references(cpu_geometry_references.slice(),
                                         texture_to_image_index.slice(),
                                         texture_to_sampler_index.slice())
                .match(Overload{
                    [&](Geometry_Reference_Buffers buffers) {
                        return write_geometry_references_async(pool, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Geometry references too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Geometry references too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        gpu_geometry_references = co_await geom_ref_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % geometry references in % ms.", cpu_geometry_references.length(),
             Profile::ms(end - start));
    }

    { // Phase 5: materials (depends on textures)
        Profile::Time_Point start = Profile::timestamp();

        auto materials_task =
            allocate_materials(cpu.materials.slice(), texture_to_image_index.slice(),
                               texture_to_sampler_index.slice())
                .match(Overload{
                    [&](Materials_Buffers<GLTF::Material> buffers) {
                        return write_materials_async<GLTF::Scene>(pool, cpu, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Materials too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Materials too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        materials = co_await materials_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % materials in % ms.", cpu.materials.length(), Profile::ms(end - start));
    }

    { // Phase 6: lights
        Profile::Time_Point start = Profile::timestamp();

        auto lights_task =
            allocate_lights<GLTF::Light>(traversal.gltf_lights.slice())
                .match(Overload{
                    [&](Lights_Buffers<GLTF::Light> buffers) {
                        return write_lights_async<GLTF::Scene>(pool, cpu, move(buffers));
                    },
                    [&](Staging_Full) -> Async::Task<rvk::Buffer> {
                        warn("Lights too large for staging heap.");
                        co_return rvk::Buffer{};
                    },
                    [&](Device_Full) -> Async::Task<rvk::Buffer> {
                        warn("Lights too large for device heap.");
                        co_return rvk::Buffer{};
                    },
                });

        lights = co_await lights_task;

        Profile::Time_Point end = Profile::timestamp();
        info("Built % lights in % ms.", cpu.lights.length(), Profile::ms(end - start));
    }

    recreate_set();
}

void Scene::traverse(Scene::Traversal_Result& out, const PBRT::Scene& cpu,
                     const PBRT::Instance& instance, Mat4 parent_to_world) {

    auto& object = cpu.objects[instance.object.id];

    Mat4 instance_to_world =
        parent_to_world * object.object_to_parent * instance.instance_to_object;

    if(auto& blas = object_blases[instance.object.id + 2]) {
        u32 geometry_index = static_cast<u32>(object_to_geometry_index[instance.object.id + 2]);
        rvk::TLAS::Instance t_instance{
            .transform = to_transform(instance_to_world),
            .instanceCustomIndex = geometry_index,
            .mask = 0xff,
            .instanceShaderBindingTableRecordOffset = geometry_index,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = blas.gpu_address(),
        };
        out.instances.push(t_instance);

        bool is_emissive = false;
        for(auto& mesh : object.meshes) {
            if(cpu.meshes[mesh.id].emission != Vec3{0.0f}) {
                is_emissive = true;
                break;
            }
        }

        if(is_emissive) {
            out.emissive_instances.push(t_instance);
        }
    }

    for(auto& child : object.instances) {
        traverse(out, cpu, child, instance_to_world);
    }
}

Scene::Traversal_Result Scene::traverse(const PBRT::Scene& cpu) {

    Profile::Time_Point start = Profile::timestamp();

    Mat4 to_camera = Mat4::swap_x_z * cpu.camera.world_to_camera;

    Scene::Traversal_Result result;

    if(object_blases[0]) {
        u32 geometry_index = static_cast<u32>(object_to_geometry_index[0]);
        rvk::TLAS::Instance instance{
            .transform = to_transform(to_camera),
            .instanceCustomIndex = geometry_index,
            .mask = 0xff,
            .instanceShaderBindingTableRecordOffset = geometry_index,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = object_blases[0].gpu_address(),
        };
        result.instances.push(instance);
    }
    if(object_blases[1]) {
        u32 geometry_index = static_cast<u32>(object_to_geometry_index[1]);
        rvk::TLAS::Instance instance{
            .transform = to_transform(to_camera),
            .instanceCustomIndex = geometry_index,
            .mask = 0xff,
            .instanceShaderBindingTableRecordOffset = geometry_index,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = object_blases[1].gpu_address(),
        };
        result.instances.push(instance);
        result.emissive_instances.push(instance);
    }

    for(auto& instance : cpu.top_level_instances) {
        traverse(result, cpu, instance, to_camera);
    }

    Profile::Time_Point end = Profile::timestamp();
    info("Traversed scene in % ms.", Profile::ms(end - start));

    return result;
}

void Scene::traverse(Scene::Traversal_Result& out, const GLTF::Scene& cpu, const GLTF::Node& node,
                     Mat4 parent_to_world) {

    Mat4 instance_to_world = parent_to_world * node.node_to_parent;

    if(node.light >= 0) {
        out.gltf_lights.push(Pair{instance_to_world, static_cast<u32>(node.light)});
    }

    if(node.mesh >= 0) {
        if(auto& blas = object_blases[node.mesh]) {
            u32 geometry_index = static_cast<u32>(object_to_geometry_index[node.mesh]);
            rvk::TLAS::Instance instance{
                .transform = to_transform(instance_to_world),
                .instanceCustomIndex = geometry_index,
                .mask = 0xff,
                .instanceShaderBindingTableRecordOffset = geometry_index,
                .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                .accelerationStructureReference = blas.gpu_address(),
            };

            out.instances.push(instance);

            bool is_emissive = false;
            if(node.mesh >= 0) {
                auto& mesh = cpu.meshes[node.mesh];
                for(auto& prim : mesh.primitives) {
                    if(prim.material >= 0) {
                        auto& material = cpu.materials[prim.material];
                        if(material.emissive_texture != -1 || material.emissive != Vec3{0.0f}) {
                            is_emissive = true;
                            break;
                        }
                    }
                }
            }

            if(is_emissive) {
                out.emissive_instances.push(instance);
            }
        }
    }

    for(auto& child : node.children) {
        traverse(out, cpu, cpu.nodes[child], instance_to_world);
    }
}

Scene::Traversal_Result Scene::traverse(const GLTF::Scene& cpu) {

    Profile::Time_Point start = Profile::timestamp();

    Traversal_Result result;

    for(u32 node : cpu.top_level_nodes) {
        traverse(result, cpu, cpu.nodes[node], Mat4::I);
    }

    Profile::Time_Point end = Profile::timestamp();
    info("Traversed scene in % ms.", Profile::ms(end - start));

    return result;
}

rvk::Binding_Table Scene::table(Table_Type type, rvk::Commands& cmds, rvk::Pipeline& pipeline) {

    Profile::Time_Point start = Profile::timestamp();
    Opt<rvk::Binding_Table> table;

    switch(type) {
    case Table_Type::geometry_to_single: {
        Region(R) {
            Vec<u32, Mregion<R>> hit(cpu_geometry_references.length());
            for(u32 i = 0; i < cpu_geometry_references.length(); i++) {
                hit.push(2u);
            }
            table = rvk::make_table(cmds, pipeline,
                                    rvk::Binding_Table::Mapping{
                                        .gen = Slice{0u},
                                        .miss = Slice{1u},
                                        .hit = hit.slice(),
                                        .call = Slice<const u32>{},
                                    });
        }
    } break;
    case Table_Type::geometry_to_material: {
        Region(R) {
            Vec<u32, Mregion<R>> hit(cpu_geometry_references.length());
            for(u32 i = 0; i < cpu_geometry_references.length(); i++) {
                hit.push(2u + static_cast<u32>(cpu_geometry_references[i].material_type));
            }
            table = rvk::make_table(cmds, pipeline,
                                    rvk::Binding_Table::Mapping{
                                        .gen = Slice{0u},
                                        .miss = Slice{1u},
                                        .hit = hit.slice(),
                                        .call = Slice<const u32>{},
                                    });
        }
    } break;
    case Table_Type::geometry_to_id: {
        Region(R) {
            Vec<u32, Mregion<R>> hit(cpu_geometry_references.length());
            for(u32 i = 0; i < cpu_geometry_references.length(); i++) {
                hit.push(2u + i);
            }
            table = rvk::make_table(cmds, pipeline,
                                    rvk::Binding_Table::Mapping{
                                        .gen = Slice{0u},
                                        .miss = Slice{1u},
                                        .hit = hit.slice(),
                                        .call = Slice<const u32>{},
                                    });
        }
    } break;
    default: RPP_UNREACHABLE;
    }

    Profile::Time_Point end = Profile::timestamp();

    if(table.ok()) {
        info("Created shader binding table in % ms.", Profile::ms(end - start));
        return move(*table);
    } else {
        warn("Failed to create % shader binding table.", type);
        return rvk::Binding_Table{};
    }
}

void Scene::recreate_set() {
    Profile::Time_Point start = Profile::timestamp();

    Region(R) {
        rvk::drop([descriptor_set = move(descriptor_set)]() {});

        rvk::Bind::TLAS<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b0{tlas};
        rvk::Bind::TLAS<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b1{emissive_tlas};

        rvk::Bind::Buffer_Storage<SCENE_STAGES> b2{gpu_geometry_references};
        rvk::Bind::Buffer_Storage<SCENE_STAGES> b3{materials};

        Vec<rvk::Bind::Image_Sampled<SCENE_STAGES>, Mregion<R>> image_binds;
        for(auto& img : images) {
            image_binds.push(rvk::Bind::Image_Sampled<SCENE_STAGES>{img.view});
            if(image_binds.length() == MAX_IMAGES) break;
        }

        Vec<rvk::Bind::Sampler<SCENE_STAGES>, Mregion<R>> sampler_binds;
        for(auto& sampler : samplers) {
            sampler_binds.push(rvk::Bind::Sampler<SCENE_STAGES>{sampler});
            if(sampler_binds.length() == MAX_SAMPLERS) break;
        }

        rvk::Bind::Image_Sampled<SCENE_STAGES> b4{environment_map.view};
        rvk::Bind::Image_Sampled_Array<SCENE_STAGES> b5{image_binds.slice()};
        rvk::Bind::Sampler_Array<SCENE_STAGES> b6{sampler_binds.slice()};

        descriptor_set = rvk::make_set(descriptor_set_layout);

        for(u32 f = 0; f < rvk::frame_count(); f++) {
            rvk::write_set<Layout>(descriptor_set, f, b0, b1, b2, b3, b4, b5, b6);
        }
    }

    Profile::Time_Point end = Profile::timestamp();
    info("Wrote descriptor set in % ms.", Profile::ms(end - start));
}

bool Scene::has_environment_map() const {
    return environment_map.image;
}

rvk::Descriptor_Set_Layout& Scene::layout() {
    return descriptor_set_layout;
}

rvk::Descriptor_Set& Scene::set() {
    return descriptor_set;
}

Scene::Scene() {
    descriptor_set_layout =
        rvk::make_layout<Layout>(Slice{1u, 1u, 1u, 1u, 1u, MAX_IMAGES, MAX_SAMPLERS});
    recreate_set();
}

Async::Task<Scene> load(Async::Pool<>& pool, const PBRT::Scene& cpu, u32 parallelism) {
    Scene ret;
    co_await ret.upload(pool, cpu, parallelism);
    co_return ret;
}

Async::Task<Scene> load(Async::Pool<>& pool, const GLTF::Scene& cpu, u32 parallelism) {
    Scene ret;
    co_await ret.upload(pool, cpu, parallelism);
    co_return ret;
}

} // namespace GPU_Scene
