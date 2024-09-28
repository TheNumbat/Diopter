
#pragma once

#include <SDL2/SDL_events.h>

#include <rpp/base.h>
#include <rpp/vmath.h>

using namespace rpp;

struct Window {

    Window();
    ~Window();

    Vec2 size();
    Vec2 drawable();
    Vec2 scale(Vec2 pt);

    void fullscreen(bool enable);
    void toggle_fullscreen();

    void capture_mouse();
    void release_mouse();
    void set_mouse(Vec2 pos);
    Vec2 get_mouse();
    void grab_mouse();
    void ungrab_mouse();

    Vec2 begin_relative();
    void end_relative(Vec2 p);

    Opt<SDL_Event> event();
    bool is_down(SDL_Scancode key);

    bool begin_frame();

    SDL_Window* sdl() {
        return window;
    }

private:
    static constexpr f32 FONT_SIZE = 12.0f;
    f32 prev_dpi = 0.0f;
    f32 prev_scale = 0.0f;

    bool set_dpi();

    SDL_Window* window = null;
    const u8* keybuf = null;
};
