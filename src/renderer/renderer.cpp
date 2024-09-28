
#include <imgui/imgui.h>
#include <nfd/nfd.h>
#include <stb/stb_image_write.h>

#include "../gui/imgui_ext.h"
#include "../scene/gltf.h"
#include "../scene/pbrt.h"

#include "renderer.h"

static void trace_compute_barrier(rvk::Commands& cmds) {

    auto barriers = Array<VkMemoryBarrier2, 2>{
        VkMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        },
        VkMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            .dstAccessMask =
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = static_cast<u32>(barriers.length()),
        .pMemoryBarriers = barriers.data(),
    };

    vkCmdPipelineBarrier2(cmds, &dep);
}

template<typename T>
Async::Task<rvk::Shader_Loader::Token> Renderer::make_pipeline(Render::Pipeline& old_pipeline,
                                                               rvk::Binding_Table& old_table) {
    co_await pool.suspend();
    co_return T::reload(scene.layout(), *shaders, [&](Render::Pipeline&& new_pipeline) {
        rvk::drop([old_pipeline = Box<Render::Pipeline, rvk::Alloc>{move(old_pipeline)}]() {});
        rvk::drop([old_table = Box<rvk::Binding_Table, rvk::Alloc>{move(old_table)}]() {});

        old_pipeline = move(new_pipeline);
        rvk::sync([&](rvk::Commands& cmds) {
            old_table = scene.table(T::table_type, cmds, old_pipeline.pipeline);
        });

        stationary_frames = 0;
        needs_reset = true;
    });
}

static Async::Task<void> reload_pipeline(Async::Pool<>& pool, rvk::Shader_Loader& shaders,
                                         rvk::Shader_Loader::Token token) {
    co_await pool.suspend();
    shaders.trigger(token);
}

Renderer::Renderer(Async::Pool<>& pool) : pool(pool), shaders(rvk::make_shader_loader()) {

    auto g_task = make_pipeline<Render::Geometry>(geometry, geometry_table);
    auto ao_task = make_pipeline<Render::AO>(ambient_occlusion, ao_table);
    auto s_task = make_pipeline<Render::Shading>(shading, shading_table);
    auto mp_task = make_pipeline<Render::MatPath>(material_path, material_path_table);

    geometry_token = g_task.block();
    ao_token = ao_task.block();
    shading_token = s_task.block();
    material_path_token = mp_task.block();

    post_token = Render::Post::reload(*shaders, [&](Render::Pipeline&& new_pipeline) {
        rvk::drop([post_process = Box<Render::Pipeline, rvk::Alloc>{move(post_process)}]() {});
        post_process = move(new_pipeline);
    });

    rebuild_frames();
}

Renderer::~Renderer() {
    if(loading_scene.ok()) static_cast<void>(loading_scene.block());

    rvk::drop([geometry = Box<Render::Pipeline, rvk::Alloc>{move(geometry)}]() {});
    rvk::drop([geometry_table = Box<rvk::Binding_Table, rvk::Alloc>{move(geometry_table)}]() {});

    rvk::drop(
        [ambient_occlusion = Box<Render::Pipeline, rvk::Alloc>{move(ambient_occlusion)}]() {});
    rvk::drop([ao_table = Box<rvk::Binding_Table, rvk::Alloc>{move(ao_table)}]() {});

    rvk::drop([shading = Box<Render::Pipeline, rvk::Alloc>{move(shading)}]() {});
    rvk::drop([shading_table = Box<rvk::Binding_Table, rvk::Alloc>{move(shading_table)}]() {});

    rvk::drop([material_path = Box<Render::Pipeline, rvk::Alloc>{move(material_path)}]() {});
    rvk::drop([material_path_table =
                   Box<rvk::Binding_Table, rvk::Alloc>{move(material_path_table)}]() {});

    rvk::drop([post_process = Box<Render::Pipeline, rvk::Alloc>{move(post_process)}]() {});

    rvk::drop([frames = move(frames)]() {});
    rvk::drop([scene = Box<GPU_Scene::Scene, rvk::Alloc>{move(scene)}]() {});
    rvk::drop([shaders = Box<rvk::Shader_Loader, rvk::Alloc>{move(shaders)}]() {});
}

void Renderer::rebuild_binding_tables() {
    rvk::drop([geometry_table = Box<rvk::Binding_Table, rvk::Alloc>{move(geometry_table)}]() {});
    rvk::drop([ao_table = Box<rvk::Binding_Table, rvk::Alloc>{move(ao_table)}]() {});
    rvk::drop([shading_table = Box<rvk::Binding_Table, rvk::Alloc>{move(shading_table)}]() {});
    rvk::drop([material_path_table =
                   Box<rvk::Binding_Table, rvk::Alloc>{move(material_path_table)}]() {});

    rvk::sync([&](rvk::Commands& cmds) {
        geometry_table = scene.table(Render::Geometry::table_type, cmds, geometry.pipeline);
        ao_table = scene.table(Render::AO::table_type, cmds, ambient_occlusion.pipeline);
        shading_table = scene.table(Render::Shading::table_type, cmds, shading.pipeline);
        material_path_table =
            scene.table(Render::MatPath::table_type, cmds, material_path.pipeline);
    });
}

void Renderer::rebuild_pipelines() {
    auto g_task = reload_pipeline(pool, *shaders, geometry_token);
    auto ao_task = reload_pipeline(pool, *shaders, ao_token);
    auto s_task = reload_pipeline(pool, *shaders, shading_token);
    auto mp_task = reload_pipeline(pool, *shaders, material_path_token);
    auto p_task = reload_pipeline(pool, *shaders, post_token);
    g_task.block();
    ao_task.block();
    s_task.block();
    mp_task.block();
    p_task.block();
    rebuild_binding_tables();
}

void Renderer::render(Camera& cam) {
    shaders->try_reload();

    Mat4 iview = cam.iview();
    Mat4 iproj = cam.iproj();

    if(!accumulate || needs_reset || iview != current_iview || iproj != current_iproj) {
        current_iview = iview;
        current_iproj = iproj;
        stationary_frames = 0;
        needs_reset = false;
    } else {
        stationary_frames++;
    }
    if(stationary_frames >= static_cast<u32>(max_stationary_frames)) {
        stationary_frames = static_cast<u32>(max_stationary_frames);
    }

    Frame& frame = frames[rvk::frame()];
    Frame& prev = frames[rvk::frame() ? rvk::frame() - 1 : rvk::frame_count() - 1];

    auto& cmds = frame.frame_cmds;
    cmds.reset();

    switch(integrator) {
    case Integrator::geometry: {
        using namespace Render;
        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b0{frame.trace_view};
        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b1{prev.trace_view};

        run_pipeline<Geometry::Push, Geometry::Layout>(
            cmds, scene, geometry, geometry_table,
            Geometry::Constants{iview, iproj, geometry_mode, stationary_frames}, b0, b1);
    } break;

    case Integrator::ambient_occlusion: {
        using namespace Render;
        if(stationary_frames < static_cast<u32>(max_stationary_frames)) {
            rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b0{frame.trace_view};
            rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b1{prev.trace_view};

            run_pipeline<AO::Push, AO::Layout>(cmds, scene, ambient_occlusion, ao_table,
                                               AO::Constants{iview, iproj, stationary_frames,
                                                             samples_per_frame, shading_normals,
                                                             sun},
                                               b0, b1);
        }
    } break;

    case Integrator::material_path: {
        using namespace Render;
        if(stationary_frames < static_cast<u32>(max_stationary_frames)) {
            rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b0{frame.trace_view};
            rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b1{prev.trace_view};

            run_pipeline<MatPath::Push, MatPath::Layout>(
                cmds, scene, material_path, material_path_table,
                MatPath::Constants{iview, iproj, stationary_frames, samples_per_frame, max_depth,
                                   roulette, suppress_fireflies, shading_normals,
                                   scene.has_environment_map() ? -1.0f : sun},
                b0, b1);
        }
    } break;

    case Integrator::shading: {
        using namespace Render;
        rvk::Bind::Image_Storage<VK_SHADER_STAGE_RAYGEN_BIT_KHR> b0{frame.trace_view};

        run_pipeline<Shading::Push, Shading::Layout>(cmds, scene, shading, shading_table,
                                                     Shading::Constants{iview, iproj, shading_mode},
                                                     b0);
    } break;

    default: RPP_UNREACHABLE;
    }

    trace_compute_barrier(cmds);

    frame.post.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    {
        using namespace Render;
        rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b1{frame.trace_view};
        rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b2{frame.post_view};

        auto extent = rvk::extent();
        Post::render(
            cmds, post_process,
            Post::Constants{postprocess_op(false), extent.width, extent.height, gamma, exposure},
            b1, b2);
    }

    frame.post.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    cmds.end();
    rvk::submit(cmds, 0);
}

void Renderer::on_resize() {
    needs_reset = true;
    rebuild_frames();
}

rvk::Image_View& Renderer::output() {
    return frames[rvk::frame()].post_view;
}

void Renderer::rebuild_frames() {
    VkExtent2D extent = rvk::extent();

    rvk::drop([frames = move(frames)]() {});

    for(u32 i = 0; i < rvk::frame_count(); i++) {
        auto trace =
            move(*rvk::make_image({extent.width, extent.height, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT));

        auto post =
            move(*rvk::make_image({extent.width, extent.height, 1}, VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT));

        auto trace_view = trace.view(VK_IMAGE_ASPECT_COLOR_BIT);
        auto post_view = post.view(VK_IMAGE_ASPECT_COLOR_BIT);

        Frame f{
            .trace = move(trace),
            .post = move(post),
            .trace_view = move(trace_view),
            .post_view = move(post_view),
            .frame_cmds = rvk::make_commands(),
        };
        frames.push(move(f));
    }

    rvk::sync([&](rvk::Commands& cmds) {
        for(Frame& f : frames) {
            f.trace.setup(cmds, VK_IMAGE_LAYOUT_GENERAL);
            f.post.setup(cmds, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });
}

void Renderer::gui() {
    using namespace ImGui;
    Indent();

    if(Button("Save Image")) {
        saving_image = save_image();
    }
    if(saving_image.ok() && saving_image.done()) {
        saving_image = {};
    }

    Checkbox("##accumulate", &accumulate);
    SameLine();
    Text("Stationary frames: %d", stationary_frames);
    InputInt("Max frames", &max_stationary_frames, 128, 1024);

    {
        Integrator prev = integrator;
        Combo("Integrator", integrator);
        if(prev != integrator) {
            needs_reset = true;
        }
    }

    if(integrator == Integrator::geometry) {
        Render::Geometry::Mode prev = geometry_mode;
        if(Combo("Geometry", geometry_mode)) {
            if(prev != geometry_mode) {
                needs_reset = true;
            }
        }
    }
    if(integrator == Integrator::shading) {
        Render::Shading::Mode prev = shading_mode;
        if(Combo("Shading", shading_mode)) {
            if(prev != shading_mode) {
                needs_reset = true;
            }
        }
    }

    if(Checkbox("HDR", &hdr)) {
        rvk::hdr(hdr);
        needs_reset = true;
    }
    if(Checkbox("Shading Normals", &shading_normals)) {
        needs_reset = true;
    }
    if(Checkbox("Roulette", &roulette)) {
        needs_reset = true;
    }
    Combo("Tonemap", tonemap);
    if(SliderU32("Samples/Frame", &samples_per_frame, 1, 32)) {
        needs_reset = true;
    }
    if(SliderU32("Suppress Fireflies", &suppress_fireflies, 0, 512)) {
        needs_reset = true;
    }
    if(SliderU32("Max Depth", &max_depth, 1, 32)) {
        needs_reset = true;
    }
    SliderFloat("Gamma", &gamma, 1.0f, 3.0f);
    SliderFloat("Exposure", &exposure, 0.01f, 10.0f);
    if(SliderFloat("Sun", &sun, 0.0f, 10.0f)) {
        needs_reset = true;
    }

    Unindent();
}

Render::Post::Op Renderer::postprocess_op(bool srgb) {
    switch(integrator) {
    case Integrator::geometry:
    case Integrator::shading: {
        return srgb ? Render::Post::Op::none : Render::Post::Op::to_linear;
    } break;
    case Integrator::ambient_occlusion:
    case Integrator::material_path: {
        if(hdr) {
            return srgb ? Render::Post::Op::to_srgb : Render::Post::Op::none;
        } else if(tonemap == Render::Tonemap::uncharted_2) {
            return srgb ? Render::Post::Op::tonemap_u2_srgb : Render::Post::Op::tonemap_u2;
        } else if(tonemap == Render::Tonemap::unreal_tournament) {
            return srgb ? Render::Post::Op::tonemap_ut_srgb : Render::Post::Op::tonemap_ut;
        } else if(tonemap == Render::Tonemap::exponential) {
            return srgb ? Render::Post::Op::tonemap_exp_srgb : Render::Post::Op::tonemap_exp;
        }
    } break;
    }
    RPP_UNREACHABLE;
}

Async::Task<void> Renderer::save_image() {
    co_await pool.suspend();

    char* path = null;
    NFD_SaveDialog(IMAGE_OUTPUT_FILE_TYPES, null, &path);

    String_View path_view{path};
    String<rvk::Alloc> save_to;

    if(path_view.file_extension() != "png"_v) {
        save_to = path_view.append<rvk::Alloc>(".png\00"_v);
    } else {
        save_to = path_view.terminate<rvk::Alloc>();
    }

    Libc::free(path);

    auto& frame = frames[rvk::frame()];

    auto extent = frame.trace.extent();

    auto image_ = rvk::make_image(extent, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    if(!image_.ok()) co_return;
    auto image = move(*image_);
    auto image_view = image.view(VK_IMAGE_ASPECT_COLOR_BIT);

    auto buffer_ = rvk::make_staging(image.linear_size());
    if(!buffer_.ok()) co_return;
    auto buffer = move(*buffer_);

    rvk::Descriptor_Set set = rvk::make_single_set(post_process.layout);
    rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b1{frame.trace_view};
    rvk::Bind::Image_Storage<VK_SHADER_STAGE_COMPUTE_BIT> b2{image_view};
    rvk::write_set<Render::Post::Layout>(set, 0, b1, b2);

    co_await rvk::async(pool, [&](rvk::Commands& cmds) {
        image.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0, VK_ACCESS_2_SHADER_WRITE_BIT);

        Render::Post::Constants push{postprocess_op(true), extent.width, extent.height, gamma,
                                     exposure};

        post_process.pipeline.bind(cmds);
        post_process.pipeline.bind_set(cmds, set, 0, 0);
        post_process.pipeline.push<Render::Post::Push>(cmds, push);

        vkCmdDispatch(cmds, (push.width + 7) / 8, (push.height + 7) / 8, 1);

        image.transition(cmds, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        image.to_buffer(cmds, buffer);
    });

    stbi_write_png(reinterpret_cast<const char*>(save_to.data()), extent.width, extent.height, 4,
                   buffer.map(), 0);
}

Async::Task<GPU_Scene::Scene> Renderer::load_scene_gltf(String_View path_) {
    auto path = path_.string<PBRT::Alloc>();

    Profile::Time_Point started_load = Profile::timestamp();
    auto cpu_scene = co_await GLTF::load(pool, path.view());
    Profile::Time_Point finished_load = Profile::timestamp();
    info("Loaded scene from disk in %ms.", Profile::ms(finished_load - started_load));

    Profile::Time_Point started_upload = Profile::timestamp();
    auto gpu_scene = co_await GPU_Scene::load(pool, cpu_scene, max_parallelism);
    Profile::Time_Point finished_upload = Profile::timestamp();
    info("Uploaded scene to GPU in %ms.", Profile::ms(finished_upload - started_upload));

    info("Scene loaded in in %ms.", Profile::ms(finished_upload - started_load));

    co_return gpu_scene;
}

Async::Task<GPU_Scene::Scene> Renderer::load_scene_pbrt(String_View path_) {
    auto path = path_.string<PBRT::Alloc>();

    Profile::Time_Point started_load = Profile::timestamp();
    auto cpu_scene = co_await PBRT::load(pool, path.view());
    Profile::Time_Point finished_load = Profile::timestamp();
    info("Loaded scene from disk in %ms.", Profile::ms(finished_load - started_load));

    Profile::Time_Point started_upload = Profile::timestamp();
    auto gpu_scene = co_await GPU_Scene::load(pool, cpu_scene, max_parallelism);
    Profile::Time_Point finished_upload = Profile::timestamp();
    info("Uploaded scene to GPU in %ms.", Profile::ms(finished_upload - started_upload));

    info("Scene loaded in in %ms.", Profile::ms(finished_upload - started_load));

    co_return gpu_scene;
}

Async::Task<GPU_Scene::Scene> Renderer::load_scene_open() {
    co_await pool.suspend();

    char* path = null;
    NFD_OpenDialog(SCENE_FILE_TYPES, null, &path);
    if(path) {
        String_View file{path};
        String_View extension = file.file_extension();

        Async::Task<GPU_Scene::Scene> loading;

        if(extension == "pbrt"_v) {
            loading = load_scene_pbrt(file);
        } else if(extension == "gltf"_v || extension == "glb"_v) {
            loading = load_scene_gltf(file);
        } else {
            warn("Unknown scene file type %.", extension);
            Libc::free(path);
            co_return {};
        }

        auto ret = co_await loading;
        Libc::free(path);
        co_return ret;
    }

    co_return {};
}

void Renderer::pick_scene(Camera& cam) {
    if(loading_scene.ok() && loading_scene.done()) {
        rvk::drop([scene = Box<GPU_Scene::Scene, rvk::Alloc>{move(scene)}]() {});
        scene = loading_scene.block();
        cam.set_pos(Vec3{});
        loading_scene = {};
        needs_reset = true;
        rebuild_binding_tables();
    }

    using namespace ImGui;
    Indent();

    if(Button("Open")) {
        loading_scene = load_scene_open();
    }
    SameLine();
    if(Button("Clear")) {
        rvk::drop([scene = Box<GPU_Scene::Scene, rvk::Alloc>{move(scene)}]() {});
        scene = {};
        needs_reset = true;
        rebuild_binding_tables();
    }
    SameLine();
    PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
    SliderInt("Parallelism", &max_parallelism, 1, 16);
    PopItemWidth();

#ifndef RPP_RELEASE_BUILD
#define LOAD(name, folder, speed)                                                                  \
    if(Button(name)) {                                                                             \
        loading_scene = load_scene_pbrt(String_View{"pbrt-scenes/" folder});                       \
        cam.set_speed(speed);                                                                      \
    }

    LOAD("Bathroom", "bathroom/scene-v4.pbrt", 1.0f);
    SameLine();
    LOAD("Bathroom 2", "bathroom2/scene-v4.pbrt", 25.0f);
    SameLine();
    LOAD("Bedroom", "bedroom/scene-v4.pbrt", 2.0f);
    SameLine();
    LOAD("Car", "car/scene-v4.pbrt", 8.0f);

    LOAD("Car 2", "car2/scene-v4.pbrt", 8.0f);
    SameLine();
    LOAD("Classroom", "classroom/scene-v4.pbrt", 3.0f);
    SameLine();
    LOAD("Coffee", "coffee/scene-v4.pbrt", 0.75f);
    SameLine();
    LOAD("Cornell Box", "cornell-box/scene-v4.pbrt", 5.0f);

    LOAD("Dining Room", "dining-room/scene-v4.pbrt", 5.0f);
    SameLine();
    LOAD("Dragon", "dragon/scene-v4.pbrt", 100.0f);
    SameLine();
    LOAD("Glass of Water", "glass-of-water/scene-v4.pbrt", 10.0f);
    SameLine();
    LOAD("House", "house/scene-v4.pbrt", 20.0f);

    LOAD("Kitchen", "kitchen/scene-v4.pbrt", 3.0f);
    SameLine();
    LOAD("Lamp", "lamp/scene-v4.pbrt", 5.0f);
    SameLine();
    LOAD("Room", "living-room/scene-v4.pbrt", 2.0f);
    SameLine();
    LOAD("Room 2", "living-room-2/scene-v4.pbrt", 2.0f);

    LOAD("Room 3", "living-room-3/scene-v4.pbrt", 2.0f);
    SameLine();
    LOAD("Spaceship", "spaceship/scene-v4.pbrt", 1.0f);
    SameLine();
    LOAD("Staircase", "staircase/scene-v4.pbrt", 4.0f);
    SameLine();
    LOAD("Staircase 2", "staircase2/scene-v4.pbrt", 5.0f);

    LOAD("Teapot", "teapot/scene-v4.pbrt", 10.0f);
    SameLine();
    LOAD("Veach Ajar", "veach-ajar/scene-v4.pbrt", 5.0f);
    SameLine();
    LOAD("Veach Bidir", "veach-bidir/scene-v4.pbrt", 8.0f);
    SameLine();
    LOAD("Veach MIS", "veach-mis/scene-v4.pbrt", 8.0f);

    LOAD("Pavilion", "barcelona-pavilion/pavilion-day.pbrt", 9.0f);
    SameLine();
    LOAD("Bistro", "bistro/bistro_vespa.pbrt", 10.0f);
    SameLine();
    LOAD("BMW M6", "bmw-m6/bmw-m6.pbrt", 5.0f);
    SameLine();
    LOAD("Crown", "crown/crown.pbrt", 10.0f);

    LOAD("Dambreak", "dambreak/dambreak0.pbrt", 40.0f);
    SameLine();
    LOAD("Ganesha", "ganesha/ganesha.pbrt", 1.0f);
    SameLine();
    LOAD("Kroken", "kroken/camera-1.pbrt", 400.0f);

    LOAD("Landscape", "landscape/view-0.pbrt", 600.0f);
    SameLine();
    LOAD("PBRT Book", "pbrt-book/book.pbrt", 3.0f);
    SameLine();
    LOAD("San Miguel", "sanmiguel/sanmiguel-entry.pbrt", 6.0f);
    SameLine();
    LOAD("SSS Dragon", "sssdragon/dragon_10.pbrt", 3.0f);

    LOAD("Machines", "transparent-machines/frame542.pbrt", 200.0f);
    SameLine();
    LOAD("Villa", "villa/villa-daylight.pbrt", 5.0f);
    SameLine();
    LOAD("Watercolor", "watercolor/camera-1.pbrt", 400.0f);
    SameLine();
    LOAD("Zero Day", "zero-day/frame25.pbrt", 350.0f);

#undef LOAD
#endif

    Unindent();
}
