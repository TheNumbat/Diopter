
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>
#include <rvk/rvk.h>

#include "pipeline.h"

using namespace rpp;

namespace Render {

struct AO {
    using Layout = List<rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>,
                        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>>;

    struct Constants {
        Mat4 iV, iP;
        u32 stationary_frames = 0;
        u32 samples_per_frame = 1;
        u32 enable_shading_normals = 0;
        f32 sun = 1.0f;
    };

    static constexpr GPU_Scene::Table_Type table_type = GPU_Scene::Table_Type::geometry_to_single;

    using Push =
        rvk::Push<Constants, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR>;

    static rvk::Shader_Loader::Token reload(rvk::Descriptor_Set_Layout& scene,
                                            rvk::Shader_Loader& loader,
                                            Function<void(Pipeline&&)> callback);
};
} // namespace Render

RPP_NAMED_RECORD(Render::AO::Constants, "AO::Constants", RPP_FIELD(iV), RPP_FIELD(iP),
                 RPP_FIELD(stationary_frames), RPP_FIELD(samples_per_frame),
                 RPP_FIELD(enable_shading_normals), RPP_FIELD(sun));