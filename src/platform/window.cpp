
#include <SDL2/SDL.h>
#include <imgui/imgui.h>

#include "imgui_impl_sdl2.h"
#include "window.h"

#include "font.dat"

#ifdef _WIN32
#include <ShellScalingApi.h>
extern "C" {
__declspec(dllexport) bool NvOptimusEnablement = true;
__declspec(dllexport) bool AmdPowerXpressRequestHighPerformance = true;
}
#endif

using ImGui_Alloc = Mallocator<"ImGui">;

void* imgui_alloc(u64 sz, void*) {
    return ImGui_Alloc::alloc(sz);
}

void imgui_free(void* mem, void*) {
    ImGui_Alloc::free(mem);
}

Window::Window() {

#ifdef _WIN32
    if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
        warn("Failed to set process DPI awareness context.");
    }
#endif

    if(SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        die("Failed to initialize SDL: %", String_View{SDL_GetError()});
    }

    window =
        SDL_CreateWindow("Diopter", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN);
    if(!window) {
        die("Failed to create window: %", String_View{SDL_GetError()});
    }

    keybuf = SDL_GetKeyboardState(null);

    ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free, null);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    set_dpi();

    ImGui_ImplSDL2_InitForVulkan(window);
}

Window::~Window() {

    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyWindow(window);
    window = null;
    SDL_Quit();
}

void Window::fullscreen(bool enable) {
    SDL_SetWindowFullscreen(window, enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Window::toggle_fullscreen() {
    u32 flags = SDL_GetWindowFlags(window);
    fullscreen(!(flags & SDL_WINDOW_FULLSCREEN_DESKTOP));
}

bool Window::set_dpi() {

    f32 dpi;
    int index = SDL_GetWindowDisplayIndex(window);
    if(index < 0) {
        return false;
    }
    if(SDL_GetDisplayDPI(index, null, &dpi, null)) {
        return false;
    }
    f32 scale = drawable().x / size().x;
    if(prev_dpi == dpi && prev_scale == scale) return false;

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0.0f;
#ifndef __APPLE__
    style.ScaleAllSizes(0.8f * dpi / 96.0f);
#else
    style.ScaleAllSizes(0.8f);
#endif
    ImGui::GetStyle() = style;

    ImGuiIO& IO = ImGui::GetIO();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    IO.IniFilename = null;
    IO.Fonts->Clear();
#ifdef __APPLE__
    IO.Fonts->AddFontFromMemoryTTF(font_ttf, font_ttf_len, FONT_SIZE * scale, &config);
    IO.FontGlobalScale = 1.0f / scale;
#else
    IO.Fonts->AddFontFromMemoryTTF(font_ttf, font_ttf_len, FONT_SIZE / 96.0f * dpi, &config);
#endif
    IO.Fonts->Build();

    prev_dpi = dpi;
    prev_scale = scale;
    return true;
}

bool Window::is_down(SDL_Scancode key) {
    return keybuf[key];
}

Opt<SDL_Event> Window::event() {
    SDL_Event e;
    if(SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        return Opt<SDL_Event>{e};
    }
    return {};
}

bool Window::begin_frame() {
    bool dpi_changed = set_dpi();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplayFramebufferScale = scale(Vec2{1.0f, 1.0f});
    ImGui_ImplSDL2_NewFrame();
    return dpi_changed;
}

Vec2 Window::scale(Vec2 pt) {
    return pt * drawable() / size();
}

Vec2 Window::size() {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    return Vec2(static_cast<f32>(w), static_cast<f32>(h));
}

Vec2 Window::drawable() {
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    return Vec2(static_cast<f32>(w), static_cast<f32>(h));
}

void Window::grab_mouse() {
    SDL_SetWindowGrab(window, SDL_TRUE);
}

void Window::ungrab_mouse() {
    SDL_SetWindowGrab(window, SDL_FALSE);
}

Vec2 Window::get_mouse() {
    int x, y;
    SDL_GetMouseState(&x, &y);
    return Vec2(static_cast<f32>(x), static_cast<f32>(y));
}

void Window::capture_mouse() {
    SDL_CaptureMouse(SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

void Window::release_mouse() {
    SDL_CaptureMouse(SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
}

void Window::set_mouse(Vec2 pos) {
    SDL_WarpMouseInWindow(window, static_cast<int>(pos.x), static_cast<int>(pos.y));
}

Vec2 Window::begin_relative() {
    Vec2 p = get_mouse();
    grab_mouse();
    capture_mouse();
    return p;
}

void Window::end_relative(Vec2 p) {
    release_mouse();
    ungrab_mouse();
    set_mouse(p);
}
