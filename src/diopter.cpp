
#include "diopter.h"
#include "gui/imgui_ext.h"

Diopter::Diopter(Window& window) : window(window), cam(window), renderer(pool) {
}

Diopter::~Diopter() {
}

void Diopter::gui() {
    using namespace ImGui;
    if(debug.begin_gui(window.drawable())) {
        if(CollapsingHeader("Camera")) {
            cam.gui();
        }
        if(CollapsingHeader("Vulkan")) {
            rvk::imgui();
        }
        if(CollapsingHeader("Renderer", null, ImGuiTreeNodeFlags_DefaultOpen)) {
            renderer.gui();
        }
        if(CollapsingHeader("Scene", null, ImGuiTreeNodeFlags_DefaultOpen)) {
            renderer.pick_scene(cam);
        }
        debug.end_gui();
    }
}

void Diopter::loop() {

    running = true;

    debug.console.command("exit"_v, [this]() { running = false; });

    while(running) {

        float dt = Profile::begin_frame();
        {
            auto opt = window.event();
            while(opt.ok()) {

                const SDL_Event& evt = *opt;
                switch(evt.type) {
                case SDL_QUIT: {
                    running = false;
                } break;
                }

                event(evt);
                opt = window.event();
            }

            bool f = window.is_down(SDL_SCANCODE_W);
            bool b = window.is_down(SDL_SCANCODE_S);
            bool l = window.is_down(SDL_SCANCODE_A);
            bool r = window.is_down(SDL_SCANCODE_D);
            bool u = window.is_down(SDL_SCANCODE_SPACE);
            bool d = window.is_down(SDL_SCANCODE_LSHIFT);
            cam.move(f, b, l, r, u, d, dt);

            if(window.begin_frame()) rvk::reset_imgui();
            if(rvk::resized()) {
                auto ext = rvk::extent();
                cam.ar(ext.width, ext.height);
                renderer.on_resize();
            }

            rvk::begin_frame();
            if(!rvk::minimized()) {
                gui();
                renderer.render(cam);
            }
            rvk::end_frame(renderer.output());
        }
        Profile::end_frame();
    }
}

void Diopter::event(SDL_Event e) {

    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplayFramebufferScale = window.scale(Vec2{1.0f, 1.0f});

    switch(e.type) {

    case SDL_WINDOWEVENT: {
        if(e.window.event == SDL_WINDOWEVENT_RESIZED ||
           e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            apply_window_dim(window.drawable());
        }
    } break;

    case SDL_MOUSEMOTION: {
        Vec2 d((f32)e.motion.xrel, (f32)e.motion.yrel);
        cam.mouse(d);
    } break;

    case SDL_MOUSEBUTTONDOWN: {
        if(IO.WantCaptureMouse) break;
        cam.click(e.button.button);
    } break;

    case SDL_MOUSEBUTTONUP: {
        if(IO.WantCaptureMouse && !cam.is_grabbed()) break;
        cam.unclick(e.button.button);
    } break;

    case SDL_MOUSEWHEEL: {
        if(IO.WantCaptureMouse) break;
        cam.scroll((float)e.wheel.y);
    } break;

    case SDL_KEYUP: {
        if(e.key.keysym.sym == SDLK_BACKQUOTE) {
            debug.toggle_gui();
        } else if(e.key.keysym.sym == SDLK_F11) {
            window.toggle_fullscreen();
        }
    } break;
    }
}

void Diopter::apply_window_dim(Vec2 new_dim) {
    cam.ar(new_dim);
}
