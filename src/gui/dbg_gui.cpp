
#include <algorithm>

#include "dbg_gui.h"
#include "imgui_ext.h"

namespace Dbg_Gui {

bool Window::begin_gui(Vec2 window_size) {

    using namespace ImGui;
    if(!show_info) return false;

    SetNextWindowSize({350.0f, 500.0f}, ImGuiCond_Once);
    Begin("Debug Info", &show_info, DebugWin);
    Checkbox("Console", &show_console);
    SameLine();
    Checkbox("Profiler", &show_profiler);

    draw_profiler();
    draw_console(window_size);

    return true;
}

void Window::end_gui() {
    ImGui::End();
}

void Window::toggle_gui() {
    show_info = !show_info;
}

void Window::draw_console(Vec2 window_size) {

    if(!show_console) return;
    console.render(window_size, &show_console);
}

void Window::draw_profiler() {

    if(!show_profiler) return;

    if(!profiler.stopProfiling) {
        Region(R) {

            Map<Log::Location, Dbg_Gui::Profiler::Graph_Entry, Mregion<R>> accum;

            Profile::iterate_timings([&accum](Thread::Id id, const Profile::Timing_Node& n) {
                if(id != Thread::this_id()) return;
                Dbg_Gui::Profiler::Graph_Entry& t = accum.get_or_insert(n.loc);
                t.name = n.loc.function;
                t.endTime += Profile::ms(n.self_time);
            });

            Vec<Dbg_Gui::Profiler::Graph_Entry, Mregion<R>> data(accum.length());

            for(auto& t : accum) data.push(t.second);

            std::sort(data.begin(), data.end(),
                      [](auto l, auto r) { return l.endTime < r.endTime; });

            for(u64 i = 1; i < data.length(); i++) {
                data[i].startTime = data[i - 1].endTime;
                data[i].endTime += data[i].startTime;
            }

            profiler.cpuGraph.load_frame_data(data.data(), data.length());
        }
    }

    profiler.render(&show_profiler);
}

namespace Console {

int console_text_edit_callback(ImGuiInputTextCallbackData* data) {
    Window* console = reinterpret_cast<Window*>(data->UserData);
    console->on_text_edit(data);
    return 0;
}

Window::Window() : history(128) {
    msg(Log::Level::info, Thread::this_id(), 0, RPP_HERE, "Console initialized."_v);
    log_token = Log::subscribe(
        [this](Log::Level level, Thread::Id thread, Log::Time time, Log::Location loc,
               String_View msg) { this->msg(level, thread, time, move(loc), move(msg)); });
}

Window::~Window() {
    Log::unsubscribe(log_token);
    log_token = 0;
}

void Window::execute(String_View command) {

    if(!history.empty() && command == history.back().view()) {
        history.pop();
    }
    history_idx = -1;

    if(history.full()) history.pop();
    history.push(command.string<Alloc>());

    auto p = Format::parse_string(command);
    if(!p.ok()) return;

    auto [name, args] = move(*p);
    Opt<Ref<Box<Command_Base, Alloc>>> cmd_ = commands.try_get(name);

    u64 time = Log::sys_time();
    if(!cmd_.ok()) {
        msg_cmd(Log::Level::info, Thread::this_id(), time, RPP_HERE, command);
        return;
    }

    auto& cmd = **cmd_;
    if(!cmd->execute(args)) {
        Region(R) {
            auto error = format<Mregion<R>>("Usage: % %"_v, name, cmd->usage());
            msg_cmd(Log::Level::warn, Thread::this_id(), time, RPP_HERE, error.view());
        }
    }
}

void Window::msg(Log::Level level, Thread::Id thread, Log::Time timestamp, Log::Location publisher,
                 String_View text) {

    auto msg =
        format<Alloc>("% [%/%] %\x00"_v, Log::sys_time_string(timestamp), level, thread, text);

    Thread::Lock lock(lines_mut);
    if(lines.length() == max_lines) {
        lines.pop();
    }
    lines.push(Message{level, thread, timestamp, publisher, move(msg), false});

    added_line = true;
}

void Window::msg_cmd(Log::Level level, Thread::Id thread, Log::Time timestamp,
                     Log::Location publisher, String_View text) {

    auto msg =
        format<Alloc>("% [%/%] %\x00"_v, Log::sys_time_string(timestamp), level, thread, text);

    Thread::Lock lock(lines_mut);
    if(lines.length() == max_lines) {
        lines.pop();
    }
    lines.push(Message{level, thread, timestamp, publisher, move(msg), true});

    added_line = true;
}

void Window::render(Vec2 window_size, bool* shown) {

    using namespace ImGui;

    f32 w = window_size.x;
    f32 h = window_size.y;

    SetNextWindowPos({0.0f, Math::ceil(h * 0.65f)});
    SetNextWindowSize({w, Math::ceil(h * 0.35f)});
    Begin("Console", shown,
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
              ImGuiWindowFlags_NoSavedSettings);

    f32 footer = GetStyle().ItemSpacing.y + GetFrameHeightWithSpacing();
    BeginChild("Scroll", ImVec2(0, -footer), false, ImGuiWindowFlags_HorizontalScrollbar);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

    if(copy_clipboard) {
        LogToClipboard();
    }
    {
        Thread::Lock lock(lines_mut);
        for(const Message& msg : lines) {

            if(msg.level < show_level) continue;

            if(!filter.PassFilter(reinterpret_cast<const char*>(msg.text.data()))) continue;

            Vec4 col = GetStyleColorVec4(ImGuiCol_Text);
            if(msg.was_command) {
                col = Vec4{1.0f, 0.78f, 0.58f, 1.0f};
            } else {
                switch(msg.level) {
                case Log::Level::info: col = col * 0.7f; break;
                case Log::Level::warn: col = Vec4{1.0f, 0.4f, 0.4f, 1.0f}; break;
                case Log::Level::fatal: col = Vec4{1.0f, 0.0f, 0.0f, 1.0f}; break;
                default: RPP_UNREACHABLE;
                }
            }

            PushStyleColor(ImGuiCol_Text, col);
            TextUnformatted(reinterpret_cast<const char*>(msg.text.data()));

            if(IsItemHovered()) {
                PopStyleColor();
                BeginTooltip();
                Text("%.*s:%u (%.*s)", msg.publisher.file.length(), msg.publisher.file.data(),
                     msg.publisher.line, msg.publisher.function.length(),
                     msg.publisher.function.data());
                EndTooltip();
                PushStyleColor(ImGuiCol_Text, col);
            }

            PopStyleColor();
        }
    }

    if(copy_clipboard) {
        LogFinish();
        copy_clipboard = false;
    }
    if(scroll_bottom) {
        SetScrollHereY(1.0f);
        scroll_bottom = false;
    }
    if(added_line) {
        if(GetScrollY() >= GetScrollMaxY() - 1.0f) {
            SetScrollHereY(1.0f);
        }
        added_line = false;
    }

    PopStyleVar();
    EndChild();
    Separator();

    Columns(4);

    SetColumnWidth(0, w * 0.40f);
    SetColumnWidth(1, w * 0.30f);
    SetColumnWidth(2, w * 0.15f);
    SetColumnWidth(3, w * 0.15f);

    bool reclaim_focus = false;
    if(InputText("Input", input_buffer.data(), input_buffer.length(),
                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter |
                     ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory,
                 &console_text_edit_callback, this)) {

        String_View input(input_buffer.data());

        if(input.length() > 1) {
            candidates.clear();
            execute(input);
            scroll_bottom = true;
        }

        input_buffer[0] = '\0';
        reclaim_focus = true;
    }

    SetItemDefaultFocus();
    if(reclaim_focus) SetKeyboardFocusHere(-1);

    NextColumn();

    filter.Draw("Filter (inc,-exc)", 180);

    NextColumn();

    Combo("Level", show_level);

    NextColumn();

    if(Button("Clear")) {
        clear();
        scroll_bottom = true;
    }
    SameLine();
    if(Button("Bottom")) {
        scroll_bottom = true;
    }
    SameLine();
    if(Button("Copy")) {
        copy_clipboard = true;
    }

    Columns(1);

    if(candidates.length() > 1) {

        f32 element = 2.0f * GetStyle().ItemSpacing.y + GetFrameHeightWithSpacing();

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_ChildWindow |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
        Begin("Completions", null, flags);

        for(const auto& candidate : candidates) {
            Text("%.*s", candidate.length(), reinterpret_cast<const char*>(candidate.data()));
        }

        f32 tip_h = GetWindowHeight();
        SetWindowPos({0, h - tip_h - element}, ImGuiCond_Always);

        EndChild();
    }

    End();
}

void Window::clear() {
    Thread::Lock lock(lines_mut);
    lines.clear();
}

void Window::on_text_edit(ImGuiInputTextCallbackData* data) {
    switch(data->EventFlag) {
    case ImGuiInputTextFlags_CallbackCompletion: {

        // Find beginning of current word
        char* end = data->Buf + data->CursorPos;
        char* begin = end;
        while(begin > data->Buf) {
            char c = begin[-1];
            if(c == ' ' || c == '\t' || c == ',' || c == ';') break;
            begin--;
        }

        String_View word{reinterpret_cast<const u8*>(begin), static_cast<u64>(end - begin)};
        candidates.clear();

        for(const auto& cmd : commands) {
            if(cmd.first.sub(0, word.length()) == word) {
                candidates.push(cmd.first);
            }
        }

        if(candidates.length() == 1) {

            data->DeleteChars(static_cast<int>(begin - data->Buf), static_cast<int>(end - begin));
            data->InsertChars(data->CursorPos, reinterpret_cast<const char*>(candidates[0].data()));
            data->InsertChars(data->CursorPos, " ");

        } else if(candidates.length() > 1) {

            u64 len = word.length() - 1;
            bool added = false;

            for(;;) {
                char cur_char = 0;
                bool all_match = true;

                for(u64 i = 0; i < candidates.length(); i++) {

                    if(len > candidates[i].length()) {
                        all_match = false;
                        break;
                    }

                    if(i == 0) {

                        cur_char = ascii::to_uppercase(candidates[i][len]);

                    } else if(cur_char == 0 ||
                              cur_char != ascii::to_uppercase(candidates[i][len])) {

                        all_match = false;
                    }
                }

                if(!all_match) break;
                len++;
                added = true;
            }

            if(added) {
                data->DeleteChars(static_cast<int>(static_cast<ptrdiff_t>(begin - data->Buf)),
                                  static_cast<int>(static_cast<ptrdiff_t>(end - begin)));
                data->InsertChars(data->CursorPos,
                                  reinterpret_cast<const char*>(candidates[0].data()),
                                  reinterpret_cast<const char*>(candidates[0].data() + len));
            }
        }

    } break;
    case ImGuiInputTextFlags_CallbackCharFilter: {

        if(data->EventChar == ' ') candidates.clear();

    } break;
    case ImGuiInputTextFlags_CallbackHistory: {

        i64 prev_idx = history_idx;
        if(data->EventKey == ImGuiKey_UpArrow) {

            if(history_idx == -1)
                history_idx = static_cast<i64>(history.length() - 1);
            else if(history_idx > 0)
                history_idx--;

        } else if(data->EventKey == ImGuiKey_DownArrow) {

            if(history_idx != -1 && ++history_idx >= static_cast<i64>(history.length()))
                history_idx = -1;
        }

        if(prev_idx != history_idx) {

            String_View history_str = history_idx >= 0 ? history[history_idx].view() : ""_v;

            data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen =
                static_cast<int>(history_str.length());

            Libc::memcpy(data->Buf, history_str.data(),
                         Math::min(data->BufSize, static_cast<int>(history_str.length() + 1)));
            if(history_str.length() < static_cast<u64>(data->BufSize))
                data->Buf[history_str.length()] = '\0';

            data->BufDirty = true;
        }

    } break;
    }
}

} // namespace Console

namespace Profiler {

static const Color color_order[] = {
    Color::alizarin,    Color::greenSea,   Color::pumpkin,   Color::silver,
    Color::turqoise,    Color::belizeHole, Color::nephritis, Color::clouds,
    Color::pomegranate, Color::peterRiver, Color::sunFlower, Color::amethyst,
    Color::carrot,      Color::wisteria,   Color::orange,    Color::emerald};
static const u64 num_colors = sizeof(color_order) / sizeof(color_order[0]);

Graph::Graph(u64 framesCount) {
    frames.extend(framesCount);
}

void Graph::load_frame_data(Profiler::Graph_Entry* tasks, u64 count) {
    auto& currFrame = frames[currFrameIndex];
    currFrame.tasks.clear();
    currFrame.taskStatsIndex.clear();

    for(u64 taskIndex = 0; taskIndex < count; taskIndex++) {
        if(tasks[taskIndex].color == Color::none)
            tasks[taskIndex].color = color_order[hash(tasks[taskIndex].name) % num_colors];

        if(taskIndex == 0)
            currFrame.tasks.push(tasks[taskIndex]);
        else {
            if(tasks[taskIndex - 1].color != tasks[taskIndex].color ||
               tasks[taskIndex - 1].name != tasks[taskIndex].name)
                currFrame.tasks.push(tasks[taskIndex]);
            else
                currFrame.tasks.back().endTime = tasks[taskIndex].endTime;
        }
    }
    currFrame.taskStatsIndex.extend(currFrame.tasks.length());

    for(u64 taskIndex = 0; taskIndex < currFrame.tasks.length(); taskIndex++) {

        auto& task = currFrame.tasks[taskIndex];
        Opt<Ref<u64>> it = taskNameToStatsIndex.try_get(task.name);

        if(!it.ok()) {
            taskNameToStatsIndex.insert(task.name, taskStats.length());
            Task_Stats taskStat;
            taskStat.maxTime = -1.0;
            taskStats.push(move(taskStat));
        }
        currFrame.taskStatsIndex[taskIndex] = taskNameToStatsIndex.get(task.name);
    }

    currFrameIndex = (currFrameIndex + 1) % frames.length();
    rebuild_task_stats(currFrameIndex, 300);
}

void Graph::render_timings(u64 graphWidth, u64 legendWidth, u64 height, u64 frameIndexOffset) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const Vec2 widgetPos(p.x, p.y);
    render_graph(drawList, widgetPos, Vec2((f32)graphWidth, (f32)height), frameIndexOffset);
    render_legend(drawList, widgetPos + Vec2((f32)graphWidth, 0.0f),
                  Vec2((f32)legendWidth, (f32)height), frameIndexOffset);
    ImGui::Dummy(ImVec2(f32(graphWidth + legendWidth), f32(height)));
}

void Graph::rebuild_task_stats(u64 endFrame, u64 framesCount) {
    for(auto& taskStat : taskStats) {
        taskStat.maxTime = -1.0f;
        taskStat.priorityOrder = static_cast<u64>(-1);
        taskStat.onScreenIndex = static_cast<u64>(-1);
    }

    for(u64 frameNumber = 0; frameNumber < framesCount; frameNumber++) {

        u64 frameIndex = (endFrame - 1 - frameNumber + frames.length()) % frames.length();
        auto& frame = frames[frameIndex];

        for(u64 taskIndex = 0; taskIndex < frame.tasks.length(); taskIndex++) {
            auto& task = frame.tasks[taskIndex];
            auto& stats = taskStats[frame.taskStatsIndex[taskIndex]];
            stats.maxTime = std::max(stats.maxTime, task.endTime - task.startTime);
        }
    }

    Region(R) {
        auto statPriorities = Vec<u64, Mregion<R>>::make(taskStats.length());

        for(u64 statIndex = 0; statIndex < taskStats.length(); statIndex++)
            statPriorities[statIndex] = statIndex;

        std::sort(statPriorities.begin(), statPriorities.end(), [this](u64 left, u64 right) {
            return taskStats[left].maxTime > taskStats[right].maxTime;
        });

        for(u64 statNumber = 0; statNumber < taskStats.length(); statNumber++) {
            u64 statIndex = statPriorities[statNumber];
            taskStats[statIndex].priorityOrder = statNumber;
        }
    }
}

void Graph::render_graph(ImDrawList* drawList, Vec2 graphPos, Vec2 graphSize,
                         u64 frameIndexOffset) {
    rect(drawList, graphPos, graphPos + graphSize, 0xffffffff, false);
    f32 maxFrameTime = 1000.0f / 30.0f;
    f32 heightThreshold = 1.0f;

    for(u64 frameNumber = 0; frameNumber < frames.length(); frameNumber++) {

        u64 frameIndex =
            (currFrameIndex - frameIndexOffset - 1 - frameNumber + 2 * frames.length()) %
            frames.length();

        Vec2 framePos = graphPos + Vec2(graphSize.x - 1 - frameWidth -
                                            (frameWidth + frameSpacing) * frameNumber,
                                        graphSize.y - 1);
        if(framePos.x < graphPos.x + 1) break;

        Vec2 taskPos = framePos + Vec2(0.0f, 0.0f);
        auto& frame = frames[frameIndex];

        for(auto task : frame.tasks) {
            f32 taskStartHeight = (f32(task.startTime) / maxFrameTime) * graphSize.y;
            f32 taskEndHeight = (f32(task.endTime) / maxFrameTime) * graphSize.y;
            if(Math::abs(taskEndHeight - taskStartHeight) > heightThreshold)
                rect(drawList, taskPos + Vec2(0.0f, static_cast<f32>(-taskStartHeight)),
                     taskPos + Vec2(static_cast<f32>(frameWidth), static_cast<f32>(-taskEndHeight)),
                     static_cast<u64>(task.color), true);
        }
    }
}

void Graph::render_legend(ImDrawList* drawList, Vec2 legendPos, Vec2 legendSize,
                          u64 frameIndexOffset) {
    f32 markerLeftRectMargin = 3.0f;
    f32 markerLeftRectWidth = 5.0f;
    f32 maxFrameTime = 1000.0f / 30.0f;
    f32 markerMidWidth = 20.0f;
    f32 markerRightRectWidth = 5.0f;
    f32 markerRigthRectMargin = 3.0f;
    f32 markerRightRectHeight = 10.0f;
    f32 markerRightRectSpacing = 4.0f;
    f32 nameOffset = 31.0f;
    Vec2 textMargin = Vec2(5.0f, -3.0f);

    auto& currFrame =
        frames[(currFrameIndex - frameIndexOffset - 1 + 2 * frames.length()) % frames.length()];
    u64 maxTasksCount =
        static_cast<u64>(legendSize.y / (markerRightRectHeight + markerRightRectSpacing));

    for(auto& taskStat : taskStats) taskStat.onScreenIndex = u64(-1);

    u64 tasksToShow = std::min<u64>(taskStats.length(), maxTasksCount);
    u64 tasksShownCount = 0;
    for(u64 taskIndex = 0; taskIndex < currFrame.tasks.length(); taskIndex++) {

        auto& task = currFrame.tasks[taskIndex];
        auto& stat = taskStats[currFrame.taskStatsIndex[taskIndex]];

        if(stat.priorityOrder >= tasksToShow) continue;

        if(stat.onScreenIndex == static_cast<u64>(-1))
            stat.onScreenIndex = tasksShownCount++;
        else
            continue;

        f32 taskStartHeight = (f32(task.startTime) / maxFrameTime) * legendSize.y;
        f32 taskEndHeight = (f32(task.endTime) / maxFrameTime) * legendSize.y;

        Vec2 markerLeftRectMin = legendPos + Vec2(markerLeftRectMargin, legendSize.y);
        Vec2 markerLeftRectMax = markerLeftRectMin + Vec2(markerLeftRectWidth, 0.0f);
        markerLeftRectMin.y -= taskStartHeight;
        markerLeftRectMax.y -= taskEndHeight;

        Vec2 markerRightRectMin =
            legendPos +
            Vec2(markerLeftRectMargin + markerLeftRectWidth + markerMidWidth,
                 legendSize.y - markerRigthRectMargin -
                     (markerRightRectHeight + markerRightRectSpacing) * stat.onScreenIndex);
        Vec2 markerRightRectMax =
            markerRightRectMin + Vec2(markerRightRectWidth, -markerRightRectHeight);
        render_task_marker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin,
                           markerRightRectMax, static_cast<u32>(task.color));

        u32 textColor = static_cast<u32>(useColoredLegendText ? task.color : Color::text);

        f32 taskTimeMs = f32(task.endTime - task.startTime);

        Region(R) {
            auto label = format<Mregion<R>>("[%\x00"_v, taskTimeMs);
            label.set_length(Math::min(label.length(), static_cast<u64>(6)));
            label[label.length() - 1] = '\x00';

            text(drawList, markerRightRectMax + textMargin, textColor,
                 reinterpret_cast<const char*>(label.data()));
            text(drawList, markerRightRectMax + textMargin + Vec2(nameOffset, 0.0f), textColor,
                 reinterpret_cast<const char*>(format<Mregion<R>>("] %\x00"_v, task.name).data()));
        }
    }
}

void Graph::rect(ImDrawList* drawList, Vec2 minPoint, Vec2 maxPoint, u32 col, bool filled) {
    if(filled)
        drawList->AddRectFilled(ImVec2(minPoint.x, minPoint.y), ImVec2(maxPoint.x, maxPoint.y),
                                col);
    else
        drawList->AddRect(ImVec2(minPoint.x, minPoint.y), ImVec2(maxPoint.x, maxPoint.y), col);
}

void Graph::text(ImDrawList* drawList, Vec2 point, u32 col, const char* text) {
    drawList->AddText(ImVec2(point.x, point.y), col, text);
}

void Graph::triangle(ImDrawList* drawList, Vec2 points[3], u32 col, bool filled) {
    if(filled)
        drawList->AddTriangleFilled(ImVec2(points[0].x, points[0].y),
                                    ImVec2(points[1].x, points[1].y),
                                    ImVec2(points[2].x, points[2].y), col);
    else
        drawList->AddTriangle(ImVec2(points[0].x, points[0].y), ImVec2(points[1].x, points[1].y),
                              ImVec2(points[2].x, points[2].y), col);
}

void Graph::render_task_marker(ImDrawList* drawList, Vec2 leftMinPoint, Vec2 leftMaxPoint,
                               Vec2 rightMinPoint, Vec2 rightMaxPoint, u32 col) {
    rect(drawList, leftMinPoint, leftMaxPoint, col, true);
    rect(drawList, rightMinPoint, rightMaxPoint, col, true);

    ImVec2 points[] = {
        ImVec2(leftMaxPoint.x, leftMinPoint.y), ImVec2(leftMaxPoint.x, leftMaxPoint.y),
        ImVec2(rightMinPoint.x, rightMaxPoint.y), ImVec2(rightMinPoint.x, rightMinPoint.y)};
    drawList->AddConvexPolyFilled(points, 4, col);
}

Window::Window() : cpuGraph(n_frames) {
    prevFpsFrameTime = Thread::perf_counter();
}

void Window::render(bool* win_open) {

    using namespace ImGui;

    fpsFramesCount++;
    auto currFrameTime = Thread::perf_counter();
    {
        f32 fpsDeltaTime = Profile::s(currFrameTime - prevFpsFrameTime);
        if(fpsDeltaTime > 0.25f) {
            this->avgFrameTime = fpsDeltaTime / f32(fpsFramesCount);
            fpsFramesCount = 0;
            prevFpsFrameTime = currFrameTime;
        }
    }

    SetNextWindowSize({400.0f, 250.0f}, ImGuiCond_Once);
    Region(R) {
        Begin(reinterpret_cast<const char*>(
                  format<Mregion<R>>("Profiler [% fps % ms]###ProfileGraph\x00"_v,
                                     1.0f / avgFrameTime, avgFrameTime * 1000.0f)
                      .data()),
              win_open, ImGuiWindowFlags_NoScrollbar);
    }
    ImVec2 canvasSize = GetContentRegionAvail();

    i64 sizeMargin = static_cast<i64>(GetStyle().ItemSpacing.y);
    i64 maxGraphHeight = 300;
    i64 availableGraphHeight = static_cast<i64>(canvasSize.y) - sizeMargin;
    i64 graphHeight = std::min(maxGraphHeight, availableGraphHeight);
    i64 graphWidth = static_cast<i64>(canvasSize.x) - legendWidth;
    cpuGraph.render_timings(graphWidth, legendWidth, graphHeight, frameOffset);
    if(graphHeight + sizeMargin + sizeMargin < canvasSize.y) {
        Columns(2);
        Checkbox("Stop profiling", &stopProfiling);
        SameLine();
        Checkbox("Colored legend text", &useColoredLegendText);
        DragI64("Frame offset", &frameOffset, 1.0f, 0, n_frames);
        DragI64("Legend width", &legendWidth, 1.0f, 50, 500);
        NextColumn();

        SliderI64("Frame width", &frameWidth, 1, 4);
        SliderI64("Frame spacing", &frameSpacing, 0, 2);
        SliderFloat("Transparency", &GetStyle().Colors[ImGuiCol_WindowBg].w, 0.0f, 1.0f);
        Columns(1);
    }

    if(!stopProfiling) frameOffset = 0;

    cpuGraph.frameWidth = frameWidth;
    cpuGraph.frameSpacing = frameSpacing;
    cpuGraph.useColoredLegendText = useColoredLegendText;
    End();
}

} // namespace Profiler

} // namespace Dbg_Gui
