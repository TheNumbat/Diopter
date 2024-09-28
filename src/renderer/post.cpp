
#include "renderer.h"

#include "pipeline.h"
#include "post.h"

#ifdef RPP_RELEASE_BUILD
#include "../shaders/precompiled.h"
#endif

namespace Render {

Pipeline make_pipeline(Stage& p) {

    rvk::Descriptor_Set_Layout layout = rvk::make_layout<Post::Layout>();

    VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = *p.shader,
                .pName = "main",
            },
    };

    rvk::Pipeline pipe = rvk::make_pipeline(rvk::Pipeline::Info{
        .push_constants = Slice{Post::Push::range},
        .descriptor_set_layouts = Slice{Ref{layout}},
        .info = move(info),
    });

    return Pipeline{move(pipe), rvk::make_set(layout), move(layout)};
}

rvk::Shader_Loader::Token Post::reload(rvk::Shader_Loader& loader,
                                       Function<void(Pipeline&&)> callback) {

#ifdef RPP_RELEASE_BUILD
    auto post = loader.compile(Slice{post_spv, post_spv_len});
#else
    auto post = loader.compile("shaders/post.spv"_v);
#endif

    loader.on_reload(
        Slice{post}, [post, callback = move(callback)](rvk::Shader_Loader& loader) mutable {
            auto start = Profile::timestamp();

            auto p = Stage{Ref{loader.get(post)}, {}};
            callback(make_pipeline(p));

            auto end = Profile::timestamp();
            info("Recreated post-processing pipeline in %ms.", Profile::ms(end - start));
        });

    return post;
}

void Post::render(rvk::Commands& cmds, Pipeline& pipeline, const Constants& push,
                  rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b1,
                  rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b2) {

    rvk::write_set<Layout>(pipeline.set, b1, b2);

    pipeline.pipeline.bind(cmds);
    pipeline.pipeline.bind_set(cmds, pipeline.set, 0);
    pipeline.pipeline.push<Push>(cmds, push);

    vkCmdDispatch(cmds, (push.width + 7) / 8, (push.height + 7) / 8, 1);
}

} // namespace Render
