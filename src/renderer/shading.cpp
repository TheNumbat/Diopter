
#include "renderer.h"

#include "pipeline.h"

#ifdef RPP_RELEASE_BUILD
#include "../shaders/precompiled.h"
#endif

namespace Render {

rvk::Shader_Loader::Token Shading::reload(rvk::Descriptor_Set_Layout& scene,
                                          rvk::Shader_Loader& loader,
                                          Function<void(Pipeline&&)> callback) {

#ifdef RPP_RELEASE_BUILD
    auto shading_rchit = loader.compile(Slice{shading_rh_spv, shading_rh_spv_len});
    auto shading_rgen = loader.compile(Slice{shading_rg_spv, shading_rg_spv_len});
    auto shading_rmiss = loader.compile(Slice{shading_rm_spv, shading_rm_spv_len});
#else
    auto shading_rchit = loader.compile("shaders/shading.rh.spv"_v);
    auto shading_rgen = loader.compile("shaders/shading.rg.spv"_v);
    auto shading_rmiss = loader.compile("shaders/shading.rm.spv"_v);
#endif

    loader.on_reload(
        Slice{shading_rchit, shading_rgen, shading_rmiss},
        [&scene, shading_rchit, shading_rgen, shading_rmiss,
         callback = move(callback)](rvk::Shader_Loader& loader) mutable {
            auto start = Profile::timestamp();

            auto gen = Stage{Ref{loader.get(shading_rgen)}, {}};
            auto miss = Stage{Ref{loader.get(shading_rmiss)}, {}};

            Region(R) {
                using Type = GPU_Scene::Material_Type;
                constexpr u64 count = Reflect::List_Length<Reflect::Refl<Type>::members>;

                auto& hit_module = loader.get(shading_rchit);

                Vec<Stage, Mregion<R>> stages(count);
                Vec<u32, Mregion<R>> data(count);

                VkSpecializationMapEntry entry{
                    .constantID = 0,
                    .offset = 0,
                    .size = sizeof(u32),
                };

                Reflect::iterate_enum<GPU_Scene::Material_Type>(
                    [&](const Literal&, const GPU_Scene::Material_Type& material_type) {
                        u32 index = static_cast<u32>(material_type);
                        data.push(index);
                        stages.push(Stage{Ref{hit_module}, VkSpecializationInfo{
                                                               .mapEntryCount = 1,
                                                               .pMapEntries = &entry,
                                                               .dataSize = sizeof(u32),
                                                               .pData = &data.back(),
                                                           }});
                    });

                callback(make_rt_pipeline<Push, Layout>(scene, gen, miss, stages.slice()));
            }

            auto end = Profile::timestamp();
            info("Recreated shading pipeline in %ms.", Profile::ms(end - start));
        });

    return shading_rgen;
}

} // namespace Render