
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>
#include <rvk/rvk.h>

#include "../scene/gpu_scene.h"
#include "pipeline.h"

using namespace rpp;

namespace Render {
struct Geometry {

    enum class Mode : u32 {
        barycentric,
        local_position,
        local_geometric_normal,
        local_shading_normal,
        geometry_index,
        instance_custom_index,
        primitive_id,
        shape_id,
        world_position,
        world_geometric_normal,
        world_shading_normal,
        uv,
        local_tangent,
        world_tangent,
        local_bitangent,
        world_bitangent,
        world_n_warp,
        world_tnb_warp,
        world_normal_mesh,
        world_tangent_mesh,
        flip_bitangent,
        traverse_time,
    };

    struct Constants {
        Mat4 iV, iP;
        Mode mode = Mode::barycentric;
        u32 prev_samples = 0;
    };

    static constexpr GPU_Scene::Table_Type table_type = GPU_Scene::Table_Type::geometry_to_single;

    using Push =
        rvk::Push<Constants, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                 VK_SHADER_STAGE_ANY_HIT_BIT_KHR>;

    using Layout = List<rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>,
                        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>>;

    static rvk::Shader_Loader::Token reload(rvk::Descriptor_Set_Layout& scene,
                                            rvk::Shader_Loader& loader,
                                            Function<void(Pipeline&&)> callback);
};
} // namespace Render

RPP_NAMED_RECORD(Render::Geometry::Constants, "Geometry::Constants", RPP_FIELD(iV), RPP_FIELD(iP),
                 RPP_FIELD(mode), RPP_FIELD(prev_samples));

RPP_NAMED_ENUM(Render::Geometry::Mode, "Geometry::Mode", barycentric, RPP_CASE(barycentric),
               RPP_CASE(local_position), RPP_CASE(local_geometric_normal),
               RPP_CASE(local_shading_normal), RPP_CASE(geometry_index),
               RPP_CASE(instance_custom_index), RPP_CASE(primitive_id), RPP_CASE(shape_id),
               RPP_CASE(world_position), RPP_CASE(world_geometric_normal),
               RPP_CASE(world_shading_normal), RPP_CASE(uv), RPP_CASE(local_tangent),
               RPP_CASE(world_tangent), RPP_CASE(local_bitangent), RPP_CASE(world_bitangent),
               RPP_CASE(world_n_warp), RPP_CASE(world_tnb_warp), RPP_CASE(world_normal_mesh),
               RPP_CASE(world_tangent_mesh), RPP_CASE(flip_bitangent), RPP_CASE(traverse_time));