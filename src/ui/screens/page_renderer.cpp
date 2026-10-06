#include "ui/screens/page_renderer.h"

#include "application/brand.h"
#include "assets/avatars.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/scroll.h"
#include "ui/controls/widgets.h"
#include <cstring>

namespace solace
{
namespace
{

bool check(const char* id, const ImVec2& pos, bool* checked, const char* label)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    checkbox_state* st = ui_runtime::animation_state<checkbox_state>(window->GetID("cb"));
    checkbox_update(*st, *checked, ImGui::GetIO().DeltaTime);
    const bool hit = checkbox_draw("c", *st, checked, label, pos, false);
    ImGui::PopID();
    return hit;
}

bool action(const char* id, const ImVec2& pos, float width, button_state state, const char* label)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    stateful_button_state* st =
        ui_runtime::animation_state<stateful_button_state>(window->GetID("btn"));
    stateful_button_update(*st, state, label, ImGui::GetIO().DeltaTime);
    const bool hit = stateful_button_draw("b", *st, state, pos, px(width), false);
    ImGui::PopID();
    return hit;
}

float heading(ImDrawList* dl, const ImVec2& pos, const char* title, const char* sub, float width,
              float alpha)
{
    ImFont* h = font_semibold(20.f);
    draw_text_tracked(dl, h, ImVec2(pos.x, pos.y + line_top(h, px(28.f))),
                      mo::with_alpha(c_foreground, alpha), title, px(-0.4f));

    ImFont* d = font_regular(text_sm);
    const int lines = ImMax(1, wrapped_line_count(d, sub, width));
    draw_text_wrapped(dl, d, ImVec2(pos.x, pos.y + px(32.f)),
                      mo::with_alpha(c_muted_foreground, alpha), sub, width, px(leading_sm));

    return px(32.f) + px(leading_sm) * (float)lines + px(12.f);
}

void panel(ImDrawList* dl, const ImRect& r, float alpha)
{
    dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_card, 0.5f * alpha), px(16.f));
    dl->AddRect(ImVec2(r.Min.x + px(0.5f), r.Min.y + px(0.5f)),
                ImVec2(r.Max.x - px(0.5f), r.Max.y - px(0.5f)), mo::with_alpha(c_border, alpha),
                px(16.f), px(1.f), ImDrawFlags_None);
}

float pill(ImDrawList* dl, const ImVec2& at, const char* text, ImU32 col, float alpha)
{
    ImFont* f = font_medium(text_xs);
    const float w = text_width(f, text) + px(16.f);
    const float h = px(22.f);

    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), mo::with_alpha(col, 0.13f * alpha), h * 0.5f);
    draw_text(dl, f, ImVec2(at.x + px(8.f), at.y + h * 0.5f - f->LegacySize * 0.5f),
              mo::with_alpha(col, alpha), text);
    return w;
}

void meter(ImDrawList* dl, const ImRect& r, float t, ImU32 col, float alpha)
{
    const float h = r.GetHeight();
    dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_muted_foreground, 0.16f * alpha), h * 0.5f);
    const float fill = r.GetWidth() * ImClamp(t, 0.f, 1.f);
    if (fill > 0.5f)
        dl->AddRectFilled(r.Min, ImVec2(r.Min.x + fill, r.Max.y), mo::with_alpha(col, alpha),
                          h * 0.5f);
}

float series_at(const float* v, int n, float u)
{
    if (n <= 0)
        return 0.f;
    if (n == 1)
        return v[0];
    const float f = ImClamp(u, 0.f, 1.f) * (float)(n - 1);
    const int i = ImMin((int)f, n - 2);
    const float k = f - (float)i;

    const float e = k * k * (3.f - 2.f * k);
    return v[i] + (v[i + 1] - v[i]) * e;
}

float edge_window(float x, float w, float fade)
{
    if (fade <= 0.f)
        return 1.f;
    const float a = ImClamp(x / fade, 0.f, 1.f);
    const float b = ImClamp((w - x) / fade, 0.f, 1.f);
    const float t = ImMin(a, b);
    return t * t * (3.f - 2.f * t);
}

void series_fill(ImDrawList* dl, const ImRect& r, const float* v, int n, float lo, float hi,
                 ImU32 col, float alpha, float reveal, float fade = 0.f)
{
    const float span = ImMax(hi - lo, 1e-4f);
    const float full = ImMax(r.GetWidth(), 1.f);
    const float w = full * ImClamp(reveal, 0.f, 1.f);
    const float step = px(2.f);

    for (float sx = 0.f; sx < w; sx += step)
    {
        const float x0 = r.Min.x + sx;
        const float x1 = ImMin(x0 + step, r.Min.x + w);
        const float u = (sx + step * 0.5f) / full;
        const float y = r.Max.y - (series_at(v, n, u) - lo) / span * r.GetHeight();

        const float a = alpha * edge_window(sx + step * 0.5f, full, fade);
        dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x1, r.Max.y),
                                    mo::with_alpha(col, 0.26f * a), mo::with_alpha(col, 0.26f * a),
                                    mo::with_alpha(col, 0.01f * a), mo::with_alpha(col, 0.01f * a));
    }
}

void series_line(ImDrawList* dl, const ImRect& r, const float* v, int n, float lo, float hi,
                 ImU32 col, float alpha, float reveal, float thickness, float fade = 0.f)
{
    const float span = ImMax(hi - lo, 1e-4f);
    const float full = ImMax(r.GetWidth(), 1.f);
    const float w = full * ImClamp(reveal, 0.f, 1.f);
    const float step = px(2.f);

    auto at = [&](float sx)
    {
        return ImVec2(r.Min.x + sx,
                      r.Max.y - (series_at(v, n, sx / full) - lo) / span * r.GetHeight());
    };

    if (fade <= 0.f)
    {
        dl->PathClear();
        for (float sx = 0.f; sx <= w; sx += step)
            dl->PathLineTo(at(sx));
        if (dl->_Path.Size > 1)
            dl->PathStroke(mo::with_alpha(col, alpha), thickness, ImDrawFlags_None);
        return;
    }

    for (float sx = 0.f; sx < w; sx += step)
    {
        const float nx = ImMin(sx + step, w);
        const float a = alpha * edge_window(sx + (nx - sx) * 0.5f, full, fade);
        dl->AddLine(at(sx), at(nx), mo::with_alpha(col, a), thickness);
    }
}

void sparkline(ImDrawList* dl, const ImRect& r, const float* v, int n, ImU32 col, float alpha,
               float reveal)
{
    float lo = v[0], hi = v[0];
    for (int i = 1; i < n; i++)
    {
        lo = ImMin(lo, v[i]);
        hi = ImMax(hi, v[i]);
    }

    const float pad = ImMax((hi - lo) * 0.18f, 0.5f);
    lo -= pad;
    hi += pad;

    const float fade = px(22.f);
    series_fill(dl, r, v, n, lo, hi, col, alpha, reveal, fade);
    series_line(dl, r, v, n, lo, hi, col, alpha, reveal, px(1.5f), fade);
}

void row_label(ImDrawList* dl, const ImVec2& pos, const char* label, const char* hint, float alpha,
               float max_width = 0.f)
{
    ImFont* f = font_medium(text_sm);
    if (max_width > 0.f)
        draw_text_ellipsis(dl, f, ImVec2(pos.x, pos.y), mo::with_alpha(c_foreground, alpha), label,
                           max_width);
    else
        draw_text(dl, f, ImVec2(pos.x, pos.y), mo::with_alpha(c_foreground, alpha), label);

    if (hint)
    {

        ImFont* g = font_medium(text_xs);
        if (max_width > 0.f)
            draw_text_ellipsis(dl, g, ImVec2(pos.x, pos.y + px(18.f)),
                               mo::with_alpha(c_muted_foreground, alpha), hint, max_width);
        else
            draw_text(dl, g, ImVec2(pos.x, pos.y + px(18.f)),
                      mo::with_alpha(c_muted_foreground, alpha), hint);
    }
}

bool row_hit(ImDrawList* dl, const char* id, const ImRect& r, float alpha, bool draw_hover = true)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    const ImGuiID item = window->GetID("row");
    ImGui::PopID();

    ImGui::SetCursorScreenPos(r.Min);
    ImGui::ItemSize(ImVec2(0.f, 0.f));
    ImGui::ItemAdd(r, item);

    bool hovered = false, held = false;
    const bool pressed = !pointer_claimed() && ImGui::ButtonBehavior(r, item, &hovered, &held);

    if (draw_hover && (hovered || held))
        dl->AddRectFilled(r.Min, r.Max,
                          mo::with_alpha(c_foreground, (held ? 0.06f : 0.035f) * alpha), px(10.f));

    return pressed;
}

void empty_state(ImDrawList* dl, const ImRect& card, const char* msg, float alpha)
{
    ImFont* f = font_regular(text_sm);
    draw_text(dl, f,
              ImVec2(card.GetCenter().x - text_width(f, msg) * 0.5f,
                     card.GetCenter().y - f->LegacySize * 0.5f),
              mo::with_alpha(c_muted_foreground, alpha), msg);
}

void hairline(ImDrawList* dl, const ImRect& card, float y, float alpha)
{
    dl->AddRectFilled(ImVec2(card.Min.x + px(sp_4), y - px(0.5f)),
                      ImVec2(card.Max.x - px(sp_4), y + px(0.5f)), mo::with_alpha(c_border, alpha));
}

void chip(ImDrawList* dl, const ImVec2& at, float size, float round, const char* label, ImU32 bg,
          ImU32 fg, int person = -1)
{
    if (person >= 0 && avatars::draw(dl, avatars::other(person), at, size, 1.f))
        return;

    dl->AddRectFilled(at, ImVec2(at.x + size, at.y + size), bg, round);

    ImFont* f = font_semibold(text_xs);
    draw_text(dl, f,
              ImVec2(at.x + size * 0.5f - text_width(f, label) * 0.5f,
                     at.y + size * 0.5f - f->LegacySize * 0.5f),
              fg, label);
}

void initials_of(const char* name, char out[3])
{
    out[0] = name && *name ? name[0] : '?';
    const char* space = name ? strchr(name, ' ') : nullptr;
    out[1] = space ? space[1] : 0;
    out[2] = 0;
}

enum act_kind
{
    act_stage = 0,
    act_note,
    act_shipped,
    act_task,
    act_authors,
    act_quiet
};

const ImU32 k_act_tone[] = {
    IM_COL32(0x4C, 0x8D, 0xF6, 0xFF),
    IM_COL32(0x9A, 0x7C, 0xF7, 0xFF),
    c_success,
    c_amber_400,
    IM_COL32(0x36, 0xB8, 0xC0, 0xFF),
    c_muted_foreground,
};

struct activity_row
{
    const char* who;
    const char* what;
    const char* when;
    act_kind kind;
};

const activity_row k_activity[] = {
    {"Graphics", "set Shadows to High", "12m ago", act_stage},
    {"Camera", "turned Depth of field on", "48m ago", act_note},
    {"Display", "set Film grain to 25%", "2h ago", act_shipped},
    {"Audio", "muted the menu music", "5h ago", act_task},
    {"Profile", "saved the Competitive preset", "Yesterday", act_stage},
    {"Engine", "loaded Solace Alpha 1.4.2", "Yesterday", act_authors},
    {"Cache", "cleared 14 stale shaders", "Monday", act_quiet},
};

float aside_head(ImDrawList* dl, const ImVec2& pos, ImU32 col, float alpha, const char* label)
{
    ImFont* lf = font_regular(10.f);
    draw_text_tracked(dl, lf, ImVec2(pos.x, pos.y + line_top(lf, px(15.f))),
                      mo::with_alpha(col, alpha), label, px(1.6f));
    return px(24.f);
}

struct stat_line
{
    const char* label;
    const char* value;
};

float aside_stats(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                  const stat_line* rows, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    const float row_h = px(38.f);
    const ImRect card(ImVec2(pos.x, y),
                      ImVec2(pos.x + width, y + px(sp_3) * 2.f + row_h * (float)count));
    panel(dl, card, alpha);

    ImFont* lf = font_regular(text_sm);
    ImFont* vf = font_medium(text_sm);

    for (int i = 0; i < count; i++)
    {
        const float ry = card.Min.y + px(sp_3) + row_h * (float)i;
        const float mid = ry + row_h * 0.5f;

        if (i > 0)
            hairline(dl, card, ry, alpha * 0.9f);

        const float vw = text_width(vf, rows[i].value);
        draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), mid - lf->LegacySize * 0.5f),
                           mo::with_alpha(c_muted_foreground, alpha), rows[i].label,
                           width - px(sp_4) * 2.f - vw - px(12.f));
        draw_text(dl, vf, ImVec2(card.Max.x - px(sp_4) - vw, mid - vf->LegacySize * 0.5f),
                  mo::with_alpha(c_foreground, alpha), rows[i].value);
    }

    return (card.Max.y - pos.y);
}

struct badge_line
{
    const char* label;
    const char* value;
    badge_status tone;
};

float aside_badges(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                   const badge_line* rows, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    const float row_h = px(38.f);
    const ImRect card(ImVec2(pos.x, y),
                      ImVec2(pos.x + width, y + px(sp_3) * 2.f + row_h * (float)count));
    panel(dl, card, alpha);

    ImFont* lf = font_regular(text_sm);

    for (int i = 0; i < count; i++)
    {
        const float ry = card.Min.y + px(sp_3) + row_h * (float)i;
        const float mid = ry + row_h * 0.5f;

        if (i > 0)
            hairline(dl, card, ry, alpha * 0.9f);

        const float bw = badge_width(rows[i].value);
        draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), mid - lf->LegacySize * 0.5f),
                           mo::with_alpha(c_muted_foreground, alpha), rows[i].label,
                           width - px(sp_4) * 2.f - bw - px(12.f));

        char bid[24];
        ImFormatString(bid, IM_ARRAYSIZE(bid), "%s%d", title, i);
        badge(bid, dl, ImVec2(card.Max.x - px(sp_4) - bw, mid - px(12.f)), rows[i].value,
              rows[i].tone, false, alpha);
    }

    return (card.Max.y - pos.y);
}

float aside_lines(ImDrawList* dl, const ImVec2& pos, float width, float alpha, const char* title,
                  const char* const* lines, int count, float card_gap = 0.f)
{
    float y = pos.y + aside_head(dl, pos, c_muted_foreground, alpha, title) + card_gap;

    ImFont* f = font_regular(text_sm);
    const float inner = width - px(sp_4) * 2.f;

    float body = px(sp_3) * 2.f;
    for (int i = 0; i < count; i++)
        body += px(leading_sm) * (float)ImMax(1, wrapped_line_count(f, lines[i], inner)) + px(sp_3);
    body -= px(sp_3);

    const ImRect card(ImVec2(pos.x, y), ImVec2(pos.x + width, y + body));
    panel(dl, card, alpha);

    float ly = card.Min.y + px(sp_3);
    for (int i = 0; i < count; i++)
    {
        const int n = ImMax(1, wrapped_line_count(f, lines[i], inner));
        draw_text_wrapped(dl, f, ImVec2(card.Min.x + px(sp_4), ly),
                          mo::with_alpha(c_muted_foreground, alpha), lines[i], inner,
                          px(leading_sm));
        ly += px(leading_sm) * (float)n + px(sp_3);
    }

    return (card.Max.y - pos.y);
}

float activity(ImDrawList* dl, const ImVec2& pos, float width, float alpha)
{
    ImFont* lf = font_regular(10.f);
    draw_text_tracked(dl, lf, ImVec2(pos.x, pos.y + line_top(lf, px(15.f))),
                      mo::with_alpha(c_muted_foreground, alpha), "RECENT ACTIVITY", px(1.6f));

    const float top = pos.y + px(24.f);
    const int rows = IM_ARRAYSIZE(k_activity);
    const ImRect card(ImVec2(pos.x, top),
                      ImVec2(pos.x + width, top + px(sp_4) * 2.f + px(52.f) * (float)rows));
    panel(dl, card, alpha);

    float ry = card.Min.y + px(sp_4);
    for (int i = 0; i < rows; i++)
    {

        const ImVec2 av(card.Min.x + px(sp_4), ry + px(10.f));
        char initial[3];
        initials_of(k_activity[i].who, initial);
        chip(dl, av, px(28.f), px(14.f), initial, mo::with_alpha(c_foreground, 0.06f * alpha),
             mo::with_alpha(c_muted_foreground, alpha), i);

        {
            const ImVec2 at(av.x + px(22.f), av.y + px(22.f));
            const ImU32 tone =
                k_act_tone[ImClamp((int)k_activity[i].kind, 0, IM_ARRAYSIZE(k_act_tone) - 1)];
            dl->AddCircleFilled(at, px(6.f), mo::with_alpha(c_background, alpha));
            dl->AddCircleFilled(at, px(4.f), mo::with_alpha(tone, alpha));
        }

        row_label(dl, ImVec2(card.Min.x + px(56.f), ry + px(8.f)), k_activity[i].who,
                  k_activity[i].what, alpha);

        ImFont* wf = font_regular(text_xs);
        draw_text(dl, wf,
                  ImVec2(card.Max.x - px(sp_4) - text_width(wf, k_activity[i].when), ry + px(10.f)),
                  mo::with_alpha(c_muted_foreground, alpha), k_activity[i].when);

        if (i + 1 < rows)
            dl->AddRectFilled(ImVec2(card.Min.x + px(sp_4), ry + px(52.f) - px(0.5f)),
                              ImVec2(card.Max.x - px(sp_4), ry + px(52.f) + px(0.5f)),
                              mo::with_alpha(c_border, alpha));

        ry += px(52.f);
    }
    return card.Max.y - pos.y;
}

constexpr int k_module_count = 6;
constexpr int k_task_count = 8;
constexpr int k_message_count = 8;
constexpr int k_range_sample_count = 13;

struct page_state
{

    smooth_scroll body[route_count];
    float content[route_count] = {};

    bool module_armed[k_module_count] = {true, false, true, false, true, false};
    bool module_compact = false;

    float module_value[k_module_count] = {220.f, 65.f, 35.f, 40.f, 25.f, 0.f};
    mo::spring module_detail[k_module_count];

    float dash_reveal = 0.f;

    int last_nav = -1;
    float entered = 0.f;

    bool pref_on[7] = {true, true, false, true, false, true, false};
    bool pref_open[3] = {true, true, false};
    mo::spring pref_detail[7];
    int pref_theme = 1;
    int pref_digest_day = 0;
    float pref_quiet = 0.34f;
    float reset_cascade = 1e6f;

    int notif_tab = 0;
    bool notif_read[7] = {false, false, true, false, true, true, true};
    bool notif_gone[7] = {};
    mo::spring notif_height[7];
    float notif_slide[7] = {};
    float mark_cascade = 1e6f;
    int notif_undo = -1;
    float undo_timer = 0.f;

    int stage = 1;
    int author = 0;
    float projected_installs = 48000.f;
    button_state save = btn_idle;
    float save_timer = 0.f;

    bool automations[4] = {true, true, false, true};

    bool tasks[k_task_count] = {true, false, false, true, false, false, true, false};
    button_state sweep = btn_idle;
    float sweep_timer = 0.f;

    int range = 1;

    bool profile_prefs[3] = {true, false, true};
    button_state profile_save = btn_idle;
    float profile_timer = 0.f;

    int search_tab = 0;
    bool search_bodies = true;
    button_state clear_history = btn_idle;
    float clear_timer = 0.f;
    bool history_cleared = false;

    int answer_style = 0;
    float creativity = 0.4f;
    button_state ask = btn_idle;
    float ask_timer = 0.f;

    int message_tab = 0;
    bool message_read[k_message_count] = {false, true, false, true, true, true, false, true};
    button_state mark_all = btn_idle;
    float mark_timer = 0.f;

    int config_sort = 0;
    bool only_mine = false;

    int notes_tab = 0;
};

page_state& state()
{
    static page_state s;
    return s;
}

const char* const k_stage_options[] = {"Draft", "Internal", "Playtest", "Candidate", "Live"};
const char* const k_author_options[] = {brand::user_name, "Corvid", "Kestrel", "Unassigned"};

struct module_row
{
    const char* name;
    const char* role;
    const char* knob;
    const char* unit;
    float min_v, max_v;
    int decimals;
    int ticks;
};
const module_row k_modules[] = {
    {"Shadows", "Graphics - High", "Distance", " m", 50.f, 400.f, 0, 0},
    {"Ambient occlusion", "Graphics - GTAO", "Strength", "%", 0.f, 100.f, 0, 4},
    {"Motion blur", "Camera - Per-object", "Amount", "%", 0.f, 100.f, 0, 4},
    {"Depth of field", "Camera - Bokeh", "Strength", "%", 0.f, 100.f, 0, 4},
    {"Film grain", "Display - Filmic", "Amount", "%", 0.f, 100.f, 0, 4},
    {"V-Sync", "Display - Adaptive", nullptr, "", 0.f, 0.f, 0, 0},
};
static_assert(IM_ARRAYSIZE(k_modules) == k_module_count, "module_rows is per module");

struct automation
{
    const char* name;
    const char* detail;
};
const automation k_automations[] = {
    {"Apply on launch", "The Competitive preset the moment the game starts"},
    {"Panic key", "Reset everything to defaults on End"},
    {"Re-detect", "Probe the GPU again after a driver change"},
    {"Weekly digest", "Send Monday 09:00 in local time"},
};

struct search_hit
{
    const char* query;
    const char* count;
};
const search_hit k_recent[] = {
    {"frame pacing", "18 results"},  {"key:insert overlay", "7 results"},
    {"input latency", "24 results"}, {"stutter", "3 results"},
    {"shader cache", "11 results"},
};
const search_hit k_saved[] = {
    {"Everything I tune", "Updated 2h ago"},
    {"Unstable presets", "Updated Monday"},
    {"Rendering, all", "Updated last week"},
};

struct chat_turn
{
    bool from_user;
    const char* text;
};
const chat_turn k_thread[] = {
    {true, "Why does frame time spike above 16 ms?"},
    {false, "Two reasons. Shadow distance updates every frame instead of every third, and "
            "motion blur is still active underneath it. Cache the value and update on change."},
    {true, "Write me the change check."},
};
const char* const k_answer_styles[] = {"Fast", "Balanced", "Thorough"};

struct message
{
    const char* who;
    const char* subject;
    const char* when;
    bool mention;
};
const message k_messages[] = {
    {"Corvid", "Shadows flicker over water", "09:41", true},
    {"Kestrel", "Re: controller deadzone", "08:12", false},
    {"Solace Lab", "Photo mode 2.1 is ready", "Yesterday", false},
    {"Halcyon", "Notes from the playtest", "Yesterday", true},
    {"Orrin", "Signed off - clean build inside", "Monday", false},
    {"Pell", "Projection is ready for review", "Monday", false},
    {"Vermillion", "Two players joined the playtest", "Friday", true},
    {"Cinder", "Archived 14 old presets", "Friday", false},
};
static_assert(IM_ARRAYSIZE(k_messages) == k_message_count, "message_read is per message");

struct config_row
{
    const char* name;
    const char* file;
    const char* loads;
    const char* author;
};
const config_row k_configs[] = {
    {"Competitive", "competitive.ini", "412k", brand::user_name},
    {"Cinematic", "cinematic.ini", "188k", "Corvid"},
    {"Performance", "performance.ini", "84k", brand::user_name},
    {"Balanced", "balanced.ini", "61k", "Kestrel"},
    {"Handheld", "handheld.ini", "26k", brand::user_name},
    {"Streaming", "streaming.ini", "54k", "Corvid"},
    {"Photo mode", "photo.ini", "47k", brand::user_name},
    {"Benchmark", "benchmark.ini", "33k", "Kestrel"},
    {"Minimal", "minimal.ini", "19k", "Corvid"},
};
const char* const k_config_sorts[] = {"Most used", "Newest first", "Alphabetical", "By author"};

struct note
{
    const char* title;
    const char* body;
    const char* meta;
    int bucket;
};

const note k_notes[] = {
    {"Shadows - what actually flickers",
     "It is not the shadow system. Two reports pin it to a texture stream that only "
     "fires on ultra, and both had render scale above 100%.",
     "You - 12m ago", 1},
    {"Playtest notes",
     "The build went out fine. What hurt was the week after: nobody owned the report list, "
     "so three of the six sat untouched.",
     "Kestrel - Yesterday", 1},
    {"Keybinds, next patch",
     "Grouping by category rather than by hand. Rough shape only so far - the modifier "
     "assumption is the part I am least sure about.",
     "You - Monday", 2},
    {"Settings guide rewrite",
     "Five steps down to three. The middle one was doing nothing that the first did not "
     "already say.",
     "Halcyon - Last week", 0},
    {"What Solace Lab actually asked for",
     "Not a rewrite. They want the preset export to carry the build number, which we "
     "already store and simply do not print.",
     "Corvid - Last week", 1},
    {"Versioning, thinking aloud",
     "Per-preset versions stop making sense the moment a patch ships a migrator. Nobody "
     "has asked yet, but Vermillion will.",
     "You - Last week", 2},
};
const char* const k_note_tabs[] = {"All", "Shared", "Drafts"};

struct field
{
    const char* label;
    const char* value;
};
const field k_profile_fields[] = {
    {"Full name", brand::user_name},
    {"GitHub", brand::user_github},
    {"Role", "Graphics and camera"},
    {"Time zone", "Europe/London (GMT+1)"},
};

struct preference
{
    const char* name;
    const char* detail;
};

const stat_line k_credits[] = {
    {"Built by", brand::author},
    {"Source", brand::repo},
    {"Runtime", "Dear ImGui 1.92.9b - DirectX 11"},
    {"Type", "Geist, through FreeType"},
};

const preference k_profile_prefs[] = {
    {"Patch emails", "Every patch, the day it goes live"},
    {"Weekly digest", "Monday 09:00, in your time zone"},
    {"Mentions", "Notify me when someone writes my name"},
};

struct kpi
{
    const char* label;
    float value;
    const char* prefix;
    const char* suffix;
    int decimals;
    const char* delta;
    bool good;
};

struct range_data
{
    kpi stats[4];
    const float spark[4][k_range_sample_count];
    int spark_n;
    const float frames[k_range_sample_count];
    int frame_count;
    const char* x_labels[k_range_sample_count];
    const char* axis_hi;
    const char* axis_mid;
    const char* total;
    const char* total_note;
};

const range_data k_ranges[3] = {

    {
        {{"Frames", 412.f, "", "k", 0, "+6.1%", true},
         {"Win rate", 34.f, "", "%", 0, "+2.0 pts", true},
         {"Frame time", 24.f, "", " ms", 0, "-1 ms", true},
         {"Sessions", 68.f, "", "", 0, "+12", true}},
        {{41, 38, 44, 47, 43, 52, 58, 0, 0, 0, 0, 0, 0},
         {29, 31, 30, 33, 32, 34, 34, 0, 0, 0, 0, 0, 0},
         {27, 26, 26, 25, 25, 24, 24, 0, 0, 0, 0, 0, 0},
         {6, 9, 8, 11, 10, 12, 12, 0, 0, 0, 0, 0, 0}},
        7,
        {48, 44, 57, 52, 66, 61, 74, 0, 0, 0, 0, 0, 0},
        7,
        {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", 0, 0, 0, 0, 0, 0},
        "80k",
        "40k",
        "402k",
        "+6.1% on last week",
    },

    {
        {{"Frames", 1.24f, "", "M", 2, "+8.2%", true},
         {"Win rate", 31.f, "", "%", 0, "+1.4 pts", true},
         {"Frame time", 26.f, "", " ms", 0, "-3 ms", true},
         {"Sessions", 214.f, "", "", 0, "+9%", true}},
        {{88, 96, 91, 104, 112, 108, 124, 131, 127, 138, 0, 0, 0},
         {27, 29, 28, 30, 29, 31, 30, 32, 31, 31, 0, 0, 0},
         {31, 30, 30, 29, 28, 28, 27, 27, 26, 26, 0, 0, 0},
         {14, 19, 17, 22, 25, 21, 28, 26, 31, 33, 0, 0, 0}},
        10,
        {96, 112, 104, 138, 126, 151, 144, 168, 159, 186, 0, 0, 0},
        10,
        {"1", "4", "7", "10", "13", "16", "19", "22", "25", "28", 0, 0, 0},
        "200k",
        "100k",
        "1.38M",
        "+8.2% on last month",
    },

    {
        {{"Frames", 3.86f, "", "M", 2, "+11.5%", true},
         {"Win rate", 29.f, "", "%", 0, "-0.6 pts", false},
         {"Frame time", 28.f, "", " ms", 0, "+2 ms", false},
         {"Sessions", 640.f, "", "", 0, "+14%", true}},
        {{210, 246, 232, 268, 291, 274, 318, 335, 322, 368, 381, 364, 412},
         {31, 30, 32, 30, 29, 30, 28, 29, 28, 29, 28, 29, 29},
         {25, 26, 25, 27, 26, 28, 27, 29, 28, 28, 29, 28, 28},
         {38, 45, 41, 52, 48, 57, 54, 61, 58, 66, 71, 68, 74}},
        13,
        {240, 286, 262, 318, 341, 304, 368, 395, 362, 428, 451, 414, 492},
        13,
        {"W1", "W2", "W3", "W4", "W5", "W6", "W7", "W8", "W9", "W10", "W11", "W12", "W13"},
        "520k",
        "260k",
        "4.66M",
        "+11.5% on last quarter",
    },
};

struct stage_row
{
    const char* name;
    const char* count;
    const char* value;
    float weight;
};
const stage_row k_stages[] = {
    {"Draft", "38 patches", "1.42M", 1.00f},   {"Internal", "24 patches", "1.08M", 0.76f},
    {"Playtest", "16 patches", "742k", 0.52f}, {"Candidate", "9 patches", "418k", 0.29f},
    {"Live", "4 patches", "196k", 0.14f},
};

struct build_row
{
    const char* mod;
    const char* author;
    const char* stage;
    const char* value;
    const char* close;
    int person;
    int tone;
};
const build_row k_builds[] = {
    {"1.4.2 Hotfix", brand::user_name, "Candidate", "248k", "12 Sep", 0, 0},
    {"1.5 Seasons", "Corvid", "Playtest", "188k", "26 Sep", 1, 1},
    {"Photo mode", brand::user_name, "Internal", "84k", "3 Oct", 0, 2},
    {"Controller fix", "Kestrel", "Live", "61k", "8 Sep", 2, 3},
    {"Ray tracing", "Halcyon", "Draft", "26k", "17 Oct", 3, 2},
};

struct author_row
{
    const char* name;
    const char* figure;
    float attainment;
    int person;
};
const author_row k_authors[] = {
    {brand::user_name, "412k", 1.03f, 0},
    {"Corvid", "368k", 0.92f, 1},
    {"Kestrel", "286k", 0.71f, 2},
    {"Halcyon", "194k", 0.48f, 3},
};

enum pref_extra
{
    extra_none = 0,
    extra_theme,
    extra_digest,
    extra_quiet
};

struct pref_row
{
    const char* label;
    const char* off_text;
    const char* on_text;
    pref_extra extra;
};

struct pref_group
{
    const char* title;
    int first, count;
};

const pref_row k_prefs[] = {
    {"Match the system theme", "Locked to whichever you last picked.",
     "Follows the machine from dusk until it changes back.", extra_theme},
    {"Reduce motion", "Transitions run at their usual length.",
     "Everything still moves, but only far enough to say where it went.", extra_none},

    {"Patch emails", "Nothing about new patches will reach you.",
     "Every patch, the day it goes live.", extra_none},
    {"Weekly digest", "No summary is sent.", "One summary a week, in your own time zone.",
     extra_digest},
    {"Mentions", "Only counted, never announced.", "Notify me the moment someone writes my name.",
     extra_none},
    {"Quiet hours", "Notifications arrive whenever they arrive.",
     "Held until morning, unless someone marks it urgent.", extra_quiet},

    {"Usage analytics", "Nothing leaves this machine.", "Anonymous counts only - no content, ever.",
     extra_none},
};

const pref_group k_pref_groups[] = {
    {"Appearance", 0, 2},
    {"Notifications", 2, 4},
    {"Privacy", 6, 1},
};

const char* const k_theme_names[] = {"Light", "Dark", "System"};
const char* const k_digest_days[] = {"Monday", "Wednesday", "Friday", "Sunday"};

struct notification
{
    const char* who;
    const char* did;
    const char* about;
    const char* when;
    int day;
    int person;
    bool mention;
};

const notification k_notifications[] = {
    {"Corvid", "moved", "1.4.2 Hotfix to Candidate", "09:41", 0, 0, false},
    {"Kestrel", "mentioned you in", "Re: controller deadzone", "08:12", 0, 1, true},
    {"Halcyon", "closed", "Playtest notes", "Yesterday", 1, 2, false},
    {"Vermillion", "assigned you", "Draft the keybind layout", "Yesterday", 1, 3, true},
    {"Solace Lab", "shipped", "Photo mode 2.1", "Yesterday", 1, 4, false},
    {brand::user_name, "shared", "Keybinds, next patch", "Monday", 2, 5, false},
    {"Orrin", "commented on", "Ray tracing", "Monday", 2, 6, false},
};

const char* const k_notif_days[] = {"Today", "Yesterday", "Earlier"};
const char* const k_notif_tabs[] = {"All", "Unread", "Mentions"};

const badge_line k_aside_reports[] = {
    {"Unread", "2", badge_info},
    {"Mentions", "2", badge_warn},
    {"On my presets", "3", badge_good},
    {"Archived", "41", badge_neutral},
};

const stat_line k_aside_configs[] = {
    {"Total loads", "771k"},
    {"Presets", "5"},
    {"Average", "154k"},
    {"Most used", "Competitive"},
};

const stat_line k_aside_pipeline[] = {
    {"Committed", "418k"},
    {"Best case", "742k"},
    {"In test", "1.42M"},
    {"Live", "286k"},
};

const stat_line k_aside_tasks[] = {
    {"Due today", "2"},
    {"Due this week", "3"},
    {"Overdue", "1"},
    {"Done this week", "7"},
};

const stat_line k_aside_notes[] = {
    {"Bugs", "6"},
    {"Keybinds", "4"},
    {"Playtests", "3"},
    {"Guides", "2"},
};

const stat_line k_aside_runs[] = {
    {"Apply on launch", "4m ago"},
    {"Panic key", "1h ago"},
    {"Weekly digest", "Monday"},
    {"Re-detect", "Paused"},
};

const stat_line k_aside_sessions[] = {
    {"This machine", "Now"},
    {"iPhone", "2h ago"},
    {"Studio iMac", "Yesterday"},
};

const badge_line k_aside_notif[] = {
    {"All", "7", badge_neutral},
    {"Unread", "2", badge_info},
    {"Mentions", "2", badge_warn},
};

const stat_line k_aside_about[] = {
    {"Game", brand::game},
    {"Plan", "Team"},
    {"Region", "eu-west"},
};

const stat_line k_aside_authors[] = {
    {brand::user_name, "525k"},
    {"Corvid", "242k"},
    {"Kestrel", "80k"},
    {"Halcyon", "33k"},
};

const stat_line k_aside_stagemix[] = {
    {"Draft", "38"},
    {"Internal", "24"},
    {"Playtest", "16"},
    {"Candidate", "9"},
};

const stat_line k_aside_assigned[] = {
    {brand::user_name, "4"},
    {"Corvid", "2"},
    {"Kestrel", "2"},
};

const stat_line k_aside_security[] = {
    {"Two-factor", "On"},
    {"Password", "41d ago"},
    {"Recovery", "Verified"},
};

const stat_line k_aside_storage[] = {
    {"Game", "84.2 GB"},
    {"Presets", "1.2 MB"},
    {"Shaders", "1.4 GB"},
};

const stat_line k_aside_threads[] = {
    {"Shadow flicker", "12m"},
    {"Controller drift", "2h"},
    {"Keybinds", "Mon"},
};

const char* const k_aside_search[] = {
    "F opens the palette from anywhere, and Escape closes it.",
    "Ctrl+B folds the rail away when you want the room.",
    "Results are ranked by what you opened last, not alphabetically.",
};

const char* const k_aside_assistant[] = {
    "Summarise the shadow reports since Tuesday.",
    "Read this crash dump and tell me which system owns it.",
    "What changed in the camera this week?",
};

const char* const k_tasks[] = {
    "Draft the keybind layout",           "Review the shadow flicker reports",
    "Refresh the settings guide",         "Close out the playtest notes",
    "Rebuild the shader cache",           "Send Solace Lab the signed build number",
    "Split the graphics presets by tier", "Chase the controller deadzone fix",
};
static_assert(IM_ARRAYSIZE(k_tasks) == k_task_count, "tasks is per task");
} // namespace

namespace
{

float page_aside(ImDrawList* dl, int nav, const ImVec2& pos, float width, float alpha,
                 float card_gap)
{
    switch (nav)
    {
    case 0:
        return aside_lines(dl, pos, width, alpha, "SHORTCUTS", k_aside_search,
                           IM_ARRAYSIZE(k_aside_search), card_gap);
    case 1:
        return aside_lines(dl, pos, width, alpha, "TRY ASKING", k_aside_assistant,
                           IM_ARRAYSIZE(k_aside_assistant), card_gap);
    case 2:
        return aside_badges(dl, pos, width, alpha, "FILTERS", k_aside_reports,
                            IM_ARRAYSIZE(k_aside_reports), card_gap);

    case 3:
        return activity(dl, pos, width, alpha);

    case 4:
        return aside_stats(dl, pos, width, alpha, "CONFIGS", k_aside_configs,
                           IM_ARRAYSIZE(k_aside_configs), card_gap);
    case 5:
        return aside_stats(dl, pos, width, alpha, "THIS DROP", k_aside_pipeline,
                           IM_ARRAYSIZE(k_aside_pipeline), card_gap);
    case 6:
        return aside_stats(dl, pos, width, alpha, "DUE", k_aside_tasks, IM_ARRAYSIZE(k_aside_tasks),
                           card_gap);
    case 7:
        return aside_stats(dl, pos, width, alpha, "TAGS", k_aside_notes,
                           IM_ARRAYSIZE(k_aside_notes), card_gap);
    case 8:
        return aside_stats(dl, pos, width, alpha, "RECENT RUNS", k_aside_runs,
                           IM_ARRAYSIZE(k_aside_runs), card_gap);
    case 10:
        return aside_stats(dl, pos, width, alpha, "SIGNED IN ON", k_aside_sessions,
                           IM_ARRAYSIZE(k_aside_sessions), card_gap);
    case 11:
        return aside_badges(dl, pos, width, alpha, "FILTERS", k_aside_notif,
                            IM_ARRAYSIZE(k_aside_notif), card_gap);
    case 12:
        return aside_stats(dl, pos, width, alpha, "ABOUT", k_aside_about,
                           IM_ARRAYSIZE(k_aside_about), card_gap);
    default:
        return 0.f;
    }
}

float page_aside_more(ImDrawList* dl, int nav, const ImVec2& pos, float width, float alpha)
{
    switch (nav)
    {
    case 1:
        return aside_stats(dl, pos, width, alpha, "RECENT THREADS", k_aside_threads,
                           IM_ARRAYSIZE(k_aside_threads));
    case 4:
        return aside_stats(dl, pos, width, alpha, "BY AUTHOR", k_aside_authors,
                           IM_ARRAYSIZE(k_aside_authors));
    case 5:
        return aside_stats(dl, pos, width, alpha, "STAGE MIX", k_aside_stagemix,
                           IM_ARRAYSIZE(k_aside_stagemix));
    case 6:
        return aside_stats(dl, pos, width, alpha, "ASSIGNED BY", k_aside_assigned,
                           IM_ARRAYSIZE(k_aside_assigned));
    case 10:
        return aside_stats(dl, pos, width, alpha, "SECURITY", k_aside_security,
                           IM_ARRAYSIZE(k_aside_security));
    case 12:
        return aside_stats(dl, pos, width, alpha, "STORAGE", k_aside_storage,
                           IM_ARRAYSIZE(k_aside_storage));
    default:
        return 0.f;
    }
}
} // namespace

void draw_page(route destination, const char* title, const char* const* subs, int sub_count,
               int* sub, const ImRect& area, float alpha)
{
    const int nav = route_index(destination);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    page_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;

    const float x = area.Min.x;
    const float w = area.GetWidth();

    if (s.last_nav != nav)
    {
        s.last_nav = nav;
        s.entered = 0.f;
    }
    s.entered += dt;

    auto stagger = [&](int index) -> float
    { return mo::EASE_OUT(ImClamp((s.entered - (float)index * 0.045f) / 0.34f, 0.f, 1.f)); };

    const int slot = ImClamp(nav, 0, 12);
    const float measured = s.content[slot] > 0.f ? s.content[slot] : area.GetHeight();
    dl->PushClipRect(area.Min, area.Max, true);
    const float scroll = scroll_area(s.body[slot], area, measured);

    float y = area.Min.y - scroll;

    static const char* const k_account_blurbs[] = {
        "Your account, and what the game is allowed to send you.",
        "Everything the game wanted you to know about, newest first.",
        "How this game behaves, and how loudly.",
    };

    static const char* k_blurbs[] = {
        "Jump anywhere in the game without lifting your hands off the keyboard.",
        "Ask for a settings recommendation, a patch summary, or a read of a crash dump.",
        "Four messages need a reply. Everything else is filed.",
        "Everything you can change, and what it is set to.",
        "Saved presets, who wrote them, and how often they get loaded.",
        "What is in test for the next patch, and how stable it looks.",
        "The short list. Anything older than a fortnight gets a nudge.",
        "Longer-form thinking that does not belong in a changelog.",
        "Rules that run without anyone remembering to run them.",
        "The numbers you check before a patch.",
    };

    const char* blurb =
        (nav >= 10) ? k_account_blurbs[ImClamp(nav - 10, 0, 2)] : k_blurbs[ImClamp(nav, 0, 9)];
    y += heading(dl, ImVec2(x, y), title, blurb, w, alpha);

    if (sub_count > 0)
    {
        tabs("page-tabs", ImVec2(x, y), subs, sub_count, sub, tabs_underline);
        y += tabs_height(tabs_underline) + px(sp_6);
    }

    const float gutter = 0.f;
    const float aside_gap = px(sp_5);
    const bool two_col = (nav != 9) && ((w - gutter) >= px(770.f));

    const float col =
        two_col ? ImMin((w - gutter - aside_gap) * 0.60f, px(520.f)) : ImMin(w, px(440.f));

    const float aside_x = x + col + aside_gap;
    const float aside_w = (w - gutter) - col - aside_gap;
    const float aside_y = y;

    static const char* const k_column_labels[] = {
        "RECENT",    "THREAD", "REPORTS", "SETTINGS",     "SAVED SETUPS", "THIS PATCH", "OPEN",
        "ALL NOTES", "RULES",  "",        "YOUR DETAILS", "EVERYTHING",   "SETTINGS",
    };

    if (two_col)
    {
        const char* label = k_column_labels[ImClamp(nav, 0, 12)];
        if (*label)
            y += aside_head(dl, ImVec2(x, y), c_muted_foreground, alpha, label);
    }

    float aside_offset = 0.f;
    switch (nav)
    {
    case 0:
    case 2:
        aside_offset = tabs_height(tabs_pill) + px(sp_4);
        break;
    case 7:
        aside_offset = tabs_height(tabs_segment) + px(sp_4);
        break;

    case 11:
        aside_offset = tabs_height(tabs_pill) + px(sp_4) + px(28.f);
        break;

    case 12:
        aside_offset = px(26.f);
        break;
    default:
        break;
    }

    switch (nav)
    {
    case 3:
    {

        const int rows = (*sub == 2) ? 2 : k_module_count;

        float open[k_module_count] = {};
        float unfolded = 0.f;
        for (int i = 0; i < rows; i++)
        {
            const float want =
                (k_modules[i].knob && s.module_armed[i]) ? px(slider_h) + px(6.f) : 0.f;
            open[i] = s.module_detail[i].to(want, mo::SPRING_LAYOUT, dt);
            unfolded += open[i];
        }

        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + px(56.f) * (float)rows + unfolded));
        panel(dl, card, alpha);

        dl->PushClipRect(card.Min, card.Max, true);

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < rows; i++)
        {

            row_label(dl, ImVec2(card.Min.x + px(56.f), ry + px(6.f)), k_modules[i].name,
                      k_modules[i].role, alpha);

            const ImVec2 av(card.Min.x + px(sp_4), ry + px(8.f));
            char initials[3];
            initials_of(k_modules[i].name, initials);
            chip(dl, av, px(28.f), px(14.f), initials, mo::with_alpha(c_card, alpha),
                 mo::with_alpha(c_muted_foreground, alpha), i);

            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "module%d", i);
            switch_toggle(id, ImVec2(card.Max.x - px(sp_4) - px(switch_w), ry + px(14.f)),
                          &s.module_armed[i]);

            const module_row& m = k_modules[i];
            if (m.knob)
            {
                const float ka = ImClamp(open[i] / (px(slider_h) + px(6.f)), 0.f, 1.f);
                if (ka > 0.01f)
                {
                    char read[48];
                    ImFormatString(read, IM_ARRAYSIZE(read), "%.*f%s", m.decimals,
                                   s.module_value[i], m.unit);

                    ImFont* kf = font_medium(text_xs);
                    const float rw = text_width(kf, read);
                    draw_text(
                        dl, kf,
                        ImVec2(card.Max.x - px(sp_4) - px(switch_w) - px(14.f) - rw, ry + px(20.f)),
                        mo::with_alpha(c_muted_foreground, alpha * ka), read);
                }
            }

            if (open[i] > 2.f)
            {
                char sid[32];
                ImFormatString(sid, IM_ARRAYSIZE(sid), "mod%d", i);

                const float lx = card.Min.x + px(56.f);
                range_slider(sid, ImVec2(lx, ry + px(56.f)), card.Max.x - px(sp_4) - lx,
                             &s.module_value[i], m.min_v, m.max_v, m.ticks);
            }

            ry += px(56.f) + open[i];
        }

        dl->PopClipRect();
        y = card.Max.y + px(sp_4);

        check("compact", ImVec2(x, y), &s.module_compact, "Compact rows");
        y += px(40.f);
        break;
    }

    case 5:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(268.f)));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), "Stage", nullptr, alpha);
        ry += px(24.f);
        select("stage", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f, k_stage_options,
               IM_ARRAYSIZE(k_stage_options), &s.stage, "Pick a stage");
        ry += px(select_h) + px(sp_4);

        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), "Author", nullptr, alpha);
        ry += px(24.f);
        select("author", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f, k_author_options,
               IM_ARRAYSIZE(k_author_options), &s.author, "Unassigned");
        ry += px(select_h) + px(sp_4);

        char amount[64];
        ImFormatString(amount, IM_ARRAYSIZE(amount), "Projected installs   %.0fk",
                       s.projected_installs / 1000.f);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), amount, nullptr, alpha);
        ry += px(24.f);
        range_slider("projected-installs", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f,
                     &s.projected_installs, 0.f, 250000.f, 6);

        y = card.Max.y + px(sp_4);

        if (action("save", ImVec2(x, y), 200.f, s.save, "Save patch") && s.save == btn_idle)
        {
            s.save = btn_loading;
            s.save_timer = 0.f;
        }
        if (s.save == btn_loading)
        {
            s.save_timer += dt;
            if (s.save_timer > 1.2f)
            {
                s.save = btn_success;
                toast("Patch saved", k_stage_options[s.stage], toast_success);
            }
        }
        y += px(44.f) + px(sp_5);

        {
            const float head = px(50.f);
            const float row_h = px(46.f);
            const int count = IM_ARRAYSIZE(k_builds);
            const ImRect list(ImVec2(x, y),
                              ImVec2(x + col, y + head + row_h * (float)count + px(8.f)));
            panel(dl, list, alpha);

            row_label(dl, ImVec2(list.Min.x + px(sp_4), list.Min.y + px(14.f)),
                      "Open in this stage", nullptr, alpha);

            ImFont* mf = font_medium(text_xs);
            float pill_w = 0.f;
            for (int i = 0; i < count; i++)
                pill_w = ImMax(pill_w, text_width(mf, k_builds[i].stage) + px(16.f));

            const ImU32 tones[4] = {c_primary, c_amber_400, c_muted_foreground, c_success};
            const float value_r = list.Max.x - px(sp_4);
            const float stage_r = value_r - px(76.f);

            for (int i = 0; i < count; i++)
            {
                const float ry2 = list.Min.y + head + row_h * (float)i;
                const float mid = ry2 + row_h * 0.5f;

                if (i > 0)
                    hairline(dl, list, ry2, alpha * 0.9f);

                const float av = px(22.f);
                char ini[3];
                initials_of(k_builds[i].author, ini);
                chip(dl, ImVec2(list.Min.x + px(sp_4), mid - av * 0.5f), av, av * 0.5f, ini,
                     mo::with_alpha(c_muted_foreground, 0.16f * alpha),
                     mo::with_alpha(c_foreground, alpha), k_builds[i].person);

                ImFont* nf = font_medium(text_sm);
                const float nx = list.Min.x + px(sp_4) + av + px(10.f);
                draw_text_ellipsis(dl, nf, ImVec2(nx, mid - nf->LegacySize * 0.5f),
                                   mo::with_alpha(c_foreground, alpha), k_builds[i].mod,
                                   (stage_r - pill_w - px(12.f)) - nx);

                pill(dl, ImVec2(stage_r - pill_w, mid - px(11.f)), k_builds[i].stage,
                     tones[ImClamp(k_builds[i].tone, 0, 3)], alpha);

                ImFont* vf = font_semibold(text_sm);
                const float vw = text_width(vf, k_builds[i].value);
                draw_text(dl, vf, ImVec2(value_r - vw, mid - vf->LegacySize * 0.5f),
                          mo::with_alpha(c_foreground, alpha), k_builds[i].value);
            }

            y = list.Max.y;
        }
        break;
    }

    case 8:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(sp_4) * 2.f + px(60.f) * 4.f));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < 4; i++)
        {
            row_label(dl, ImVec2(card.Min.x + px(sp_4), ry + px(6.f)), k_automations[i].name,
                      k_automations[i].detail, alpha);

            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "auto%d", i);
            if (switch_toggle(id, ImVec2(card.Max.x - px(sp_4) - px(switch_w), ry + px(14.f)),
                              &s.automations[i]))
                toast(k_automations[i].name, s.automations[i] ? "Enabled" : "Paused",
                      s.automations[i] ? toast_success : toast_neutral);

            ry += px(60.f);
        }
        y = card.Max.y;
        break;
    }

    case 6:
    {
        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + px(34.f) * (float)k_task_count));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < k_task_count; i++)
        {
            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "task%d", i);
            check(id, ImVec2(card.Min.x + px(sp_4), ry), &s.tasks[i], k_tasks[i]);
            ry += px(34.f);
        }

        y = card.Max.y + px(sp_4);
        if (action("sweep", ImVec2(x, y), 200.f, s.sweep, "Clear completed") && s.sweep == btn_idle)
        {
            s.sweep = btn_loading;
            s.sweep_timer = 0.f;
        }
        if (s.sweep == btn_loading)
        {
            s.sweep_timer += dt;
            if (s.sweep_timer > 1.f)
            {
                int cleared = 0;
                for (bool& t : s.tasks)
                {
                    if (t)
                        cleared++;
                    t = false;
                }
                s.sweep = btn_success;

                char msg[64];
                ImFormatString(msg, IM_ARRAYSIZE(msg), "%d task%s cleared", cleared,
                               cleared == 1 ? "" : "s");
                toast("Done", msg, toast_success);
            }
        }
        y += px(44.f);
        break;
    }

    case 9:
    {

        const float dashboard_width = w;
        const range_data& metrics = k_ranges[ImClamp(s.range, 0, 2)];

        static const char* const ranges[] = {"7 days", "30 days", "Quarter"};
        const int was = s.range;
        tabs("range", ImVec2(x, y), ranges, IM_ARRAYSIZE(ranges), &s.range, tabs_segment);

        if (s.range != was)
            s.dash_reveal = 0.f;
        s.dash_reveal = ImMin(1.f, s.dash_reveal + dt / 0.55f);
        const float reveal = mo::EASE_OUT(s.dash_reveal);

        y += tabs_height(tabs_segment) + px(sp_4);

        {
            const float gap = px(sp_3);
            const float tile_w = (dashboard_width - gap * 3.f) / 4.f;
            const float tile_h = px(118.f);

            for (int i = 0; i < 4; i++)
            {
                const ImRect t(ImVec2(x + (tile_w + gap) * (float)i, y),
                               ImVec2(x + (tile_w + gap) * (float)i + tile_w, y + tile_h));
                panel(dl, t, alpha);

                ImFont* lf = font_medium(text_xs);
                draw_text(dl, lf, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(14.f)),
                          mo::with_alpha(c_muted_foreground, alpha), metrics.stats[i].label);

                char nid[16];
                ImFormatString(nid, IM_ARRAYSIZE(nid), "kpi%d", i);
                const float now = number_value(nid, metrics.stats[i].value);

                char shown[32];
                ImFormatString(shown, IM_ARRAYSIZE(shown), "%s%.*f%s", metrics.stats[i].prefix,
                               metrics.stats[i].decimals, now, metrics.stats[i].suffix);

                ImFont* vf = font_semibold(22.f);
                draw_text_tracked(dl, vf, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(36.f)),
                                  mo::with_alpha(c_foreground, alpha), shown, px(-0.4f));

                const ImU32 tone = metrics.stats[i].good ? c_success : c_destructive;
                ImFont* df = font_medium(text_xs);
                draw_text(dl, df, ImVec2(t.Min.x + px(sp_4), t.Min.y + px(70.f)),
                          mo::with_alpha(tone, alpha), metrics.stats[i].delta);

                const ImRect spark(ImVec2(t.Min.x + px(1.f), t.Min.y + px(86.f)),
                                   ImVec2(t.Max.x - px(1.f), t.Max.y - px(1.f)));
                dl->PushClipRect(spark.Min, spark.Max, true);
                sparkline(dl, spark, metrics.spark[i], metrics.spark_n, tone, alpha, reveal);
                dl->PopClipRect();
            }
            y += tile_h + px(sp_3);
        }

        const float grid_gap = px(sp_3);
        const float chart_w = (dashboard_width - grid_gap) * 0.64f;
        const float pipe_w = (dashboard_width - grid_gap) * 0.38f;

        float row_a_y = 0.f, row_b_y = 0.f, row_a_h = 0.f, row_b_h = 0.f;

        {
            const float h = px(248.f);
            row_a_y = y;
            row_a_h = h;
            const ImRect card(ImVec2(x, y), ImVec2(x + chart_w, y + h));
            panel(dl, card, alpha);

            row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "Frames",
                      "Rendered, by day", alpha);

            {

                ImFont* f = font_semibold(text_sm);
                const float bw = text_width(f, metrics.total);
                draw_text(dl, f, ImVec2(card.Max.x - px(sp_4) - bw, card.Min.y + px(15.f)),
                          mo::with_alpha(c_foreground, alpha), metrics.total);

                ImFont* g = font_medium(text_xs);
                const float gw = text_width(g, metrics.total_note);
                draw_text(dl, g, ImVec2(card.Max.x - px(sp_4) - gw, card.Min.y + px(34.f)),
                          mo::with_alpha(c_success, alpha), metrics.total_note);
            }

            const ImRect plot(ImVec2(card.Min.x + px(sp_4) + px(38.f), card.Min.y + px(72.f)),
                              ImVec2(card.Max.x - px(sp_4), card.Max.y - px(34.f)));

            float lo = metrics.frames[0], hi = metrics.frames[0];
            for (int i = 1; i < metrics.frame_count; i++)
            {
                lo = ImMin(lo, metrics.frames[i]);
                hi = ImMax(hi, metrics.frames[i]);
            }
            lo = 0.f;
            hi *= 1.12f;

            ImFont* af = font_medium(text_xs);
            for (int g = 0; g <= 3; g++)
            {
                const float gy = plot.Max.y - plot.GetHeight() * ((float)g / 3.f);
                dl->AddRectFilled(ImVec2(plot.Min.x, gy - px(0.5f)),
                                  ImVec2(plot.Max.x, gy + px(0.5f)),
                                  mo::with_alpha(c_border, (g == 0 ? 1.4f : 0.75f) * alpha));

                if (g > 0)
                {
                    const char* txt = (g == 3) ? metrics.axis_hi : (g == 2 ? metrics.axis_mid : "");
                    if (*txt)
                        draw_text(dl, af,
                                  ImVec2(plot.Min.x - px(8.f) - text_width(af, txt),
                                         gy - af->LegacySize * 0.5f),
                                  mo::with_alpha(c_muted_foreground, 0.85f * alpha), txt);
                }
            }

            dl->PushClipRect(ImVec2(plot.Min.x, plot.Min.y - px(6.f)), plot.Max, true);
            series_fill(dl, plot, metrics.frames, metrics.frame_count, lo, hi, c_primary, alpha,
                        reveal);
            series_line(dl, plot, metrics.frames, metrics.frame_count, lo, hi, c_primary, alpha,
                        reveal, px(2.f));
            dl->PopClipRect();

            const int stride = (metrics.frame_count > 10) ? 2 : 1;
            for (int i = 0; i < metrics.frame_count; i += stride)
            {
                const char* lab = metrics.x_labels[i];
                if (!lab)
                    continue;
                const float lx =
                    plot.Min.x +
                    plot.GetWidth() * ((float)i / (float)ImMax(metrics.frame_count - 1, 1));
                draw_text(dl, af, ImVec2(lx - text_width(af, lab) * 0.5f, plot.Max.y + px(10.f)),
                          mo::with_alpha(c_muted_foreground, 0.85f * alpha), lab);
            }

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (reveal > 0.98f && plot.Contains(mouse) && !pointer_claimed() &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            {
                const float u = (mouse.x - plot.Min.x) / ImMax(plot.GetWidth(), 1.f);
                const int idx = ImClamp((int)(u * (float)(metrics.frame_count - 1) + 0.5f), 0,
                                        metrics.frame_count - 1);
                const float px_ =
                    plot.Min.x +
                    plot.GetWidth() * ((float)idx / (float)ImMax(metrics.frame_count - 1, 1));
                const float py = plot.Max.y - (metrics.frames[idx] - lo) / ImMax(hi - lo, 1e-4f) *
                                                  plot.GetHeight();

                dl->AddRectFilled(ImVec2(px_ - px(0.5f), plot.Min.y),
                                  ImVec2(px_ + px(0.5f), plot.Max.y),
                                  mo::with_alpha(c_border_strong, alpha));
                dl->AddCircleFilled(ImVec2(px_, py), px(4.5f), mo::with_alpha(c_card, alpha));
                dl->AddCircle(ImVec2(px_, py), px(4.5f), mo::with_alpha(c_primary, alpha), 0,
                              px(2.f));

                char read[32];
                ImFormatString(read, IM_ARRAYSIZE(read), "%dk frames", (int)metrics.frames[idx]);

                ImFont* tf = font_semibold(text_xs);
                const float tw = text_width(tf, read) + px(16.f);
                const float tx = ImClamp(px_ - tw * 0.5f, plot.Min.x, plot.Max.x - tw);
                const ImRect tip(ImVec2(tx, py - px(34.f)),
                                 ImVec2(tx + tw, py - px(34.f) + px(24.f)));

                dl->AddRectFilled(tip.Min, tip.Max, mo::with_alpha(c_foreground, 0.94f * alpha),
                                  px(6.f));
                draw_text(dl, tf,
                          ImVec2(tip.Min.x + px(8.f), tip.GetCenter().y - tf->LegacySize * 0.5f),
                          mo::with_alpha(c_background, alpha), read);
            }

            y += h + px(sp_3);
        }

        {
            const float h = px(232.f);
            row_b_y = y;
            row_b_h = h;

            {
                const ImRect card(ImVec2(x, y), ImVec2(x + pipe_w, y + h));
                panel(dl, card, alpha);
                row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)),
                          "Patches by stage", "Weighted, this patch", alpha);

                float ry = card.Min.y + px(66.f);
                const float bar_x = card.Min.x + px(sp_4) + px(96.f);
                const float bar_w = card.Max.x - px(sp_4) - px(72.f) - bar_x;

                for (int i = 0; i < IM_ARRAYSIZE(k_stages); i++)
                {
                    ImFont* nf = font_medium(text_xs);
                    draw_text(dl, nf, ImVec2(card.Min.x + px(sp_4), ry - nf->LegacySize * 0.5f),
                              mo::with_alpha(c_foreground, alpha), k_stages[i].name);

                    meter(dl,
                          ImRect(ImVec2(bar_x, ry - px(4.f)), ImVec2(bar_x + bar_w, ry + px(4.f))),
                          k_stages[i].weight * reveal, c_primary, alpha);

                    ImFont* vf = font_medium(text_xs);
                    const float vw = text_width(vf, k_stages[i].value);
                    draw_text(dl, vf,
                              ImVec2(card.Max.x - px(sp_4) - vw, ry - vf->LegacySize * 0.5f),
                              mo::with_alpha(c_foreground, alpha), k_stages[i].value);

                    ry += px(30.f);
                }
            }

            {
                const ImRect card(ImVec2(x + chart_w + grid_gap, row_a_y),
                                  ImVec2(x + dashboard_width, row_a_y + row_a_h));
                panel(dl, card, alpha);
                row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "Win rate",
                          "Against target", alpha);

                float ry = card.Min.y + px(62.f);
                for (int i = 0; i < IM_ARRAYSIZE(k_authors); i++)
                {
                    const float avatar = px(24.f);
                    const ImVec2 at(card.Min.x + px(sp_4), ry);

                    char ini[3];
                    initials_of(k_authors[i].name, ini);
                    chip(dl, at, avatar, avatar * 0.5f, ini,
                         mo::with_alpha(c_muted_foreground, 0.18f * alpha),
                         mo::with_alpha(c_foreground, alpha), k_authors[i].person);

                    ImFont* nf = font_medium(text_xs);
                    draw_text(dl, nf, ImVec2(at.x + avatar + px(10.f), ry + px(1.f)),
                              mo::with_alpha(c_foreground, alpha), k_authors[i].name);

                    ImFont* ff = font_medium(text_xs);
                    const float fw = text_width(ff, k_authors[i].figure);
                    draw_text(dl, ff, ImVec2(card.Max.x - px(sp_4) - fw, ry + px(1.f)),
                              mo::with_alpha(c_muted_foreground, alpha), k_authors[i].figure);

                    const ImU32 tone = (k_authors[i].attainment >= 1.f) ? c_success : c_primary;
                    meter(dl,
                          ImRect(ImVec2(at.x + avatar + px(10.f), ry + px(19.f)),
                                 ImVec2(card.Max.x - px(sp_4), ry + px(25.f))),
                          k_authors[i].attainment * reveal, tone, alpha);

                    ry += px(40.f);
                }
            }
        }

        {
            const float head = px(58.f);
            const float row_h = px(52.f);
            const float h = head + row_h * (float)IM_ARRAYSIZE(k_builds) + px(10.f);
            row_b_h = ImMax(row_b_h, h);
            const ImRect card(ImVec2(x + pipe_w + grid_gap, row_b_y), ImVec2(x + col, row_b_y + h));
            panel(dl, card, alpha);

            row_label(dl, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)), "Shipping soon",
                      nullptr, alpha);

            const ImU32 tones[4] = {c_primary, c_amber_400, c_muted_foreground, c_success};

            ImFont* mf = font_medium(text_xs);
            ImFont* of = font_regular(text_xs);

            float pill_w = 0.f, author_w = 0.f;
            for (int i = 0; i < IM_ARRAYSIZE(k_builds); i++)
            {
                pill_w = ImMax(pill_w, text_width(mf, k_builds[i].stage) + px(16.f));
                author_w = ImMax(author_w, text_width(of, k_builds[i].author));
            }

            const float avatar_w = px(22.f);
            const bool show_author_name = (col >= px(680.f));
            const bool show_close = (col >= px(430.f));

            const float value_r = card.Max.x - px(sp_4);
            const float close_r = show_close ? value_r - px(76.f) : value_r;
            const float stage_r = close_r - px(52.f);
            const float stage_x = stage_r - pill_w;

            const float author_cell = avatar_w + (show_author_name ? px(8.f) + author_w : 0.f);
            const float author_x = stage_x - px(16.f) - author_cell;

            for (int i = 0; i < IM_ARRAYSIZE(k_builds); i++)
            {
                const float ry = card.Min.y + head + row_h * (float)i;
                const ImRect row(ImVec2(card.Min.x + px(6.f), ry),
                                 ImVec2(card.Max.x - px(6.f), ry + row_h));

                if (row.Contains(ImGui::GetIO().MousePos) && !pointer_claimed() &&
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
                    dl->AddRectFilled(row.Min, row.Max,
                                      mo::with_alpha(c_foreground, 0.035f * alpha), px(10.f));

                if (i > 0)
                    hairline(dl, card, ry, alpha * 0.9f);

                const float mid = ry + row_h * 0.5f;

                char ini[3];
                initials_of(k_builds[i].mod, ini);
                chip(dl, ImVec2(card.Min.x + px(sp_4), mid - px(14.f)), px(28.f), px(8.f), ini,
                     mo::with_alpha(c_muted_foreground, 0.16f * alpha),
                     mo::with_alpha(c_foreground, alpha));

                ImFont* cf = font_medium(text_sm);
                const float name_x = card.Min.x + px(sp_4) + px(38.f);
                draw_text_ellipsis(dl, cf, ImVec2(name_x, mid - cf->LegacySize * 0.5f),
                                   mo::with_alpha(c_foreground, alpha), k_builds[i].mod,
                                   author_x - px(12.f) - name_x);

                const float avatar = px(22.f);
                char oini[3];
                initials_of(k_builds[i].author, oini);
                chip(dl, ImVec2(author_x, mid - avatar * 0.5f), avatar, avatar * 0.5f, oini,
                     mo::with_alpha(c_muted_foreground, 0.16f * alpha),
                     mo::with_alpha(c_foreground, alpha), k_builds[i].person);

                if (show_author_name)
                    draw_text(dl, of,
                              ImVec2(author_x + avatar + px(8.f), mid - of->LegacySize * 0.5f),
                              mo::with_alpha(c_muted_foreground, alpha), k_builds[i].author);

                pill(dl, ImVec2(stage_x, mid - px(11.f)), k_builds[i].stage,
                     tones[ImClamp(k_builds[i].tone, 0, 3)], alpha);

                if (show_close)
                {
                    const float cw = text_width(of, k_builds[i].close);
                    draw_text(dl, of, ImVec2(close_r - cw, mid - of->LegacySize * 0.5f),
                              mo::with_alpha(c_muted_foreground, alpha), k_builds[i].close);
                }

                ImFont* vf = font_semibold(text_sm);
                const float vw = text_width(vf, k_builds[i].value);
                draw_text(dl, vf, ImVec2(value_r - vw, mid - vf->LegacySize * 0.5f),
                          mo::with_alpha(c_foreground, alpha), k_builds[i].value);
            }

            y = row_b_y + row_b_h;
        }
        break;
    }

    case 0:
    {
        static const char* const scopes[] = {"Recent", "Saved"};
        tabs("search-tabs", ImVec2(x, y), scopes, IM_ARRAYSIZE(scopes), &s.search_tab, tabs_pill);
        y += tabs_height(tabs_pill) + px(sp_4);

        const bool recent = (s.search_tab == 0);
        const int count =
            recent ? (s.history_cleared ? 0 : IM_ARRAYSIZE(k_recent)) : IM_ARRAYSIZE(k_saved);
        const float row = px(44.f);

        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)ImMax(count, 1)));
        panel(dl, card, alpha);

        if (count == 0)
        {
            ImFont* f = font_regular(text_sm);
            const char* msg = "Nothing here yet.";
            draw_text(dl, f,
                      ImVec2(card.GetCenter().x - text_width(f, msg) * 0.5f,
                             card.GetCenter().y - f->LegacySize * 0.5f),
                      mo::with_alpha(c_muted_foreground, alpha), msg);
        }

        float ry = card.Min.y + px(sp_4);
        for (int i = 0; i < count; i++)
        {
            const search_hit& hit = recent ? k_recent[i] : k_saved[i];

            icons::draw(recent ? icons::id::clock : icons::id::star, dl,
                        ImVec2(card.Min.x + px(sp_4), ry + px(14.f)), px(16.f),
                        mo::with_alpha(c_muted_foreground, alpha));

            ImFont* qf = font_medium(text_sm);
            draw_text(dl, qf,
                      ImVec2(card.Min.x + px(44.f), ry + px(12.f) + line_top(qf, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), hit.query);

            ImFont* cf = font_regular(text_xs);
            draw_text(dl, cf,
                      ImVec2(card.Max.x - px(sp_4) - text_width(cf, hit.count), ry + px(14.f)),
                      mo::with_alpha(c_muted_foreground, alpha), hit.count);

            if (i + 1 < count)
                hairline(dl, card, ry + row, alpha);
            ry += row;
        }
        y = card.Max.y + px(sp_4);

        check("search-bodies", ImVec2(x, y), &s.search_bodies, "Search inside note bodies");
        y += px(38.f);

        if (recent &&
            action("clear-history", ImVec2(x, y), 200.f, s.clear_history, "Clear history") &&
            s.clear_history == btn_idle)
        {
            s.clear_history = btn_loading;
            s.clear_timer = 0.f;
        }
        if (s.clear_history == btn_loading)
        {
            s.clear_timer += dt;
            if (s.clear_timer > 0.9f)
            {
                s.clear_history = btn_success;
                s.history_cleared = true;
                toast("History cleared", "Five recent searches removed", toast_success);
            }
        }
        if (recent)
            y += px(44.f);
        break;
    }

    case 1:
    {
        const float bubble_max = ImMin(col - px(60.f), px(340.f));

        for (int i = 0; i < IM_ARRAYSIZE(k_thread); i++)
        {
            const chat_turn& turn = k_thread[i];
            ImFont* f = font_regular(text_sm);

            const float inner = bubble_max - px(sp_3) * 2.f;
            const int lines = wrapped_line_count(f, turn.text, inner);
            const float h = px(sp_3) * 2.f + px(22.f) * (float)lines;

            const ImRect b = turn.from_user
                                 ? ImRect(ImVec2(x + col - bubble_max, y), ImVec2(x + col, y + h))
                                 : ImRect(ImVec2(x, y), ImVec2(x + bubble_max, y + h));

            dl->AddRectFilled(b.Min, b.Max,
                              mo::with_alpha(c_card, (turn.from_user ? 1.f : 0.5f) * alpha),
                              px(14.f));
            if (!turn.from_user)
                dl->AddRect(ImVec2(b.Min.x + px(0.5f), b.Min.y + px(0.5f)),
                            ImVec2(b.Max.x - px(0.5f), b.Max.y - px(0.5f)),
                            mo::with_alpha(c_border, alpha), px(14.f), px(1.f), ImDrawFlags_None);

            draw_text_wrapped(
                dl, f, ImVec2(b.Min.x + px(sp_3), b.Min.y + px(sp_3)),
                mo::with_alpha(turn.from_user ? c_foreground : c_muted_foreground, alpha),
                turn.text, inner, px(22.f));

            y = b.Max.y + px(sp_2);
        }
        y += px(sp_3);

        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(184.f)));
        panel(dl, card, alpha);

        float ry = card.Min.y + px(sp_4);
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), "Answer style", nullptr, alpha);
        ry += px(24.f);
        select("answer-style", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f,
               k_answer_styles, IM_ARRAYSIZE(k_answer_styles), &s.answer_style, "Balanced");
        ry += px(select_h) + px(sp_4);

        char creativity[64];
        ImFormatString(creativity, IM_ARRAYSIZE(creativity), "Creativity   %d%%",
                       (int)(s.creativity * 100.f + 0.5f));
        row_label(dl, ImVec2(card.Min.x + px(sp_4), ry), creativity, nullptr, alpha);
        ry += px(24.f);
        range_slider("creativity", ImVec2(card.Min.x + px(sp_4), ry), col - px(sp_4) * 2.f,
                     &s.creativity, 0.f, 1.f, 10);

        y = card.Max.y + px(sp_4);

        if (action("ask", ImVec2(x, y), 200.f, s.ask, "Draft the reply") && s.ask == btn_idle)
        {
            s.ask = btn_loading;
            s.ask_timer = 0.f;
        }
        if (s.ask == btn_loading)
        {
            s.ask_timer += dt;
            if (s.ask_timer > 1.4f)
            {
                s.ask = btn_success;
                toast("Draft ready", k_answer_styles[s.answer_style], toast_info);
            }
        }
        y += px(44.f);
        break;
    }

    case 2:
    {
        static const char* const filters[] = {"All", "Unread", "Mentions"};
        tabs("message-tabs", ImVec2(x, y), filters, IM_ARRAYSIZE(filters), &s.message_tab,
             tabs_pill);
        y += tabs_height(tabs_pill) + px(sp_4);

        int shown[IM_ARRAYSIZE(k_messages)];
        int count = 0;
        for (int i = 0; i < IM_ARRAYSIZE(k_messages); i++)
        {
            const bool unread = !s.message_read[i];
            if (s.message_tab == 1 && !unread)
                continue;
            if (s.message_tab == 2 && !k_messages[i].mention)
                continue;
            shown[count++] = i;
        }

        const float row = px(56.f);
        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)ImMax(count, 1)));
        panel(dl, card, alpha);

        if (count == 0)
            empty_state(dl, card, s.message_tab == 2 ? "No mentions." : "No messages.", alpha);

        float ry = card.Min.y + px(sp_4);
        for (int k = 0; k < count; k++)
        {
            const int i = shown[k];
            const message& m = k_messages[i];
            const bool unread = !s.message_read[i];

            char row_id[16];
            ImFormatString(row_id, IM_ARRAYSIZE(row_id), "message%d", i);
            const ImRect hit(ImVec2(card.Min.x + px(6.f), ry),
                             ImVec2(card.Max.x - px(6.f), ry + row));
            if (row_hit(dl, row_id, hit, alpha))
            {
                s.message_read[i] = !s.message_read[i];
                toast(s.message_read[i] ? "Marked read" : "Marked unread", m.who, toast_neutral);
            }

            char in[3];
            initials_of(m.who, in);
            chip(dl, ImVec2(card.Min.x + px(sp_4), ry + px(14.f)), px(28.f), px(14.f), in,
                 mo::with_alpha(c_foreground, 0.06f * alpha),
                 mo::with_alpha(c_muted_foreground, alpha), i + 4);

            const float tx = card.Min.x + px(56.f);

            ImFont* tf = font_regular(text_xs);
            const float when_w = text_width(tf, m.when);
            const float text_w = (card.Max.x - px(sp_4) - when_w - px(12.f)) - tx;

            ImFont* wf = unread ? font_semibold(text_sm) : font_medium(text_sm);
            draw_text_ellipsis(dl, wf, ImVec2(tx, ry + px(10.f) + line_top(wf, px(leading_sm))),
                               mo::with_alpha(c_foreground, alpha), m.who, text_w);

            ImFont* sf = font_regular(text_xs);
            draw_text_ellipsis(dl, sf, ImVec2(tx, ry + px(30.f) + line_top(sf, px(leading_xs))),
                               mo::with_alpha(c_muted_foreground, alpha), m.subject,
                               text_w - (unread ? px(14.f) : 0.f));

            draw_text(dl, tf, ImVec2(card.Max.x - px(sp_4) - when_w, ry + px(12.f)),
                      mo::with_alpha(c_muted_foreground, alpha), m.when);

            if (unread)
            {
                const ImVec2 dot(card.Max.x - px(sp_4) - px(4.f), ry + px(36.f));
                dl->AddCircleFilled(dot, px(4.f), mo::with_alpha(c_primary, alpha));
            }

            if (k + 1 < count)
                hairline(dl, card, ry + row, alpha);
            ry += row;
        }
        y = card.Max.y + px(sp_4);

        if (action("mark-all", ImVec2(x, y), 200.f, s.mark_all, "Mark all read") &&
            s.mark_all == btn_idle)
        {
            s.mark_all = btn_loading;
            s.mark_timer = 0.f;
        }
        if (s.mark_all == btn_loading)
        {
            s.mark_timer += dt;
            if (s.mark_timer > 0.9f)
            {
                int cleared = 0;
                for (int i = 0; i < IM_ARRAYSIZE(k_messages); i++)
                {
                    if (!s.message_read[i])
                        cleared++;
                    s.message_read[i] = true;
                }
                s.mark_all = btn_success;

                char msg[64];
                ImFormatString(msg, IM_ARRAYSIZE(msg), "%d conversation%s filed", cleared,
                               cleared == 1 ? "" : "s");
                toast("Messages clear", msg, toast_success);
            }
        }
        y += px(44.f);
        break;
    }

    case 4:
    {
        const float half = (col - px(sp_3)) * 0.5f;
        select("config-sort", ImVec2(x, y), half, k_config_sorts, IM_ARRAYSIZE(k_config_sorts),
               &s.config_sort, "Largest first");
        check("only-mine", ImVec2(x + half + px(sp_3), y + px(6.f)), &s.only_mine, "Only mine");
        y += px(select_h) + px(sp_4);

        int shown[IM_ARRAYSIZE(k_configs)];
        int count = 0;
        for (int i = 0; i < IM_ARRAYSIZE(k_configs); i++)
            if (!s.only_mine || strcmp(k_configs[i].author, brand::user_name) == 0)
                shown[count++] = i;

        const float row = px(56.f);
        const ImRect card(ImVec2(x, y),
                          ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)ImMax(count, 1)));
        panel(dl, card, alpha);

        if (count == 0)
            empty_state(dl, card, "Nothing of yours here.", alpha);

        float ry = card.Min.y + px(sp_4);
        for (int k = 0; k < count; k++)
        {
            const config_row& c = k_configs[shown[k]];

            char row_id[16];
            ImFormatString(row_id, IM_ARRAYSIZE(row_id), "co%d", shown[k]);
            const ImRect hit(ImVec2(card.Min.x + px(6.f), ry),
                             ImVec2(card.Max.x - px(6.f), ry + row));
            if (row_hit(dl, row_id, hit, alpha))
                toast(c.name, c.file, toast_neutral);

            char in[3];
            in[0] = c.name[0];
            in[1] = 0;
            in[2] = 0;
            chip(dl, ImVec2(card.Min.x + px(sp_4), ry + px(14.f)), px(28.f), px(8.f), in,
                 mo::with_alpha(c_foreground, 0.06f * alpha),
                 mo::with_alpha(c_muted_foreground, alpha));

            ImFont* af = font_medium(text_sm);
            ImFont* of = font_regular(text_xs);
            const float right_w = ImMax(text_width(af, c.loads), text_width(of, c.author));
            const float name_x = card.Min.x + px(56.f);
            const float name_w = (card.Max.x - px(sp_4) - right_w - px(12.f)) - name_x;

            row_label(dl, ImVec2(name_x, ry + px(12.f)), c.name, c.file, alpha, name_w);

            draw_text(dl, af,
                      ImVec2(card.Max.x - px(sp_4) - text_width(af, c.loads), ry + px(12.f)),
                      mo::with_alpha(c_foreground, alpha), c.loads);

            draw_text(dl, of,
                      ImVec2(card.Max.x - px(sp_4) - text_width(of, c.author), ry + px(32.f)),
                      mo::with_alpha(c_muted_foreground, alpha), c.author);

            if (k + 1 < count)
                hairline(dl, card, ry + row, alpha);
            ry += row;
        }
        y = card.Max.y;
        break;
    }

    case 7:
    {
        tabs("notes-tabs", ImVec2(x, y), k_note_tabs, IM_ARRAYSIZE(k_note_tabs), &s.notes_tab,
             tabs_segment);
        y += tabs_height(tabs_segment) + px(sp_4);

        for (int i = 0; i < IM_ARRAYSIZE(k_notes); i++)
        {
            const note& n = k_notes[i];
            if (s.notes_tab == 1 && n.bucket != 1)
                continue;
            if (s.notes_tab == 2 && n.bucket != 2)
                continue;

            ImFont* bf = font_regular(text_xs);
            const float inner = col - px(sp_4) * 2.f;
            const int lines = ImMin(wrapped_line_count(bf, n.body, inner), 2);
            const float h = px(sp_4) * 2.f + px(leading_sm) + px(4.f) + px(18.f) * (float)lines +
                            px(6.f) + px(leading_xs);

            const ImRect card(ImVec2(x, y), ImVec2(x + col, y + h));
            panel(dl, card, alpha);

            ImFont* tf = font_medium(text_sm);
            draw_text(
                dl, tf,
                ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4) + line_top(tf, px(leading_sm))),
                mo::with_alpha(c_foreground, alpha), n.title);

            dl->PushClipRect(card.Min,
                             ImVec2(card.Max.x, card.Min.y + px(sp_4) + px(leading_sm) + px(4.f) +
                                                    px(18.f) * (float)lines),
                             true);
            draw_text_wrapped(
                dl, bf,
                ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4) + px(leading_sm) + px(4.f)),
                mo::with_alpha(c_muted_foreground, alpha), n.body, inner, px(18.f));
            dl->PopClipRect();

            ImFont* mf = font_regular(text_xs);
            draw_text(dl, mf,
                      ImVec2(card.Min.x + px(sp_4),
                             card.Max.y - px(sp_4) - px(leading_xs) + line_top(mf, px(leading_xs))),
                      mo::with_alpha(c_muted_foreground, 0.75f * alpha), n.meta);

            y = card.Max.y + px(sp_3);
        }
        break;
    }

    case 10:
    {

        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(96.f)));
        panel(dl, card, alpha);

        const float avatar = px(56.f);
        const ImVec2 at(card.Min.x + px(sp_5), card.GetCenter().y - avatar * 0.5f);
        if (!avatars::draw(dl, avatars::me(), at, avatar, alpha))
            chip(dl, at, avatar, avatar * 0.5f, brand::user_initials,
                 mo::with_alpha(c_foreground, 0.06f * alpha),
                 mo::with_alpha(c_muted_foreground, alpha));

        ImFont* nf = font_semibold(text_base);
        draw_text(dl, nf, ImVec2(at.x + avatar + px(sp_4), card.GetCenter().y - px(19.f)),
                  mo::with_alpha(c_foreground, alpha), brand::user_name);

        ImFont* ef = font_regular(text_sm);
        draw_text(dl, ef, ImVec2(at.x + avatar + px(sp_4), card.GetCenter().y + px(3.f)),
                  mo::with_alpha(c_muted_foreground, alpha), brand::user_github);

        y = card.Max.y + px(sp_5);

        const float row = px(46.f);
        const ImRect fields(
            ImVec2(x, y),
            ImVec2(x + col, y + px(sp_4) * 2.f + row * (float)IM_ARRAYSIZE(k_profile_fields)));
        panel(dl, fields, alpha);

        float fy = fields.Min.y + px(sp_4);
        for (int i = 0; i < IM_ARRAYSIZE(k_profile_fields); i++)
        {
            ImFont* lf = font_regular(text_xs);
            draw_text(dl, lf, ImVec2(fields.Min.x + px(sp_4), fy + px(6.f)),
                      mo::with_alpha(c_muted_foreground, alpha), k_profile_fields[i].label);

            ImFont* vf = font_medium(text_sm);
            draw_text(dl, vf,
                      ImVec2(fields.Max.x - px(sp_4) - text_width(vf, k_profile_fields[i].value),
                             fy + px(4.f)),
                      mo::with_alpha(c_foreground, alpha), k_profile_fields[i].value);

            if (i + 1 < IM_ARRAYSIZE(k_profile_fields))
                hairline(dl, fields, fy + row, alpha);
            fy += row;
        }
        y = fields.Max.y + px(sp_5);

        ImFont* sf = font_regular(10.f);
        draw_text_tracked(dl, sf, ImVec2(x, y + line_top(sf, px(15.f))),
                          mo::with_alpha(c_muted_foreground, alpha), "NOTIFICATIONS", px(1.6f));
        y += px(24.f);

        const ImRect prefs(
            ImVec2(x, y),
            ImVec2(x + col, y + px(sp_4) * 2.f + px(60.f) * (float)IM_ARRAYSIZE(k_profile_prefs)));
        panel(dl, prefs, alpha);

        float py = prefs.Min.y + px(sp_4);
        for (int i = 0; i < IM_ARRAYSIZE(k_profile_prefs); i++)
        {
            row_label(dl, ImVec2(prefs.Min.x + px(sp_4), py + px(6.f)), k_profile_prefs[i].name,
                      k_profile_prefs[i].detail, alpha);

            char id[32];
            ImFormatString(id, IM_ARRAYSIZE(id), "pref%d", i);
            if (switch_toggle(id, ImVec2(prefs.Max.x - px(sp_4) - px(switch_w), py + px(14.f)),
                              &s.profile_prefs[i]))
                toast(k_profile_prefs[i].name, s.profile_prefs[i] ? "On" : "Off",
                      s.profile_prefs[i] ? toast_success : toast_neutral);

            py += px(60.f);
        }
        y = prefs.Max.y + px(sp_5);

        if (action("profile-save", ImVec2(x, y), 200.f, s.profile_save, "Save changes") &&
            s.profile_save == btn_idle)
        {
            s.profile_save = btn_loading;
            s.profile_timer = 0.f;
        }
        if (s.profile_save == btn_loading)
        {
            s.profile_timer += dt;
            if (s.profile_timer > 1.1f)
            {
                s.profile_save = btn_success;
                toast("Profile saved", brand::user_name, toast_success);
            }
        }
        y += px(44.f);

        ImFont* cl = font_regular(10.f);
        draw_text_tracked(dl, cl, ImVec2(x, y + line_top(cl, px(15.f))),
                          mo::with_alpha(c_muted_foreground, alpha), "CREDITS", px(1.6f));
        y += px(24.f);

        {
            const float crow = px(36.f);
            const ImRect box(
                ImVec2(x, y),
                ImVec2(x + col, y + px(sp_4) * 2.f + crow * (float)IM_ARRAYSIZE(k_credits)));
            panel(dl, box, alpha);

            ImFont* kf = font_regular(text_sm);
            ImFont* vf = font_medium(text_sm);

            float cy = box.Min.y + px(sp_4);
            for (int i = 0; i < IM_ARRAYSIZE(k_credits); i++)
            {
                draw_text(dl, kf, ImVec2(box.Min.x + px(sp_4), cy + px(4.f)),
                          mo::with_alpha(c_muted_foreground, alpha), k_credits[i].label);

                const float lw = text_width(kf, k_credits[i].label);
                const float room = col - px(sp_4) * 2.f - lw - px(16.f);
                const float vw = ImMin(text_width(vf, k_credits[i].value), room);
                draw_text_ellipsis(dl, vf, ImVec2(box.Max.x - px(sp_4) - vw, cy + px(4.f)),
                                   mo::with_alpha(c_foreground, alpha), k_credits[i].value, room);

                if (i + 1 < IM_ARRAYSIZE(k_credits))
                    hairline(dl, box, cy + crow - px(6.f), alpha);

                cy += crow;
            }
            y = box.Max.y + px(sp_5);
        }
        break;
    }

    case 11:
    {
        tabs("notif-tabs", ImVec2(x, y), k_notif_tabs, IM_ARRAYSIZE(k_notif_tabs), &s.notif_tab,
             tabs_pill);

        {
            const float bw = px(150.f);
            const float tabs_w = tabs_width(k_notif_tabs, IM_ARRAYSIZE(k_notif_tabs), tabs_pill);
            const float bx = ImMax(x + tabs_w + px(sp_3), x + col - bw);
            if (action("mark-notifs", ImVec2(bx, y - px(3.f)), 150.f, btn_idle, "Mark all read"))
                s.mark_cascade = 0.f;
        }
        if (s.mark_cascade < 1e5f)
        {
            s.mark_cascade += dt;
            for (int i = 0; i < IM_ARRAYSIZE(k_notifications); i++)
                if (s.mark_cascade > (float)i * 0.055f)
                    s.notif_read[i] = true;
            if (s.mark_cascade > 1.4f)
                s.mark_cascade = 1e6f;
        }

        y += tabs_height(tabs_pill) + px(sp_4);

        const float row_h = px(64.f);
        int visible[IM_ARRAYSIZE(k_notifications)];
        int count = 0;
        for (int i = 0; i < IM_ARRAYSIZE(k_notifications); i++)
        {
            if (s.notif_gone[i])
                continue;
            if (s.notif_tab == 1 && s.notif_read[i])
                continue;
            if (s.notif_tab == 2 && !k_notifications[i].mention)
                continue;
            visible[count++] = i;
        }

        int drawn = 0;
        int last_day = -1;
        float total = 0.f;
        const float start_y = y;

        for (int k = 0; k < count; k++)
        {
            const int i = visible[k];
            const notification& n = k_notifications[i];

            const float open = s.notif_height[i].to(row_h, mo::SPRING_LAYOUT, dt);
            if (open < 1.f)
                continue;

            if (n.day != last_day)
            {
                last_day = n.day;
                ImFont* df = font_medium(text_xs);
                const float t = stagger(drawn);
                draw_text(dl, df, ImVec2(x + px(2.f), y + px(6.f) + px(8.f) * (1.f - t)),
                          mo::with_alpha(c_muted_foreground, 0.9f * t * alpha),
                          k_notif_days[ImClamp(n.day, 0, 2)]);
                y += px(28.f);
                total += px(28.f);
            }

            const float t = stagger(drawn++);
            const float slide = s.notif_slide[i];
            const float rx = x + px(16.f) * (1.f - t) + slide;
            const float row_a = alpha * t * (1.f - ImClamp(slide / px(120.f), 0.f, 1.f));

            const ImRect card(ImVec2(rx, y), ImVec2(rx + col, y + open - px(6.f)));

            dl->PushClipRect(ImVec2(x, y), ImVec2(x + col, y + open), true);
            panel(dl, card, row_a);

            const bool unread = !s.notif_read[i];

            {
                ImGui::PushID(1000 + i);
                mo::spring* bar = ui_runtime::animation_state<mo::spring>(
                    ImGui::GetCurrentWindow()->GetID("bar"));
                ImGui::PopID();
                const float h = bar->to(unread ? px(20.f) : 0.f, mo::SPRING_LAYOUT, dt);
                if (h > 0.5f)
                    dl->AddRectFilled(ImVec2(card.Min.x + px(1.f), card.GetCenter().y - h * 0.5f),
                                      ImVec2(card.Min.x + px(4.f), card.GetCenter().y + h * 0.5f),
                                      mo::with_alpha(c_primary, row_a), px(2.f));
            }

            char row_id[24];
            ImFormatString(row_id, IM_ARRAYSIZE(row_id), "notif%d", i);
            const ImRect hit(ImVec2(card.Min.x + px(4.f), card.Min.y + px(2.f)),
                             ImVec2(card.Max.x - px(40.f), card.Max.y - px(2.f)));
            if (row_hit(dl, row_id, hit, row_a, false) && unread)
                s.notif_read[i] = true;

            const float av = px(30.f);
            char ini[3];
            initials_of(n.who, ini);
            chip(dl, ImVec2(card.Min.x + px(14.f), card.GetCenter().y - av * 0.5f), av, av * 0.5f,
                 ini, mo::with_alpha(c_foreground, 0.06f * row_a),
                 mo::with_alpha(c_muted_foreground, row_a), n.person);

            const float tx = card.Min.x + px(14.f) + av + px(12.f);

            ImFont* wf = unread ? font_semibold(text_sm) : font_medium(text_sm);
            ImFont* af = font_regular(text_sm);
            ImFont* tf = font_regular(text_xs);

            const float when_w = text_width(tf, n.when);
            const float when_r = card.Max.x - px(44.f);
            const float line_w = (when_r - px(10.f) - when_w) - tx;

            float cx = tx;
            cx += draw_text_ellipsis(dl, wf, ImVec2(cx, card.GetCenter().y - px(15.f)),
                                     mo::with_alpha(c_foreground, row_a), n.who, line_w);
            cx += px(4.f);
            draw_text_ellipsis(dl, af, ImVec2(cx, card.GetCenter().y - px(15.f)),
                               mo::with_alpha(c_muted_foreground, row_a), n.did,
                               line_w - (cx - tx));

            draw_text_ellipsis(dl, af, ImVec2(tx, card.GetCenter().y + px(3.f)),
                               mo::with_alpha(c_foreground, 0.88f * row_a), n.about, line_w);

            draw_text(dl, tf, ImVec2(when_r - when_w, card.GetCenter().y - tf->LegacySize * 0.5f),
                      mo::with_alpha(c_muted_foreground, row_a), n.when);

            {
                char cid[24];
                ImFormatString(cid, IM_ARRAYSIZE(cid), "x%d", i);
                const ImRect xb(ImVec2(card.Max.x - px(36.f), card.GetCenter().y - px(13.f)),
                                ImVec2(card.Max.x - px(10.f), card.GetCenter().y + px(13.f)));

                ImGui::PushID(cid);
                const ImGuiID xid = ImGui::GetCurrentWindow()->GetID("x");
                ImGui::PopID();
                ImGui::SetCursorScreenPos(xb.Min);
                ImGui::ItemSize(ImVec2(0, 0));
                ImGui::ItemAdd(xb, xid);

                bool xh = false, xheld = false;
                if (ImGui::ButtonBehavior(xb, xid, &xh, &xheld))
                {
                    s.notif_undo = i;
                    s.undo_timer = 0.f;
                }
                if (xh)
                    dl->AddCircleFilled(xb.GetCenter(), px(13.f),
                                        mo::with_alpha(c_foreground, 0.07f * row_a));
                icons::draw(icons::id::cross, dl,
                            ImVec2(xb.GetCenter().x - px(7.f), xb.GetCenter().y - px(7.f)),
                            px(14.f),
                            mo::with_alpha(c_muted_foreground, (xh ? 1.f : 0.7f) * row_a));
            }

            dl->PopClipRect();

            y += open;
            total += open;
        }

        if (s.notif_undo >= 0)
        {
            const int i = s.notif_undo;
            s.undo_timer += dt;
            s.notif_slide[i] = ImMin(px(140.f), s.notif_slide[i] + dt * px(900.f));
            if (s.notif_slide[i] >= px(120.f))
                s.notif_height[i].to(0.f, mo::SPRING_LAYOUT, dt);

            if (s.undo_timer > 0.32f)
            {
                s.notif_gone[i] = true;
                s.notif_height[i] = mo::spring();
                s.notif_slide[i] = 0.f;
                s.notif_undo = -1;
                toast("Dismissed", k_notifications[i].about, toast_neutral);
            }
        }

        if (drawn == 0)
        {
            const ImRect card(ImVec2(x, start_y), ImVec2(x + col, start_y + px(120.f)));
            const float t = mo::EASE_OUT(ImClamp(s.entered / 0.4f, 0.f, 1.f));
            panel(dl, card, alpha * t);
            empty_state(dl, card, s.notif_tab == 2 ? "Nobody has said your name." : "Nothing new.",
                        alpha * t);
            y = card.Max.y;
        }
        break;
    }

    case 12:
    {

        if (s.reset_cascade < 1e5f)
        {
            const bool defaults[] = {true, false, true, true, true, false, true};
            s.reset_cascade += dt;
            for (int i = 0; i < IM_ARRAYSIZE(k_prefs); i++)
                if (s.reset_cascade > (float)i * 0.07f)
                    s.pref_on[i] = defaults[i];
            if (s.reset_cascade > 1.4f)
            {
                s.reset_cascade = 1e6f;
                toast("Preferences reset", "Back to how they shipped", toast_info);
            }
        }

        int index = 0;

        for (int g = 0; g < IM_ARRAYSIZE(k_pref_groups); g++)
        {
            const pref_group& grp = k_pref_groups[g];

            float body_h = 0.f;
            for (int r = 0; r < grp.count; r++)
            {
                const pref_row& pr = k_prefs[grp.first + r];
                const float extra_h = (pr.extra == extra_none)
                                          ? 0.f
                                          : (pr.extra == extra_quiet ? px(slider_h) + px(8.f)
                                                                     : px(select_h) + px(10.f));
                body_h += px(70.f) + px(sp_2) +
                          ((s.pref_on[grp.first + r] && pr.extra != extra_none) ? extra_h : 0.f);
            }

            char aid[16];
            ImFormatString(aid, IM_ARRAYSIZE(aid), "prefgrp%d", g);

            float body_alpha = 1.f;
            const float open_h = accordion(aid, dl, ImVec2(x, y), col, grp.title, &s.pref_open[g],
                                           body_h, alpha, &body_alpha);

            y += px(accordion_trigger_h);

            if (open_h <= 1.f)
            {

                index += grp.count;
                y += px(sp_3);
                continue;
            }

            const float body_top = y;
            dl->PushClipRect(ImVec2(x, body_top), ImVec2(x + col, body_top + open_h), true);
            y = body_top + open_h - body_h;

            for (int r = 0; r < grp.count; r++)
            {
                const int i = grp.first + r;
                const pref_row& pr = k_prefs[i];
                const float t = stagger(index++);

                const float extra_h = (pr.extra == extra_none)
                                          ? 0.f
                                          : (pr.extra == extra_quiet ? px(slider_h) + px(8.f)
                                                                     : px(select_h) + px(10.f));
                const float open =
                    s.pref_detail[i].to((s.pref_on[i] && pr.extra != extra_none) ? extra_h : 0.f,
                                        mo::SPRING_LAYOUT, dt);

                const float base_h = px(70.f);
                const ImRect card(ImVec2(x + px(14.f) * (1.f - t), y),
                                  ImVec2(x + col + px(14.f) * (1.f - t), y + base_h + open));
                const float row_a = alpha * t;

                dl->PushClipRect(ImVec2(x, y), ImVec2(x + col, y + base_h + open), true);
                panel(dl, card, row_a);

                ImFont* lf = font_medium(text_sm);
                draw_text_ellipsis(dl, lf, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(16.f)),
                                   mo::with_alpha(c_foreground, row_a), pr.label,
                                   col - px(sp_4) * 2.f - px(switch_w) - px(16.f));

                {
                    ImGui::PushID(2000 + i);
                    mo::spring* mixv = ui_runtime::animation_state<mo::spring>(
                        ImGui::GetCurrentWindow()->GetID("mix"));
                    ImGui::PopID();
                    const float m = mixv->to(s.pref_on[i] ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);

                    ImFont* df = font_regular(text_xs);
                    const float dy = card.Min.y + px(38.f);
                    const float dw = col - px(sp_4) * 2.f - px(switch_w) - px(16.f);

                    if (m < 0.995f)
                        draw_text_ellipsis(dl, df, ImVec2(card.Min.x + px(sp_4), dy + px(6.f) * m),
                                           mo::with_alpha(c_muted_foreground, (1.f - m) * row_a),
                                           pr.off_text, dw);
                    if (m > 0.005f)
                        draw_text_ellipsis(
                            dl, df, ImVec2(card.Min.x + px(sp_4), dy - px(6.f) * (1.f - m)),
                            mo::with_alpha(c_muted_foreground, m * row_a), pr.on_text, dw);
                }

                {
                    char sid[24];
                    ImFormatString(sid, IM_ARRAYSIZE(sid), "pref%d", i);
                    switch_toggle(
                        sid, ImVec2(card.Max.x - px(sp_4) - px(switch_w), card.Min.y + px(21.f)),
                        &s.pref_on[i]);
                }

                if (open > 2.f)
                {
                    const float iy = card.Min.y + base_h + open - extra_h;
                    const float iw = ImMin(col - px(sp_4) * 2.f, px(220.f));
                    const ImVec2 at(card.Min.x + px(sp_4), iy);

                    if (pr.extra == extra_theme)
                        select("pref-theme", at, iw, k_theme_names, IM_ARRAYSIZE(k_theme_names),
                               &s.pref_theme, "System");
                    else if (pr.extra == extra_digest)
                        select("pref-day", at, iw, k_digest_days, IM_ARRAYSIZE(k_digest_days),
                               &s.pref_digest_day, "Monday");
                    else
                        range_slider("pref-quiet", ImVec2(at.x, iy - px(6.f)), col - px(sp_4) * 2.f,
                                     &s.pref_quiet, 0.f, 1.f, 6);
                }

                dl->PopClipRect();
                y += base_h + open + px(sp_2);
            }

            dl->PopClipRect();
            y = body_top + open_h;
            y += px(sp_3);
        }

        y -= px(sp_3);
        y += px(sp_2);

        if (action("reset-prefs", ImVec2(x, y), 170.f, btn_idle, "Reset to defaults"))
            s.reset_cascade = 0.f;
        y += px(44.f);
        break;
    }

    default:
    {
        const ImRect card(ImVec2(x, y), ImVec2(x + col, y + px(120.f)));
        panel(dl, card, alpha);

        ImFont* f = font_regular(text_sm);
        draw_text_wrapped(
            dl, f, ImVec2(card.Min.x + px(sp_4), card.Min.y + px(sp_4)),
            mo::with_alpha(c_muted_foreground, alpha),
            "Nothing needs your attention here right now. Pick another view from the rail, "
            "or fold it away with Ctrl+B.",
            col - px(sp_4) * 2.f, px(22.f));
        y = card.Max.y;
        break;
    }
    }

    if (two_col)
    {

        float aside_h = page_aside(dl, nav, ImVec2(aside_x, aside_y), aside_w, alpha, aside_offset);

        const float more =
            page_aside_more(dl, nav, ImVec2(aside_x, aside_y + aside_h + px(sp_5)), aside_w, alpha);
        if (more > 0.f)
            aside_h += px(sp_5) + more;

        y = ImMax(y, aside_y + aside_h) + px(sp_6);
    }
    else
    {
        y += activity(dl, ImVec2(x, y + px(sp_6)), col, alpha) + px(sp_6);
    }

    s.content[slot] = (y + px(sp_6)) - (area.Min.y - scroll);

    dl->PopClipRect();
    scrollbar(dl, area, measured, scroll, alpha);
}
} // namespace solace
