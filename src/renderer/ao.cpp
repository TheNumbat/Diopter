
#include "renderer.h"

#include "ao.h"
#include "pipeline.h"

#ifdef RPP_RELEASE_BUILD
#include "../shaders/precompiled.h"
#endif

namespace Render {

rvk::Shader_Loader::Token AO::reload(rvk::Descriptor_Set_Layout& scene, rvk::Shader_Loader& loader,
                                     Function<void(Pipeline&&)> callback) {

#ifdef RPP_RELEASE_BUILD
    auto ao_rchit = loader.compile(Slice{ao_rh_spv, ao_rh_spv_len});
    auto ao_rgen = loader.compile(Slice{ao_rg_spv, ao_rg_spv_len});
    auto ao_rmiss = loader.compile(Slice{ao_rm_spv, ao_rm_spv_len});
    auto ao_rahit = loader.compile(Slice{ao_ra_spv, ao_ra_spv_len});
#else
    auto ao_rchit = loader.compile("shaders/ao.rh.spv"_v);
    auto ao_rgen = loader.compile("shaders/ao.rg.spv"_v);
    auto ao_rmiss = loader.compile("shaders/ao.rm.spv"_v);
    auto ao_rahit = loader.compile("shaders/ao.ra.spv"_v);
#endif

    loader.on_reload(Slice{{ao_rchit, ao_rgen, ao_rmiss, ao_rahit}},
                     [&scene, ao_rchit, ao_rgen, ao_rmiss, ao_rahit,
                      callback = move(callback)](rvk::Shader_Loader& loader) mutable {
                         auto start = Profile::timestamp();

                         auto gen = Stage{Ref{loader.get(ao_rgen)}, {}};
                         auto miss = Stage{Ref{loader.get(ao_rmiss)}, {}};
                         auto chit = Stage{Ref{loader.get(ao_rchit)}, {}};
                         auto ahit = Stage{Ref{loader.get(ao_rahit)}, {}};

                         callback(make_rt_pipeline<Push, Layout>(scene, gen, miss, Slice{chit},
                                                                 Slice{Opt{ahit}}));

                         auto end = Profile::timestamp();
                         info("Recreated ao pipeline in %ms.", Profile::ms(end - start));
                     });

    return ao_rgen;
}

} // namespace Render
