
#pragma once

#include <imgui/imgui.h>

#include <rpp/base.h>
#include <rpp/log_callback.h>
#include <rpp/tuple.h>

#include "../platform/window.h"

using namespace rpp;

namespace Dbg_Gui {

struct Window;

using Alloc = Mallocator<"DebugGUI">;

namespace Profiler {

enum class Color : u32 {
#define RGBA_LE(col)                                                                               \
    (((col & 0xff000000) >> (3 * 8)) + ((col & 0x00ff0000) >> (1 * 8)) +                           \
     ((col & 0x0000ff00) << (1 * 8)) + ((col & 0x000000ff) << (3 * 8)))

    none,
    turqoise = RGBA_LE(0x1abc9cffu),
    greenSea = RGBA_LE(0x16a085ffu),
    emerald = RGBA_LE(0x2ecc71ffu),
    nephritis = RGBA_LE(0x27ae60ffu),
    peterRiver = RGBA_LE(0x3498dbffu),
    belizeHole = RGBA_LE(0x2980b9ffu),
    amethyst = RGBA_LE(0x9b59b6ffu),
    wisteria = RGBA_LE(0x8e44adffu),
    sunFlower = RGBA_LE(0xf1c40fffu),
    orange = RGBA_LE(0xf39c12ffu),
    carrot = RGBA_LE(0xe67e22ffu),
    pumpkin = RGBA_LE(0xd35400ffu),
    alizarin = RGBA_LE(0xe74c3cffu),
    pomegranate = RGBA_LE(0xc0392bffu),
    clouds = RGBA_LE(0xecf0f1ffu),
    silver = RGBA_LE(0xbdc3c7ffu),
    text = RGBA_LE(0xF2F5FAFFu)

#undef RGBA_LE
};

struct Graph_Entry {
    Graph_Entry() = default;

    f32 startTime = 0.0f, endTime = 0.0f;
    Color color = Color::none;
    String_View name;

    f32 length() {
        return endTime - startTime;
    }
};

struct Graph {

    u64 frameWidth = 3, frameSpacing = 1;
    bool useColoredLegendText = false;

    Graph(u64 frames);
    ~Graph() = default;

    void load_frame_data(Graph_Entry* tasks, u64 count);
    void render_timings(u64 graphWidth, u64 legendWidth, u64 height, u64 frameIndexOffset);

private:
    void rebuild_task_stats(u64 endFrame, u64 framesCount);
    void render_graph(ImDrawList* drawList, Vec2 graphPos, Vec2 graphSize, u64 frameIndexOffset);
    void render_legend(ImDrawList* drawList, Vec2 legendPos, Vec2 legendSize, u64 frameIndexOffset);
    static void rect(ImDrawList* drawList, Vec2 minPoint, Vec2 maxPoint, u32 col,
                     bool filled = true);
    static void text(ImDrawList* drawList, Vec2 point, u32 col, const char* text);
    static void triangle(ImDrawList* drawList, Vec2 points[3], u32 col, bool filled = true);
    static void render_task_marker(ImDrawList* drawList, Vec2 leftMinPoint, Vec2 leftMaxPoint,
                                   Vec2 rightMinPoint, Vec2 rightMaxPoint, u32 col);

    struct Frame_Data {
        Frame_Data() = default;
        Vec<Graph_Entry, Alloc> tasks;
        Vec<u64, Alloc> taskStatsIndex;
    };

    struct Task_Stats {
        f32 maxTime;
        u64 priorityOrder, onScreenIndex;
    };

    Vec<Task_Stats, Alloc> taskStats;
    Vec<Frame_Data, Alloc> frames;
    Map<String_View, u64, Alloc> taskNameToStatsIndex;

    u64 currFrameIndex = 0;
};

struct Window {

    Window();
    ~Window() = default;

private:
    bool stopProfiling = false;
    Graph cpuGraph;
    static constexpr u64 n_frames = 1000;

    void render(bool* win_open);

    f32 avgFrameTime = 1.0f;
    u64 fpsFramesCount = 0;
    bool useColoredLegendText = true;
    i64 frameOffset = 0, frameWidth = 3, frameSpacing = 1;
    i64 legendWidth = 250;

    Profile::Time_Point prevFpsFrameTime;

    friend struct Dbg_Gui::Window;
};

} // namespace Profiler

namespace Console {

template<typename T>
concept Arg = One_Is<T, i64, f32, String_View> || Reflect::Enum<T>;

template<Allocator A, Arg... Args>
String<A> usage() {
    auto name = []<Arg T>() -> String_View {
        if constexpr(Same<T, i64>)
            return "i64"_v;
        else if constexpr(Same<T, f32>)
            return "f32"_v;
        else if constexpr(Same<T, String_View>)
            return "string"_v;
        else if constexpr(Reflect::Enum<T>)
            return String_View{Reflect::Refl<T>::name};
    };
    return concat<A>(" "_v, name.template operator()<Args>()...);
}

template<Arg A>
Opt<Pair<A, String_View>> parse(String_View input) {
    if constexpr(Same<A, i64>) {
        return Format::parse_i64(input);
    } else if constexpr(Same<A, f32>) {
        return Format::parse_f32(input);
    } else if constexpr(Same<A, String_View>) {
        return Format::parse_string(input);
    } else if constexpr(Reflect::Enum<A>) {
        return Format::parse_enum<A>(input);
    }
}

template<Arg... Args>
struct Parse;

template<Arg A, Arg... Rest>
struct Parse<A, Rest...> {
    static Opt<Tuple<A, Rest...>> parse(String_View input) {
        auto p = Console::parse<A>(input);
        if(!p) return {};
        auto [arg, rest] = move(*p);
        auto args = Parse<Rest...>::parse(rest);
        if(!args) return {};
        return Opt<Tuple<A, Rest...>>{Tuple<A, Rest...>{move(arg), move(*args)}};
    }
};

template<>
struct Parse<> {
    static Opt<Tuple<>> parse(String_View) {
        return Opt<Tuple<>>{{}};
    }
};

template<Arg... Args>
Opt<Tuple<Args...>> parse(String_View input) {
    return Parse<Args...>::parse(input);
}

struct Window {

    Window();
    ~Window();

    Window(const Window& src) = delete;
    Window& operator=(const Window& src) = delete;

    Window(Window&& src) = delete;
    Window& operator=(Window&& src) = delete;

    void msg(Log::Level level, Thread::Id thread, Log::Time timestamp, Log::Location publisher,
             String_View text);
    void clear();

    template<Console::Arg... Args>
    void command(String_View name, Invocable<Args...> auto&& f) {
        using Cmd = Command<decltype(f), Args...>;
        Box<Cmd, Alloc> cmd{forward<decltype(f)>(f)};
        commands.insert(name, Box<Command_Base, Alloc>{move(cmd)});
    }

private:
    void render(Vec2 window_size, bool* shown);

    struct Message {
        Log::Level level;
        Thread::Id thread;
        Log::Time timestamp;
        Log::Location publisher;
        String<Alloc> text;
        bool was_command;
    };

    struct Command_Base {
        virtual ~Command_Base() {
        }
        virtual bool execute(String_View input) = 0;
        virtual String_View usage() = 0;
    };

    template<typename F, Console::Arg... Args>
        requires Invocable<F, Args...>
    struct Command : public Command_Base {
        Command(F&& func) : func(forward<F>(func)) {
            usage_ = Console::usage<Alloc, Args...>();
        }
        bool execute(String_View input) {
            auto args = Console::parse<Args...>(input);
            if(!args.ok()) return false;
            args->invoke(func);
            return true;
        }
        String_View usage() {
            return usage_.view();
        }
        F func;
        String<Alloc> usage_;
    };

    ImGuiTextFilter filter;

    Array<char, 2048> input_buffer;

    Log::Token log_token = 0;
    Log::Level show_level = Log::Level::info;

    bool scroll_bottom = true;
    bool copy_clipboard = false;
    bool added_line = false;

    i64 history_idx = -1;
    Vec<String<Alloc>, Alloc> history;

    static constexpr u64 max_lines = 1024;
    Thread::Mutex lines_mut;
    Queue<Message, Alloc> lines;

    Map<String_View, Box<Command_Base, Alloc>, Alloc> commands;
    Vec<String_View, Alloc> candidates;

    void execute(String_View command);
    void on_text_edit(ImGuiInputTextCallbackData* data);

    void msg_cmd(Log::Level level, Thread::Id thread, Log::Time timestamp, Log::Location publisher,
                 String_View text);

    friend int console_text_edit_callback(ImGuiInputTextCallbackData*);
    friend struct Dbg_Gui::Window;
};

} // namespace Console

struct Window {

    Window() = default;
    ~Window() = default;

    bool begin_gui(Vec2 window_size);
    void end_gui();
    void toggle_gui();

    Profiler::Window profiler;
    Console::Window console;

private:
    void draw_profiler();
    void draw_console(Vec2 window_size);

    bool show_info = true;
    bool show_profiler = false;
    bool show_console = false;
};

} // namespace Dbg_Gui
