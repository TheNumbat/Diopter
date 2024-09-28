
#pragma once

#include <rpp/base.h>
#include <rpp/thread.h>
#include <rvk/rvk.h>

#include "../scene/gpu_scene.h"
#include "../util/camera.h"

#include "pipeline.h"

#include "ao.h"
#include "geometry.h"
#include "matpath.h"
#include "post.h"
#include "shading.h"

enum Integrator : u8 {
    geometry,
    shading,
    ambient_occlusion,
    material_path,
};

constexpr Literal SCENE_FILE_TYPES = "pbrt,gltf,glb";
constexpr Literal IMAGE_OUTPUT_FILE_TYPES = "png";

struct Renderer {

    Renderer(Async::Pool<>& pool);
    ~Renderer();

    void render(Camera& cam);
    void on_resize();
    rvk::Image_View& output();

    void gui();
    void pick_scene(Camera& cam);

private:
    Async::Pool<>& pool;

    struct Frame {
        // This data should _not_ be recreated each frame. Any per-frame data (i.e. the TLAS)
        // should be setup/used and immediately dropped. Its lifetime will be extended until the
        // CPU returns to the frame slot and waits for completion.
        rvk::Image trace, post;
        rvk::Image_View trace_view, post_view;
        rvk::Commands frame_cmds;
    };

    // Vulkan data

    Vec<Frame, rvk::Alloc> frames;
    Box<rvk::Shader_Loader, rvk::Alloc> shaders;

    Render::Pipeline geometry, ambient_occlusion, shading, material_path;
    rvk::Shader_Loader::Token geometry_token, ao_token, shading_token, material_path_token;
    rvk::Binding_Table geometry_table, ao_table, shading_table, material_path_table;

    Render::Pipeline post_process;
    rvk::Shader_Loader::Token post_token;

    void rebuild_frames();
    void rebuild_pipelines();
    void rebuild_binding_tables();
    template<typename T>
    Async::Task<rvk::Shader_Loader::Token> make_pipeline(Render::Pipeline& old_pipeline,
                                                         rvk::Binding_Table& old_table);

    // Scene data

    GPU_Scene::Scene scene;
    Async::Task<GPU_Scene::Scene> loading_scene;
    Async::Task<void> saving_image;
    i32 max_parallelism = 32;

    // Render settings

    Integrator integrator = Integrator::material_path;
    Render::Geometry::Mode geometry_mode = Render::Geometry::Mode::barycentric;
    Render::Shading::Mode shading_mode = Render::Shading::Mode::material_id;

    Mat4 current_iview, current_iproj;
    u32 stationary_frames = 0;
    u32 max_depth = 16;
    i32 max_stationary_frames = 100000;

    Render::Tonemap tonemap = Render::Tonemap::exponential;
    f32 gamma = 2.2f;
    f32 exposure = 1.0f;
    f32 sun = 1.0f;

    u32 suppress_fireflies = 0;
    u32 samples_per_frame = 1;
    bool accumulate = true;
    bool needs_reset = false;
    bool shading_normals = true;
    bool hdr = false;
    bool roulette = true;

    Async::Task<GPU_Scene::Scene> load_scene_pbrt(String_View path_);
    Async::Task<GPU_Scene::Scene> load_scene_gltf(String_View path_);
    Async::Task<GPU_Scene::Scene> load_scene_open();
    Async::Task<void> save_image();

    Render::Post::Op postprocess_op(bool output_srgb);
};

RPP_ENUM(Integrator, geometry, RPP_CASE(geometry), RPP_CASE(shading), RPP_CASE(ambient_occlusion),
         RPP_CASE(material_path));
