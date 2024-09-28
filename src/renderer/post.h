
#pragma once

#include <rpp/base.h>
#include <rpp/vmath.h>
#include <rvk/rvk.h>

#include "pipeline.h"

using namespace rpp;

namespace Render {

enum class Tonemap : u8 {
    uncharted_2,
    unreal_tournament,
    exponential,
};

struct Post {
    using Layout = List<rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT>,
                        rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT>>;

    enum class Op : u32 {
        none,
        to_linear,
        to_srgb,
        tonemap_u2,
        tonemap_ut,
        tonemap_exp,
        tonemap_u2_srgb,
        tonemap_ut_srgb,
        tonemap_exp_srgb,
    };
    struct Constants {
        Op op = Op::none;
        u32 width = 0;
        u32 height = 0;
        f32 gamma = 2.2f;
        f32 exposure = 1.0f;
    };

    using Push = rvk::Push<Constants, VK_SHADER_STAGE_COMPUTE_BIT>;

    static rvk::Shader_Loader::Token reload(rvk::Shader_Loader& loader,
                                            Function<void(Pipeline&&)> callback);

    static void render(rvk::Commands& cmds, Pipeline& pipeline, const Constants& constants,
                       rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b1,
                       rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b2);
};

} // namespace Render

RPP_NAMED_RECORD(Render::Post::Constants, "Post::Constants", RPP_FIELD(op), RPP_FIELD(width),
                 RPP_FIELD(height), RPP_FIELD(gamma), RPP_FIELD(exposure));

RPP_ENUM(Render::Tonemap, exponential, RPP_CASE(uncharted_2), RPP_CASE(unreal_tournament),
         RPP_CASE(exponential));