
#pragma once

#include <SDL2/SDL_events.h>
#include <rpp/base.h>

#include "gui/dbg_gui.h"
#include "platform/window.h"
#include "renderer/renderer.h"

using namespace rpp;

struct Diopter {

    explicit Diopter(Window& window);
    ~Diopter();

    void loop();

private:
    void event(SDL_Event e);
    void gui();
    void apply_window_dim(Vec2 new_dim);

    Window& window;
    bool running = false;

    Async::Pool<> pool;
    Dbg_Gui::Window debug;
    Camera cam;
    Renderer renderer;
};
