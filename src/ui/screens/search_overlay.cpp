#include "ui/screens/search_overlay.h"
#include "ui/controls/caret.h"
#include "ui/controls/scroll.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace solace
{
namespace
{

constexpr mo::spring_cfg k_morph{248.28f, 24.58f, 1.f};

constexpr float k_clip_duration = 0.32f;

constexpr float k_morph_settle = 0.6f;

constexpr float k_face_duration = 0.1f;
constexpr float k_face_delay_open = 0.1f;
constexpr float k_face_delay_close = 0.12f;

constexpr float k_list_duration = 0.16f;
constexpr float k_list_delay_out = 0.18f;
constexpr float k_list_shift = 6.f;

constexpr float k_panel_round = 12.f;
constexpr float k_item_round = 8.f;
constexpr float k_kbd_round = 6.f;
constexpr float k_header_h = 48.f;
constexpr float k_kbd_h = 28.f;
constexpr float k_icon = 16.f;
constexpr float k_gap = 10.f;
constexpr float k_pad_x = 14.f;
constexpr float k_list_pad = 8.f;
constexpr float k_item_pad_x = 12.f;
constexpr float k_item_pad_y = 10.f;

struct search_state
{
    bool open = false;
    bool focus_next = false;

    char query[128] = "";
    int active = 0;
    std::string normalized_query;
    std::string haystack;
    std::vector<int> filtered;

    mo::spring shell_x, shell_y, shell_w, shell_h;
    bool seeded = false;

    mo::presence dialog;
    mo::spring opacity;
    float clip_t = 0.f;

    mo::spring highlight_y, highlight_h;
    bool highlight_seeded = false;

    bool strip_shortcut = false;
    float face_t = 1e6f;
    bool hovered = false;

    solace::caret text_caret;
    smooth_scroll list_scroll;
    ImRect anchor;
    bool have_anchor = false;
    const char* placeholder = "Search";
    const char* shortcut = "F";
};

search_state& state()
{
    static search_state s;
    return s;
}

void begin_search(search_state& state)
{
    state.open = true;
    state.face_t = 0.f;
    state.focus_next = true;
    state.query[0] = 0;
    state.active = 0;
    state.list_scroll.jump(0.0);
}

void normalize_query(const char* query, std::string& normalized)
{
    normalized.assign(query ? query : "");
    for (char& character : normalized)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    const std::size_t begin = normalized.find_first_not_of(" \t");
    if (begin == std::string::npos)
    {
        normalized.clear();
        return;
    }

    const std::size_t end = normalized.find_last_not_of(" \t");
    normalized.erase(end + 1);
    normalized.erase(0, begin);
}

bool matches(const search_item& item, const std::string& needle, std::string& haystack)
{
    if (needle.empty())
        return true;

    haystack.clear();
    if (item.title)
        haystack += item.title;
    haystack += ' ';
    if (item.description)
        haystack += item.description;
    haystack += ' ';
    if (item.keywords)
        haystack += item.keywords;

    for (char& character : haystack)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    return haystack.find(needle) != std::string::npos;
}

float item_height(const search_item& item)
{
    const float body = px(leading_sm) + (item.description ? px(leading_xs) : 0.f);
    return px(k_item_pad_y) * 2.f + body;
}

ImVec2 panel_size(const ImRect& anchor, const ImRect& viewport)
{
    const float w =
        ImMax(anchor.GetWidth(), ImMin(px(448.f), viewport.Max.x - anchor.Min.x - px(16.f)));
    const float h = ImMax(px(96.f), ImMin(px(288.f), viewport.Max.y - anchor.Min.y - px(80.f)));
    return ImVec2(w, h);
}

float kbd_width(ImFont* f, const char* label)
{
    return ImMax(px(k_kbd_h), text_width(f, label) + px(sp_2) * 2.f);
}

void draw_kbd(ImDrawList* dl, const ImVec2& top_left, const char* label, float alpha)
{
    ImFont* f = font_regular(text_xs);
    const float w = kbd_width(f, label);
    const ImRect r(top_left, ImVec2(top_left.x + w, top_left.y + px(k_kbd_h)));

    dl->AddRect(ImVec2(r.Min.x + px(0.5f), r.Min.y + px(0.5f)),
                ImVec2(r.Max.x - px(0.5f), r.Max.y - px(0.5f)), mo::with_alpha(c_border, alpha),
                px(k_kbd_round), px(1.f), ImDrawFlags_None);

    draw_text(dl, f,
              ImVec2(r.GetCenter().x - text_width(f, label) * 0.5f,
                     r.GetCenter().y - f->LegacySize * 0.5f),
              mo::with_alpha(c_muted_foreground, alpha), label);
}

void draw_shell(ImDrawList* dl, const ImRect& r, float bg_alpha, ImU32 stroke, float alpha,
                float blur)
{
    if (blur > 0.f)
        backdrop_blur(dl, r, px(blur), px(k_panel_round), alpha);
    dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_background, bg_alpha * alpha),
                      px(k_panel_round));
    dl->AddRect(ImVec2(r.Min.x + px(0.5f), r.Min.y + px(0.5f)),
                ImVec2(r.Max.x - px(0.5f), r.Max.y - px(0.5f)), mo::with_alpha(stroke, alpha),
                px(k_panel_round), px(1.f), ImDrawFlags_None);
}

void draw_face(ImDrawList* dl, const ImRect& anchor, const char* placeholder, const char* shortcut,
               float alpha)
{
    if (alpha <= 0.004f)
        return;

    const float cy = anchor.GetCenter().y;
    float x = anchor.Min.x + px(k_pad_x);

    icons::draw(icons::id::search, dl, ImVec2(x, cy - px(k_icon) * 0.5f), px(k_icon),
                mo::with_alpha(c_muted_foreground, alpha));
    x += px(k_icon) + px(k_gap);

    ImFont* f = font_regular(text_sm);
    float right = anchor.Max.x - px(k_pad_x);
    if (shortcut && *shortcut)
    {
        const float w = kbd_width(font_regular(text_xs), shortcut);
        right -= w;
        draw_kbd(dl, ImVec2(right, cy - px(k_kbd_h) * 0.5f), shortcut, alpha);
        right -= px(k_gap);
    }

    dl->PushClipRect(ImVec2(x, anchor.Min.y), ImVec2(ImMax(right, x), anchor.Max.y), true);
    draw_text(dl, f, ImVec2(x, cy - f->LegacySize * 0.5f),
              mo::with_alpha(c_muted_foreground, alpha), placeholder);
    dl->PopClipRect();
}
} // namespace

bool morphing_search_open()
{
    return state().open;
}

void morphing_search_trigger(const ImRect& anchor, float alpha, const char* placeholder,
                             const char* shortcut)
{
    search_state& s = state();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    s.anchor = anchor;
    s.have_anchor = true;
    s.placeholder = placeholder;
    s.shortcut = shortcut;

    ImGui::PushID("morphing-search");
    const ImGuiID id = window->GetID("trigger");
    ImGui::SetCursorScreenPos(anchor.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(anchor, id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(anchor, id, &hovered, &held);
    ImGui::PopID();
    s.hovered = hovered;

    if (pressed && !s.open)
        begin_search(s);

    const bool morphing = s.open || s.dialog.mounted;
    if (!morphing)
    {

        draw_shell(dl, anchor, 0.6f, hovered ? c_border_strong : c_border, alpha, 12.f);
        draw_face(dl, anchor, placeholder, shortcut, alpha);
    }
}

int morphing_search_overlay(const ImRect& viewport, const search_item* items, int count,
                            float alpha)
{
    search_state& s = state();
    if (!s.have_anchor)
        return -1;

    const float dt = ImGui::GetIO().DeltaTime;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImRect anchor = s.anchor;

    if (!s.open && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        s.strip_shortcut = true;
        begin_search(s);
    }

    const bool was_open = s.open;
    if (s.open && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        s.open = false;

    const bool mounted = s.dialog.update(s.open, dt, k_morph_settle);

    if (was_open != s.open)
        s.face_t = 0.f;
    s.face_t += dt;

    const ImVec2 size = panel_size(anchor, viewport);
    const ImRect target =
        s.open ? ImRect(anchor.Min,
                        ImVec2(anchor.Min.x + size.x, anchor.Min.y + px(k_header_h) + size.y))
               : anchor;

    if (!s.seeded)
    {
        s.shell_x.snap(target.Min.x);
        s.shell_y.snap(target.Min.y);
        s.shell_w.snap(target.GetWidth());
        s.shell_h.snap(target.GetHeight());
        s.seeded = true;
    }

    const ImVec2 shell_min(s.shell_x.to(target.Min.x, k_morph, dt),
                           s.shell_y.to(target.Min.y, k_morph, dt));
    const float shell_w = s.shell_w.to(target.GetWidth(), k_morph, dt);
    const float shell_h = s.shell_h.to(target.GetHeight(), k_morph, dt);
    const ImRect shell_rect(shell_min, ImVec2(shell_min.x + shell_w, shell_min.y + shell_h));

    const float opacity = s.opacity.to(s.open ? 1.f : 0.f, k_morph, dt);
    s.clip_t = ImClamp(s.clip_t + (s.open ? 1.f : -1.f) * dt / k_clip_duration, 0.f, 1.f);
    const float clip = mo::EASE_OUT(s.clip_t);

    const float face_delay = s.open ? k_face_delay_open : k_face_delay_close;
    const float face_p = mo::EASE_OUT(ImClamp((s.face_t - face_delay) / k_face_duration, 0.f, 1.f));
    const float face_a = (s.open ? 1.f - face_p : face_p) * alpha;

    if (!mounted && !s.open)
    {

        return -1;
    }

    if (s.open)
    {
        claim_pointer();

        ImGui::PushID("morphing-search-catcher");
        const ImGuiID cid = window->GetID("c");
        ImGui::SetCursorScreenPos(viewport.Min);
        ImGui::ItemSize(ImVec2(0, 0));
        ImGui::ItemAdd(viewport, cid);
        bool ch = false, cheld = false;
        if (ImGui::ButtonBehavior(viewport, cid, &ch, &cheld))
            s.open = false;
        ImGui::PopID();
    }

    draw_shell(dl, shell_rect, s.open || mounted ? 0.9f : 0.6f,
               s.hovered && !s.open ? c_border_strong : c_border, alpha,
               mo::lerp(12.f, 24.f, opacity));

    const float inset_r = (size.x - anchor.GetWidth()) * (1.f - clip);
    const float inset_b = size.y * (1.f - clip);
    const ImRect dialog(anchor.Min,
                        ImVec2(anchor.Min.x + size.x, anchor.Min.y + px(k_header_h) + size.y));

    dl->PushClipRect(dialog.Min, ImVec2(dialog.Max.x - inset_r, dialog.Max.y - inset_b), true);

    const float da = opacity * alpha;

    const ImRect header(dialog.Min, ImVec2(dialog.Max.x, dialog.Min.y + px(k_header_h)));
    const float hcy = header.GetCenter().y;

    icons::draw(icons::id::search, dl, ImVec2(header.Min.x + px(k_pad_x), hcy - px(k_icon) * 0.5f),
                px(k_icon), mo::with_alpha(c_muted_foreground, da));

    ImFont* fld = font_regular(text_sm);
    const float esc_w = kbd_width(font_regular(text_xs), "Esc");
    draw_kbd(dl, ImVec2(header.Max.x - px(k_pad_x) - esc_w, hcy - px(k_kbd_h) * 0.5f), "Esc", da);

    const float field_x = header.Min.x + px(k_pad_x) + px(k_icon) + px(k_gap);
    const float field_w =
        ImMax((header.Max.x - px(k_pad_x) - esc_w - px(k_gap)) - field_x, px(40.f));

    dl->AddRectFilled(ImVec2(header.Min.x, header.Max.y - px(1.f)),
                      ImVec2(header.Max.x, header.Max.y), mo::with_alpha(c_border, da));

    ImGui::PushID("morphing-search-field");
    ui_runtime::push_font(fld);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(0.f, (px(k_header_h) - fld->LegacySize) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, mo::with_alpha(c_foreground, da));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32_BLACK_TRANS);

    ImGui::SetCursorScreenPos(header.Min);
    ImGui::SetNextItemWidth(field_x - header.Min.x + field_w);
    if (s.focus_next)
    {
        ImGui::SetKeyboardFocusHere();
        s.focus_next = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(field_x - header.Min.x, (px(k_header_h) - fld->LegacySize) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, IM_COL32_BLACK_TRANS);

    const bool typed = ImGui::InputText("##q", s.query, IM_ARRAYSIZE(s.query));
    const bool field_active = ImGui::IsItemActive();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ui_runtime::pop_font();
    ImGui::PopID();

    draw_caret(dl, s.text_caret, header, px(leading_sm), field_active && s.open, c_foreground, da);

    if (s.strip_shortcut && s.query[0] != 0)
    {
        if (s.query[0] == 'f' || s.query[0] == 'F')
            memmove(s.query, s.query + 1, strlen(s.query));
        s.strip_shortcut = false;
    }

    if (typed)
    {
        s.active = 0;
        s.list_scroll.jump(0.0);
    }

    if (s.query[0] == 0)
        draw_text(dl, fld, ImVec2(field_x, hcy - fld->LegacySize * 0.5f),
                  mo::with_alpha(c_muted_foreground, da), s.placeholder);

    std::vector<int>& filtered = s.filtered;
    filtered.clear();
    filtered.reserve((size_t)count);
    normalize_query(s.query, s.normalized_query);
    s.haystack.reserve(96);
    for (int i = 0; i < count; i++)
        if (matches(items[i], s.normalized_query, s.haystack))
            filtered.push_back(i);

    if (s.active >= (int)filtered.size())
        s.active = ImMax(0, (int)filtered.size() - 1);

    if (!filtered.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            s.active = ImMin(s.active + 1, (int)filtered.size() - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            s.active = ImMax(s.active - 1, 0);
    }

    const ImRect list(ImVec2(dialog.Min.x, header.Max.y), dialog.Max);

    float content = px(k_list_pad) * 2.f;
    for (size_t i = 0; i < filtered.size(); i++)
        content += item_height(items[filtered[i]]);
    if (filtered.empty())
        content = px(64.f) + px(leading_sm);

    const float lp = s.open ? mo::EASE_OUT(ImClamp(s.face_t / k_list_duration, 0.f, 1.f))
                            : 1.f - mo::EASE_OUT(ImClamp(
                                        (s.face_t - k_list_delay_out) / k_list_duration, 0.f, 1.f));
    const float list_a = lp * da;
    const float list_dy = px(k_list_shift) * (1.f - lp);

    int chosen = -1;

    dl->PushClipRect(list.Min, list.Max, true);
    const float offset =
        s.open ? scroll_area(s.list_scroll, list, content, true) : (float)s.list_scroll.animated;

    if (filtered.empty())
    {
        ImFont* f = font_regular(text_sm);
        const char* msg = "No results found.";
        draw_text(
            dl, f,
            ImVec2(list.GetCenter().x - text_width(f, msg) * 0.5f, list.Min.y + px(32.f) + list_dy),
            mo::with_alpha(c_muted_foreground, list_a), msg);
    }
    else
    {

        float y = list.Min.y + px(k_list_pad) - offset + list_dy;
        float active_y = y, active_h = 0.f;

        for (size_t k = 0; k < filtered.size(); k++)
        {
            const search_item& item = items[filtered[k]];
            const float h = item_height(item);
            const ImRect row(ImVec2(list.Min.x + px(k_list_pad), y),
                             ImVec2(list.Max.x - px(k_list_pad), y + h));

            if (s.open && row.Max.y > list.Min.y && row.Min.y < list.Max.y)
            {
                ImGui::PushID((int)k + 1000);
                const ImGuiID rid = window->GetID("row");
                ImGui::SetCursorScreenPos(row.Min);
                ImGui::ItemSize(ImVec2(0, 0));
                ImGui::ItemAdd(row, rid);
                bool rh = false, rheld = false;
                const bool rp = ImGui::ButtonBehavior(row, rid, &rh, &rheld);
                ImGui::PopID();

                if (rh &&
                    (ImGui::GetIO().MouseDelta.x != 0.f || ImGui::GetIO().MouseDelta.y != 0.f))
                    s.active = (int)k;
                if (rp)
                    chosen = filtered[k];
            }

            if ((int)k == s.active)
            {
                active_y = row.Min.y;
                active_h = h;
            }
            y += h;
        }

        if (!s.highlight_seeded)
        {
            s.highlight_y.snap(active_y);
            s.highlight_h.snap(active_h);
            s.highlight_seeded = true;
        }

        const float hy = s.highlight_y.to(active_y, mo::SPRING_LAYOUT, dt);
        const float hh = s.highlight_h.to(active_h, mo::SPRING_LAYOUT, dt);

        dl->AddRectFilled(ImVec2(list.Min.x + px(k_list_pad), hy),
                          ImVec2(list.Max.x - px(k_list_pad), hy + hh),
                          mo::with_alpha(c_foreground, 0.05f * list_a), px(k_item_round));

        y = list.Min.y + px(k_list_pad) - offset + list_dy;
        for (size_t k = 0; k < filtered.size(); k++)
        {
            const search_item& item = items[filtered[k]];
            const float h = item_height(item);

            if (y + h > list.Min.y && y < list.Max.y)
            {
                float x = list.Min.x + px(k_list_pad) + px(k_item_pad_x);
                const float cy = y + h * 0.5f;

                if (item.icon != icons::id::none)
                {
                    icons::draw(item.icon, dl, ImVec2(x, cy - px(k_icon) * 0.5f), px(k_icon),
                                mo::with_alpha(c_muted_foreground, list_a));
                    x += px(k_icon) + px(k_gap);
                }

                ImFont* ft = font_medium(text_sm);
                draw_text(dl, ft, ImVec2(x, y + px(k_item_pad_y) + line_top(ft, px(leading_sm))),
                          mo::with_alpha(c_foreground, list_a), item.title);

                if (item.description)
                {
                    ImFont* fd = font_regular(text_xs);
                    draw_text(dl, fd,
                              ImVec2(x, y + px(k_item_pad_y) + px(leading_sm) +
                                            line_top(fd, px(leading_xs))),
                              mo::with_alpha(c_muted_foreground, list_a), item.description);
                }
            }
            y += h;
        }

        scrollbar(dl, list, content, offset, list_a);

        if (s.open && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && s.active < (int)filtered.size())
            chosen = filtered[s.active];
    }
    dl->PopClipRect();
    dl->PopClipRect();

    draw_face(dl, anchor, s.placeholder, s.shortcut, face_a);

    if (chosen >= 0)
    {
        s.open = false;
        s.face_t = 0.f;
        s.query[0] = 0;
    }
    return chosen;
}
} // namespace solace
