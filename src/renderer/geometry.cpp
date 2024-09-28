
#include "renderer.h"

#include "geometry.h"
#include "pipeline.h"

#ifdef RPP_RELEASE_BUILD
#include "../shaders/precompiled.h"
#endif

namespace Render {

rvk::Shader_Loader::Token Geometry::reload(rvk::Descriptor_Set_Layout& scene,
                                           rvk::Shader_Loader& loader,
                                           Function<void(Pipeline&&)> callback) {

#ifdef RPP_RELEASE_BUILD
    auto geometry_rchit = loader.compile(Slice{geometry_rh_spv, geometry_rh_spv_len});
    auto geometry_rgen = loader.compile(Slice{geometry_rg_spv, geometry_rg_spv_len});
    auto geometry_rmiss = loader.compile(Slice{geometry_rm_spv, geometry_rm_spv_len});
    auto geometry_rahit = loader.compile(Slice{geometry_ra_spv, geometry_ra_spv_len});
#else
    auto geometry_rchit = loader.compile("shaders/geometry.rh.spv"_v);
    auto geometry_rgen = loader.compile("shaders/geometry.rg.spv"_v);
    auto geometry_rmiss = loader.compile("shaders/geometry.rm.spv"_v);
    auto geometry_rahit = loader.compile("shaders/geometry.ra.spv"_v);
#endif

    loader.on_reload(Slice{geometry_rchit, geometry_rgen, geometry_rmiss, geometry_rahit},
                     [&scene, geometry_rchit, geometry_rgen, geometry_rmiss, geometry_rahit,
                      callback = move(callback)](rvk::Shader_Loader& loader) mutable {
                         auto start = Profile::timestamp();

                         auto gen = Stage{Ref{loader.get(geometry_rgen)}, {}};
                         auto miss = Stage{Ref{loader.get(geometry_rmiss)}, {}};
                         auto chit = Stage{Ref{loader.get(geometry_rchit)}, {}};
                         auto ahit = Stage{Ref{loader.get(geometry_rahit)}, {}};

                         callback(make_rt_pipeline<Push, Layout>(scene, gen, miss, Slice{chit},
                                                                 Slice{Opt{ahit}}));

                         auto end = Profile::timestamp();
                         info("Recreated geometry pipeline in %ms.", Profile::ms(end - start));
                     });

    return geometry_rgen;
}

} // namespace Render