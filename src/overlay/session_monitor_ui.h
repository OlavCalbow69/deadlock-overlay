#pragma once

#include "session_monitor.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"
#include "external_ui.h"
#include "hud_style.h"
#include <shellapi.h>

namespace overlay {

template<class Value, class Valid>
inline void session_plot(ImDrawList* draw, const SessionMonitor& monitor, const ImRect& box,
    double maximum, ImU32 color, Value value, Valid valid, float scale = 1.f) {
    const auto end = monitor.graph_time();
    if (!end || maximum <= 0) return;
    draw->PushClipRect(box.Min, box.Max, true);
    ImVec2 previous{};
    uint64_t previous_time{};
    bool have_previous{};
    for (size_t i = 0; i < monitor.size(); ++i) {
        const auto& p = monitor.at(i);
        if (!valid(p) || !std::isfinite(value(p)) || p.time_ms > end || end - p.time_ms > 60000) {
            have_previous = false; continue;
        }
        const float x = box.Max.x - float(end - p.time_ms) / 60000.f * box.GetWidth();
        const float y = box.Max.y - float(std::clamp(value(p) / maximum, 0., 1.)) * box.GetHeight();
        const ImVec2 current{x, y};
        if (have_previous && p.time_ms - previous_time <= 1000)
            draw->AddLine(previous, current, color, 1.75f * scale);
        else draw->AddCircleFilled(current, 1.75f * scale, color, 8);
        previous = current; previous_time = p.time_ms; have_previous = true;
    }
    draw->PopClipRect();
}

inline bool draw_session_monitor(SessionMonitor& monitor, const Snapshot& snap,
    const ImRect& body, float alpha, bool transparent,
    bool input_ready, const std::string& input_status, const std::filesystem::path& reports, HWND owner) {
    using namespace solace;
    const auto origin = body.Min;
    auto* draw = ImGui::GetWindowDrawList();
    auto text = [&](ImVec2 at, const char* label, bool muted = false, float size = 13.f) {
        draw_text(draw, font_semibold(size), at,
            mo::with_alpha(muted ? c_muted_foreground : c_foreground, alpha), label);
    };
    auto clipped_text = [&](ImVec2 at, float width, const char* label, bool muted = false, float size = 13.f) {
        draw->PushClipRect(at, at + ImVec2(width, px(size + 8)), true);
        text(at, label, muted, size); draw->PopClipRect();
    };
    auto card = [&](const ImRect& box, const char* title, icons::id icon) {
        rounded_panel::draw(draw, box.Min, box.Max,
            mo::with_alpha(c_card, (transparent ? 1.f : .72f) * alpha), px(16));
        draw->AddRect(box.Min + px(.5f, .5f), box.Max - px(.5f, .5f),
            mo::with_alpha(c_border, alpha), px(16), 0, px(1));
        icons::draw(icon, draw, box.Min + px(18, 17), px(15), mo::with_alpha(c_muted_foreground, alpha));
        draw_text_tracked(draw, font_semibold(11), box.Min + px(42, 18),
            mo::with_alpha(c_muted_foreground, alpha), title, px(.8f));
    };
    text(origin + px(0, 38), "Session monitor / last 60 seconds", true, 13);
    const char* live = monitor.frozen ? "History frozen / metrics live" : "Live / 5 samples per second";
    text({body.Max.x - px(195), origin.y + px(38)}, live, true, 12);
    const float gap = px(14), tile_width = (body.GetWidth() - gap * 2) / 3;
    const auto& current = monitor.latest;
    char label[160]{};
    ImRect fps_tile(origin + px(0, 73), origin + ImVec2(tile_width, px(164)));
    ImRect read_tile({fps_tile.Max.x + gap, fps_tile.Min.y}, {fps_tile.Max.x + gap + tile_width, fps_tile.Max.y});
    ImRect memory_tile({read_tile.Max.x + gap, fps_tile.Min.y}, {body.Max.x, fps_tile.Max.y});
    card(fps_tile, "OVERLAY FPS", icons::id::clock);
    card(read_tile, "GAME DATA READ", icons::id::workflow);
    card(memory_tile, "OVERLAY RESOURCES", icons::id::layout_grid);
    if (current.rendering) snprintf(label, sizeof(label), "%.0f / %.0f Hz", current.fps, current.refresh_hz);
    else snprintf(label, sizeof(label), "Paused");
    text(fps_tile.Min + px(18, 41), label, false, 23);
    text(fps_tile.Min + px(18, 72), "Overlay presentation, separate from game FPS", true, 10);
    if (current.current_data) snprintf(label, sizeof(label), "%.2f ms", current.read_ms);
    else snprintf(label, sizeof(label), "Waiting");
    text(read_tile.Min + px(18, 41), label, false, 23);
    if (current.age_ms >= 0) snprintf(label, sizeof(label), "Snapshot age %.0f ms", current.age_ms);
    else snprintf(label, sizeof(label), "No current game snapshot");
    text(read_tile.Min + px(18, 72), label, true, 11);
    if (current.resources_valid) snprintf(label, sizeof(label), "%.0f MB", current.private_mb);
    else snprintf(label, sizeof(label), "Waiting");
    text(memory_tile.Min + px(18, 41), label, false, 23);
    snprintf(label, sizeof(label), "CPU %.1f%% / total processor capacity", current.cpu_percent);
    text(memory_tile.Min + px(18, 72), label, true, 10);

    const float left = (body.GetWidth() - gap) * .60f;
    ImRect history(origin + px(0, 178), origin + ImVec2(left, px(403)));
    ImRect view({history.Max.x + gap, history.Min.y}, {body.Max.x, history.Max.y});
    card(history, "TIMING HISTORY", icons::id::clock);
    card(view, "LIVE VIEW", icons::id::eye);
    const double read_p95 = monitor.percentile(.95, [](const SessionMetric& p) { return p.read_ms; },
        [](const SessionMetric& p) { return p.current_data; });
    const double fps_p05 = monitor.percentile(.05, [](const SessionMetric& p) { return p.fps; },
        [](const SessionMetric& p) { return p.rendering; });
    text(history.Min + px(18, 45), "Overlay FPS", true, 11);
    if (fps_p05 >= 0) snprintf(label, sizeof(label), "5th percentile %.0f", fps_p05);
    else snprintf(label, sizeof(label), "Waiting for rendered frames");
    text({history.Max.x - px(195), history.Min.y + px(45)}, label, true, 10);
    double fps_max = std::max(60., current.refresh_hz * 1.1), read_max = 2.;
    for (size_t i = 0; i < monitor.size(); ++i) {
        const auto& p = monitor.at(i);
        if (p.rendering) fps_max = std::max(fps_max, p.fps * 1.1);
        if (p.current_data) read_max = std::max(read_max, p.read_ms * 1.15);
    }
    ImRect fps_graph(history.Min + px(18, 66), {history.Max.x - px(18), history.Min.y + px(117)});
    ImRect read_graph(history.Min + px(18, 147), {history.Max.x - px(18), history.Min.y + px(198)});
    const ImU32 blue = mo::with_alpha(IM_COL32(83, 159, 245, 255), alpha);
    const ImU32 purple = mo::with_alpha(IM_COL32(179, 134, 245, 255), alpha);
    for (const auto& graph : {fps_graph, read_graph}) {
        draw->AddRectFilled(graph.Min, graph.Max, mo::with_alpha(c_background, alpha * .45f), px(5));
        draw->AddLine({graph.Min.x, graph.GetCenter().y}, {graph.Max.x, graph.GetCenter().y}, mo::with_alpha(c_border, alpha * .65f));
    }
    session_plot(draw, monitor, fps_graph, fps_max, blue,
        [](const SessionMetric& p) { return p.fps; }, [](const SessionMetric& p) { return p.rendering; }, px(1));
    text(history.Min + px(18, 126), "Reader latency", true, 11);
    if (read_p95 >= 0) snprintf(label, sizeof(label), "95th percentile %.2f ms", read_p95);
    else snprintf(label, sizeof(label), "Waiting for current data");
    text({history.Max.x - px(195), history.Min.y + px(126)}, label, true, 10);
    session_plot(draw, monitor, read_graph, read_max, purple,
        [](const SessionMetric& p) { return p.read_ms; }, [](const SessionMetric& p) { return p.current_data; }, px(1));
    text(history.Min + px(18, 205), "60s ago", true, 10);
    text({history.Max.x - px(48), history.Min.y + px(205)}, monitor.frozen ? "Held" : "Now", true, 10);
    if (ImGui::IsMouseHoveringRect(fps_graph.Min, fps_graph.Max) || ImGui::IsMouseHoveringRect(read_graph.Min, read_graph.Max)) {
        const bool fps_hover = ImGui::IsMouseHoveringRect(fps_graph.Min, fps_graph.Max);
        const auto& graph = fps_hover ? fps_graph : read_graph;
        const double age = (graph.Max.x - ImGui::GetIO().MousePos.x) / graph.GetWidth() * 60000.;
        const SessionMetric* nearest{}; double distance = 1000.;
        for (size_t i = 0; i < monitor.size(); ++i) {
            const auto& p = monitor.at(i);
            if (p.time_ms > monitor.graph_time()) continue;
            const double d = std::abs(double(monitor.graph_time() - p.time_ms) - age);
            if (d < distance) { distance = d; nearest = &p; }
        }
        if (nearest) {
            ImGui::BeginTooltip();
            ImGui::Text("%.1f seconds before %s", double(monitor.graph_time() - nearest->time_ms) / 1000., monitor.frozen ? "freeze" : "now");
            if (nearest->rendering) ImGui::Text("Overlay: %.0f FPS / work %.2f ms", nearest->fps, nearest->work_ms);
            else ImGui::TextUnformatted("Overlay rendering paused");
            if (nearest->current_data) ImGui::Text("Read: %.2f ms / age %.0f ms", nearest->read_ms, nearest->age_ms);
            else ImGui::TextUnformatted("Game data unavailable or stale");
            ImGui::Text("Present %.2f ms / GPU wait %.2f ms", nearest->present_ms, nearest->queue_wait_ms);
            ImGui::EndTooltip();
        }
    }
    float row_y = 47.f;
    auto row = [&](const char* key, const char* value) {
        text(view.Min + px(18, row_y), key, true, 11);
        clipped_text(view.Min + px(90, row_y), view.GetWidth() - px(108), value, false, 11);
        row_y += 21.f;
    };
    row("Hero", snap.hero.valid ? snap.hero.name.c_str() : "Waiting");
    row("View", snap.hero.sniper_scoped ? "Assassinate" : "Render camera");
    if (snap.projection.valid) snprintf(label, sizeof(label), "%.1f x %.1f deg", snap.projection.horizontal_fov, snap.projection.vertical_fov);
    else snprintf(label, sizeof(label), "Unavailable");
    row("FOV H / V", label);
    if (snap.projection.valid) snprintf(label, sizeof(label), "%.3f", snap.projection.aspect);
    else snprintf(label, sizeof(label), "Unavailable");
    row("Aspect", label);
    row("Map", snap.map_name.empty() ? "Waiting" : snap.map_name.c_str());
    snprintf(label, sizeof(label), "%zu / %d missing anchors", snap.players.size(), snap.missing_anchors);
    row("Players", label);
    snprintf(label, sizeof(label), "%.2f ms", current.input_queue_ms);
    row("Input queue", label);
    clipped_text(view.Min + px(18, 201), view.GetWidth() - px(36), monitor.health(input_ready), true, 11);

    ImRect tools(origin + px(0, 417), {body.Max.x, origin.y + px(532)});
    card(tools, "SESSION TOOLS", icons::id::code);
    ImGui::PushID("session_monitor");
    auto button = [&](const char* title, float x, float width) {
        ImGui::SetCursorScreenPos(tools.Min + px(x, 44));
        return extra_action(title,px(width),false,px(30));
    };
    if (button(monitor.frozen ? "Resume" : "Freeze", 18, 88)) monitor.frozen = !monitor.frozen;
    if (button("Reset history", 114, 118)) monitor.reset();
    if (button("Export report", 240, 126)) monitor.export_report(reports, snap, input_status);
    if (button("Open reports", 374, 122)) {
        std::error_code error; std::filesystem::create_directories(reports, error);
        if (error) monitor.export_status = "Could not open reports folder: " + error.message();
        else if (reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", reports.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            monitor.export_status = "Windows could not open the reports folder.";
    }
    clipped_text(tools.Min + px(18, 87), tools.GetWidth() - px(36), monitor.export_status.c_str(), true, 11);
    ImGui::PopID();
    return false;
}

inline void draw_session_hud(ImDrawList* draw, const SessionMonitor& monitor, float width, float scale,float opacity=1) {
    const auto& p = monitor.latest;
    char value[32]{},detail[96]{};snprintf(value,sizeof(value),"%.0f FPS",p.fps);
    if(p.current_data)snprintf(detail,sizeof(detail),"Read %.2f ms  /  Input %.1f ms",p.read_ms,p.input_queue_ms);
    else strcpy_s(detail,"Game data stale");
    draw_hud_card(draw,{width-(hud_card_width+24)*scale,24*scale},scale,"Session",value,detail,HudGlyph::Activity,p.current_data?solace::c_foreground:hud_warning(),opacity);
}
} // namespace overlay
