
#pragma once

#include <rpp/base.h>
#include <rvk/rvk.h>

#include "../scene/gpu_scene.h"

using namespace rpp;

namespace Render {

struct Pipeline {
    rvk::Pipeline pipeline;
    rvk::Descriptor_Set set;
    rvk::Descriptor_Set_Layout layout;
};

struct Stage {
    Ref<rvk::Shader> shader;
    VkSpecializationInfo specialization;
};

inline void transfer_trace_barrier(rvk::Commands& cmds) {

    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(cmds, &dep);
}

template<rvk::Push_Constant Push, Type_List Layout>
    requires(Reflect::All<rvk::Is_Binding, Layout>)
Pipeline make_rt_pipeline(rvk::Descriptor_Set_Layout& scene, Stage& gen, Stage& miss,
                          Slice<const Stage> closest_hits, Slice<const Opt<Stage>> any_hits = {}) {

    assert(any_hits.empty() || any_hits.length() == closest_hits.length());
    u64 total_shaders = 2 + closest_hits.length() + any_hits.length();

    Region(R) {
        Vec<VkRayTracingShaderGroupCreateInfoKHR, Mregion<R>> groups(total_shaders);

        groups.push(VkRayTracingShaderGroupCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 0,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        });

        groups.push(VkRayTracingShaderGroupCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 1,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        });

        u64 any_hit_count = 0;

        for(u64 i = 0; i < closest_hits.length(); i++) {
            groups.push(VkRayTracingShaderGroupCreateInfoKHR{
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
                .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
                .generalShader = VK_SHADER_UNUSED_KHR,
                .closestHitShader = static_cast<u32>(2 + i),
                .anyHitShader = !any_hits.empty() && any_hits[i].ok()
                                    ? static_cast<u32>(2 + closest_hits.length() + any_hit_count++)
                                    : VK_SHADER_UNUSED_KHR,
                .intersectionShader = VK_SHADER_UNUSED_KHR,
            });
        }

        Vec<VkPipelineShaderStageCreateInfo, Mregion<R>> stages(total_shaders);

        stages.push(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = *gen.shader,
            .pName = "main",
            .pSpecializationInfo = &gen.specialization,
        });

        stages.push(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = *miss.shader,
            .pName = "main",
            .pSpecializationInfo = &miss.specialization,
        });

        for(u64 i = 0; i < closest_hits.length(); i++) {
            stages.push(VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                .module = *closest_hits[i].shader,
                .pName = "main",
                .pSpecializationInfo = &closest_hits[i].specialization,
            });
        }

        for(u64 i = 0; i < any_hits.length(); i++) {
            if(any_hits[i].ok()) {
                stages.push(VkPipelineShaderStageCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
                    .module = *any_hits[i]->shader,
                    .pName = "main",
                    .pSpecializationInfo = &any_hits[i]->specialization,
                });
            }
        }

        VkRayTracingPipelineCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .stageCount = static_cast<u32>(stages.length()),
            .pStages = stages.data(),
            .groupCount = static_cast<u32>(groups.length()),
            .pGroups = groups.data(),
            .maxPipelineRayRecursionDepth = 1,
        };

        auto layout = rvk::make_layout<Layout>();
        auto set = rvk::make_set(layout);

        Array<Ref<rvk::Descriptor_Set_Layout>, 2> layouts{Ref{scene}, Ref{layout}};

        auto pipe = rvk::make_pipeline(rvk::Pipeline::Info{
            .push_constants = Slice{Push::range},
            .descriptor_set_layouts = layouts.slice(),
            .info = move(create_info),
        });

        return Pipeline{move(pipe), move(set), move(layout)};
    }
}

template<rvk::Push_Constant Push, Type_List Layout, rvk::Binding... Binds>
    requires(Same<Layout, List<Binds...>>)
void run_pipeline(rvk::Commands& cmds, GPU_Scene::Scene& scene, Pipeline& pipeline,
                  rvk::Binding_Table& table, const typename Push::T& push, Binds&... binds) {

    rvk::write_set<Layout>(pipeline.set, binds...);

    pipeline.pipeline.bind(cmds);
    pipeline.pipeline.bind_set(cmds, scene.set(), 0);
    pipeline.pipeline.bind_set(cmds, pipeline.set, 1);
    pipeline.pipeline.push<Push>(cmds, push);

    auto ext = rvk::extent();
    Region(R) {
        auto regions = table.regions<R>();
        vkCmdTraceRaysKHR(cmds, &regions[0], &regions[1], &regions[2], &regions[3], ext.width,
                          ext.height, 1);
    }
}

} // namespace Render
