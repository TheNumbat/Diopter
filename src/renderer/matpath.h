
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>
#include <rvk/rvk.h>

#include "pipeline.h"

using namespace rpp;

namespace Render {

struct MatPath {
    using Layout = List<rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>,
                        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>>;

    struct Constants {
        Mat4 iV, iP;
        u32 stationary_frames = 0;
        u32 samples_per_frame = 1;
        u32 max_depth = 1;
        u32 roulette = 1;
        u32 suppress_fireflies = 0;
        u32 shading_normals = 1;
        f32 environment = 1.0f;
    };

    static constexpr GPU_Scene::Table_Type table_type = GPU_Scene::Table_Type::geometry_to_material;

    using Push =
        rvk::Push<Constants, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                 VK_SHADER_STAGE_MISS_BIT_KHR>;

    static rvk::Shader_Loader::Token reload(rvk::Descriptor_Set_Layout& scene,
                                            rvk::Shader_Loader& loader,
                                            Function<void(Pipeline&&)> callback);
};
} // namespace Render

RPP_NAMED_RECORD(Render::MatPath::Constants, "MatPath::Constants", RPP_FIELD(iV), RPP_FIELD(iP),
                 RPP_FIELD(stationary_frames), RPP_FIELD(samples_per_frame),
                 RPP_FIELD(environment));