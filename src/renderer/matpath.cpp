
#include "renderer.h"

#include "matpath.h"
#include "pipeline.h"

#ifdef RPP_RELEASE_BUILD
#include "../shaders/precompiled.h"
#endif

namespace Render {

rvk::Shader_Loader::Token MatPath::reload(rvk::Descriptor_Set_Layout& scene,
                                          rvk::Shader_Loader& loader,
                                          Function<void(Pipeline&&)> callback) {

#ifdef RPP_RELEASE_BUILD
    auto mp_rchit = loader.compile(Slice{mp_rh_spv, mp_rh_spv_len});
    auto mp_rgen = loader.compile(Slice{mp_rg_spv, mp_rg_spv_len});
    auto mp_rmiss = loader.compile(Slice{mp_rm_spv, mp_rm_spv_len});
    auto mp_rahit = loader.compile(Slice{mp_ra_spv, mp_ra_spv_len});
#else
    auto mp_rchit = loader.compile("shaders/mp.rh.spv"_v);
    auto mp_rgen = loader.compile("shaders/mp.rg.spv"_v);
    auto mp_rmiss = loader.compile("shaders/mp.rm.spv"_v);
    auto mp_rahit = loader.compile("shaders/mp.ra.spv"_v);
#endif

    loader.on_reload(
        Slice{{mp_rchit, mp_rgen, mp_rmiss, mp_rahit}},
        [&scene, mp_rchit, mp_rgen, mp_rmiss, mp_rahit,
         callback = move(callback)](rvk::Shader_Loader& loader) mutable {
            auto start = Profile::timestamp();

            auto gen = Stage{Ref{loader.get(mp_rgen)}};
            auto miss = Stage{Ref{loader.get(mp_rmiss)}};

            Region(R) {
                using Type = GPU_Scene::Material_Type;
                constexpr u64 count = Reflect::List_Length<Reflect::Refl<Type>::members>;

                auto& chit_module = loader.get(mp_rchit);
                auto& ahit_module = loader.get(mp_rahit);

                Vec<Stage, Mregion<R>> ch_stages(count);
                Vec<Opt<Stage>, Mregion<R>> ah_stages(count);
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
                        ch_stages.push(Stage{Ref{chit_module}, VkSpecializationInfo{
                                                                   .mapEntryCount = 1,
                                                                   .pMapEntries = &entry,
                                                                   .dataSize = sizeof(u32),
                                                                   .pData = &data.back(),
                                                               }});
                        ah_stages.push(Opt{Stage{Ref{ahit_module}}});
                    });

                callback(make_rt_pipeline<Push, Layout>(scene, gen, miss, ch_stages.slice(),
                                                        ah_stages.slice()));
            }

            auto end = Profile::timestamp();
            info("Recreated material path pipeline in %ms.", Profile::ms(end - start));
        });

    return mp_rgen;
}

} // namespace Render
