
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>
#include <rvk/rvk.h>

#include "pipeline.h"

using namespace rpp;

namespace Render {

struct Shading {
    enum class Mode : u32 {
        material_id,
        material_type,
        geometry_divergence,
        material_divergence,
        active_lanes,
        alpha_mask,
        texture0,
        texture1,
        texture2,
        texture3,
        texture4,
        texture5,
        texture6,
        texture7,
        texture8,
        texture9,
        texture10,
        texture11,
        param0,
        param1,
        param2,
        param3,
    };

    struct Constants {
        Mat4 iV, iP;
        Mode mode = Mode::material_id;
    };

    static constexpr GPU_Scene::Table_Type table_type = GPU_Scene::Table_Type::geometry_to_material;

    using Push =
        rvk::Push<Constants, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR>;

    using Layout = List<rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR>>;

    static rvk::Shader_Loader::Token reload(rvk::Descriptor_Set_Layout& scene,
                                            rvk::Shader_Loader& loader,
                                            Function<void(Pipeline&&)> callback);
};
} // namespace Render

RPP_NAMED_RECORD(Render::Shading::Constants, "Shading::Constants", RPP_FIELD(iV), RPP_FIELD(iP),
                 RPP_FIELD(mode));

RPP_NAMED_ENUM(Render::Shading::Mode, "Shading::Mode", material_id, RPP_CASE(material_id),
               RPP_CASE(material_type), RPP_CASE(geometry_divergence),
               RPP_CASE(material_divergence), RPP_CASE(active_lanes), RPP_CASE(alpha_mask),
               RPP_CASE(texture0), RPP_CASE(texture1), RPP_CASE(texture2), RPP_CASE(texture3),
               RPP_CASE(texture4), RPP_CASE(texture5), RPP_CASE(texture6), RPP_CASE(texture7),
               RPP_CASE(texture8), RPP_CASE(texture9), RPP_CASE(texture10), RPP_CASE(texture11),
               RPP_CASE(param0), RPP_CASE(param1), RPP_CASE(param2), RPP_CASE(param3));