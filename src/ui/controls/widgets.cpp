#include "ui/controls/widgets.h"

#include "ui/foundation/draw.h"
#include "ui/foundation/icons.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/runtime.h"
#include "ui/foundation/theme.h"

#include "imgui_internal.h"

#include <string.h>

#include <string>
#include <vector>

namespace solace
{
namespace
{

constexpr mo::spring_cfg k_thumb{800.f, 80.f, 4.f};
constexpr float k_switch_pad = 4.f;
constexpr float k_thumb_size = 20.f;

constexpr mo::spring_cfg k_indicator{170.f, 24.f, 1.2f};

constexpr float k_list_stagger = 0.035f;
constexpr float k_list_delay = 0.05f;
constexpr float k_item_move = 0.18f;
constexpr float k_item_blur = 3.f;

constexpr mo::spring_cfg k_glide{700.f, 50.f, 0.5f};
constexpr mo::spring_cfg k_bouncy{500.f, 14.f, 0.7f};

constexpr mo::spring_cfg k_stack{420.f, 34.f, 0.9f};
constexpr float k_toast_life = 4.f;
constexpr int k_toast_visible = 4;
constexpr float k_toast_exit = 0.18f;

struct switch_state
{
    mo::spring travel;
    mo::spring squish;
    color_tween track;
};

struct tabs_state
{
    mo::spring x, w;
    bool seeded = false;
    std::vector<color_tween> text;
};

struct select_state
{
    bool open = false;
    mo::presence panel;
    mo::spring chevron;
    color_tween border;
};

struct slider_state
{
    mo::spring fill;
    mo::spring handle;
};

struct toast_entry
{
    std::string title, description;
    toast_status status = toast_info;
    float age = 0.f;
    bool leaving = false;
    float leave_t = 0.f;
    mo::spring y, scale, opacity;
    bool seeded = false;
};

std::vector<toast_entry>& toast_list()
{
    static std::vector<toast_entry> v;
    return v;
}

struct overlay
{
    select_state* state = nullptr;
    ImRect trigger;
    float width = 0.f;
    const char* const* options = nullptr;
    int option_count = 0;
    int* value = nullptr;
};

std::vector<overlay>& overlays()
{
    static std::vector<overlay> v;
    return v;
}

ImU32 status_colour(toast_status s)
{
    switch (s)
    {
    case toast_info:
    case toast_loading:
        return c_primary;
    case toast_success:
        return is_dark() ? c_emerald_400 : c_emerald_600;
    case toast_error:
        return c_destructive;
    default:
        return c_muted_foreground;
    }
}

ImU32 status_tint(toast_status s)
{
    switch (s)
    {
    case toast_info:
    case toast_loading:
        return mo::with_alpha(c_primary, 0.10f);
    case toast_success:
        return mo::with_alpha(c_emerald_500, 0.10f);
    case toast_error:
        return mo::with_alpha(c_destructive, 0.10f);
    default:
        return mo::with_alpha(c_primary, 0.05f);
    }
}

icons::id status_icon(toast_status s)
{
    switch (s)
    {
    case toast_info:
        return icons::id::info;
    case toast_loading:
        return icons::id::loader;
    case toast_success:
        return icons::id::check;
    case toast_error:
        return icons::id::circle_alert;
    default:
        return icons::id::bell;
    }
}
} // namespace

namespace
{
struct otp_state
{
    int active = 0;
    float caret = 0.f;
    float shake = 1e6f;
    float entry[12] = {};
    char seen[13] = {};
    bool focused = false;
    otp_status last_status = otp_idle;
};

constexpr float k_otp_entry = 0.22f;
constexpr float k_otp_shake = 0.45f;
} // namespace

float otp_width(int length)
{
    if (length <= 0)
        return 0.f;
    return px(otp_cell_w) * (float)length + px(otp_gap) * (float)(length - 1);
}

bool otp_input(const char* id, ImDrawList* dl, const ImVec2& pos, char* buf, int length,
               otp_status status, float alpha)
{
    if (!buf || length <= 0)
        return false;
    length = ImMin(length, 12);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("otp");
    otp_state* st = ui_runtime::animation_state<otp_state>(item_id);
    ImGui::PopID();

    const float w = otp_width(length);
    const float h = px(otp_cell_h);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(ImVec2(0.f, 0.f));
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    if (ImGui::ButtonBehavior(bb, item_id, &hovered, &held))
    {
        st->focused = true;
        ImGui::SetActiveID(item_id, window);
        ImGui::SetFocusID(item_id, window);
    }
    else if (io.MouseClicked[0] && !hovered)
    {
        st->focused = false;
    }

    bool completed = false;
    if (st->focused && status != otp_success)
    {
        for (int n = 0; n < io.InputQueueCharacters.Size; n++)
        {
            const ImWchar c = io.InputQueueCharacters[n];
            if (c < '0' || c > '9')
                continue;

            if (st->active < length)
            {
                buf[st->active] = (char)c;
                st->active = ImMin(st->active + 1, length);
            }
        }
        io.InputQueueCharacters.resize(0);

        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
        {
            if (st->active > 0 && buf[st->active] == 0)
            {
                st->active--;
                buf[st->active] = 0;
            }
            else if (st->active < length && buf[st->active] != 0)
            {
                buf[st->active] = 0;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            st->active = ImMax(0, st->active - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            st->active = ImMin(length - 1, st->active + 1);
    }

    buf[length] = 0;

    st->caret += dt;
    st->shake += dt;

    for (int i = 0; i < length; i++)
    {
        const char now = buf[i];
        if (st->seen[i] != now)
        {
            st->seen[i] = now;
            st->entry[i] = 0.f;
        }
        else
        {
            st->entry[i] += dt;
        }
    }

    if (status == otp_error && st->last_status != otp_error)
        st->shake = 0.f;
    st->last_status = status;

    float shake_x = 0.f;
    if (st->shake < k_otp_shake)
    {
        static const float k_keys[] = {0.f, -5.f, 5.f, -3.f, 3.f, -1.f, 0.f};
        const float u = mo::EASE_OUT(st->shake / k_otp_shake) * 6.f;
        const int k = ImMin((int)u, 5);
        shake_x = px(mo::lerp(k_keys[k], k_keys[k + 1], u - (float)k));
    }

    for (int i = 0; i < length; i++)
    {
        const float cx = pos.x + shake_x + (px(otp_cell_w) + px(otp_gap)) * (float)i;
        const ImRect cell(ImVec2(cx, pos.y), ImVec2(cx + px(otp_cell_w), pos.y + h));

        const bool filled = buf[i] != 0;
        const bool active = st->focused && i == st->active && status != otp_success;

        ImU32 border = mo::with_alpha(c_border, alpha);
        if (status == otp_error)
            border = mo::with_alpha(c_destructive, 0.6f * alpha);
        else if (status == otp_success)
            border = mo::with_alpha(c_success, 0.6f * alpha);
        else if (active)
            border = mo::with_alpha(c_foreground, alpha);
        else if (filled)
            border = mo::with_alpha(c_border_strong, alpha);

        dl->AddRectFilled(cell.Min, cell.Max, mo::with_alpha(c_card, 0.5f * alpha), px(12.f));
        dl->AddRect(ImVec2(cell.Min.x + px(0.5f), cell.Min.y + px(0.5f)),
                    ImVec2(cell.Max.x - px(0.5f), cell.Max.y - px(0.5f)), border, px(12.f), px(1.f),
                    ImDrawFlags_None);

        if (filled)
        {

            const float t = mo::EASE_OUT(ImClamp(st->entry[i] / k_otp_entry, 0.f, 1.f));
            const char text[2] = {buf[i], 0};

            ImFont* f = font_semibold(20.f);
            const ImVec2 at(cell.GetCenter().x - text_width(f, text) * 0.5f,
                            cell.GetCenter().y - f->LegacySize * 0.5f + px(14.f) * (1.f - t));

            dl->PushClipRect(cell.Min, cell.Max, true);
            draw_text_blur(dl, f, at, mo::with_alpha(c_foreground, t * alpha), text,
                           px(4.f) * (1.f - t));
            dl->PopClipRect();
        }

        if (active && fmodf(st->caret, 1.f) < 0.5f)
        {
            const float cxx = filled ? cell.Max.x - px(3.f + 1.f) : cell.GetCenter().x - px(0.5f);
            dl->AddRectFilled(ImVec2(cxx, cell.Min.y + px(14.f)),
                              ImVec2(cxx + px(1.f), cell.Max.y - px(14.f)),
                              mo::with_alpha(c_foreground, alpha));
        }
    }

    completed = true;
    for (int i = 0; i < length; i++)
        if (buf[i] == 0)
        {
            completed = false;
            break;
        }

    return completed && st->entry[length - 1] < dt * 1.5f;
}

namespace
{
struct number_state
{
    float from = 0.f, to = 0.f, t = 1.f, current = 0.f;
    bool seeded = false;
};
} // namespace

float number_value(const char* id, float target, float duration)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    number_state* st = ui_runtime::animation_state<number_state>(window->GetID("num"));
    ImGui::PopID();

    if (!st->seeded)
    {

        st->seeded = true;
        st->from = 0.f;
        st->to = target;
        st->t = 0.f;
    }
    else if (fabsf(target - st->to) > 1e-4f)
    {
        st->from = st->current;
        st->to = target;
        st->t = 0.f;
    }

    st->t = ImMin(1.f, st->t + ImGui::GetIO().DeltaTime / ImMax(duration, 0.01f));
    st->current = st->from + (st->to - st->from) * mo::EASE_OUT(st->t);
    return st->current;
}

namespace
{
struct badge_state
{
    mo::spring roll;
    char last[48] = {};
};

ImU32 badge_tone(badge_status status)
{
    switch (status)
    {
    case badge_good:
        return c_success;
    case badge_warn:
        return c_amber_400;
    case badge_bad:
        return c_destructive;
    case badge_info:
        return c_foreground;
    default:
        return c_muted_foreground;
    }
}
} // namespace

float badge_width(const char* label, bool medium)
{
    ImFont* f = medium ? font_medium(12.f) : font_medium(11.f);
    return text_width(f, label ? label : "") + px(medium ? 3.f : 2.f) * 2.f + px(12.f);
}

void badge(const char* id, ImDrawList* dl, const ImVec2& pos, const char* label,
           badge_status status, bool medium, float alpha)
{
    if (!label)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    badge_state* st = ui_runtime::animation_state<badge_state>(window->GetID("badge"));
    ImGui::PopID();

    if (strncmp(st->last, label, sizeof(st->last) - 1) != 0)
    {
        ImStrncpy(st->last, label, sizeof(st->last));
        st->roll = mo::spring();
    }

    const float dt = ImGui::GetIO().DeltaTime;
    const float t = ImClamp(st->roll.to(1.f, mo::spring_cfg{210.f, 24.f, 0.85f}, dt), 0.f, 1.f);

    const float h = px(medium ? 32.f : 24.f);
    const float w = badge_width(label, medium);
    const ImU32 tone = badge_tone(status);

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), mo::with_alpha(tone, 0.10f * alpha),
                      h * 0.5f);
    dl->AddRect(ImVec2(pos.x + px(0.5f), pos.y + px(0.5f)),
                ImVec2(pos.x + w - px(0.5f), pos.y + h - px(0.5f)),
                mo::with_alpha(tone, 0.30f * alpha), h * 0.5f, px(1.f), ImDrawFlags_None);

    ImFont* f = medium ? font_medium(12.f) : font_medium(11.f);
    const float dy = h * 0.85f * (1.f - t);
    const float blur = px(5.f) * (1.f - t);

    dl->PushClipRect(pos, ImVec2(pos.x + w, pos.y + h), true);
    draw_text_blur(
        dl, f,
        ImVec2(pos.x + (w - text_width(f, label)) * 0.5f, pos.y + (h - f->LegacySize) * 0.5f + dy),
        mo::with_alpha(tone, t * alpha), label, blur);
    dl->PopClipRect();
}

namespace
{
struct accordion_state
{
    mo::spring height, chevron;
    float text_t = 0.f;
    bool was_open = false;
};
} // namespace

float accordion(const char* id, ImDrawList* dl, const ImVec2& pos, float width, const char* title,
                bool* open, float body_h, float alpha, float* out_text_alpha)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("acc");
    accordion_state* st = ui_runtime::animation_state<accordion_state>(item_id);
    ImGui::PopID();

    const float trig = px(accordion_trigger_h);
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + trig));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(ImVec2(0.f, 0.f));
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    if (ImGui::ButtonBehavior(bb, item_id, &hovered, &held))
        *open = !*open;

    if (st->was_open != *open)
    {
        st->was_open = *open;
        st->text_t = 0.f;
    }

    const mo::spring_cfg grow = mo::from_bounce(0.58f, 0.32f);
    const mo::spring_cfg shrink = mo::from_bounce(0.46f, 0.26f);

    const float h = st->height.to(*open ? body_h : 0.f, *open ? grow : shrink, dt);
    const float turn = st->chevron.to(*open ? 1.f : 0.f, mo::from_bounce(0.42f, 0.28f), dt);

    st->text_t = ImMin(1.f, st->text_t + dt / 0.18f);
    if (out_text_alpha)
        *out_text_alpha = *open ? mo::EASE_OUT(st->text_t) : 1.f - mo::EASE_OUT(st->text_t);

    if (hovered || held)
        dl->AddRectFilled(bb.Min, bb.Max,
                          mo::with_alpha(c_foreground, (held ? 0.05f : 0.03f) * alpha), px(28.f));

    ImFont* tf = font_medium(text_sm);
    draw_text_ellipsis(
        dl, tf, ImVec2(bb.Min.x + px(sp_4), bb.GetCenter().y - tf->LegacySize * 0.5f),
        mo::with_alpha(c_foreground, alpha), title, width - px(sp_4) * 2.f - px(24.f));

    {
        const ImVec2 c(bb.Max.x - px(sp_4) - px(6.f), bb.GetCenter().y);
        const float a = turn * IM_PI;
        const float ca = cosf(a), sa = sinf(a);
        const float arm = px(5.f);

        auto rot = [&](float ox, float oy)
        { return ImVec2(c.x + ox * ca - oy * sa, c.y + ox * sa + oy * ca); };

        const ImU32 col = mo::with_alpha(c_muted_foreground, alpha);
        dl->AddLine(rot(-arm, -arm * 0.5f), rot(0.f, arm * 0.5f), col, px(1.6f));
        dl->AddLine(rot(0.f, arm * 0.5f), rot(arm, -arm * 0.5f), col, px(1.6f));
    }

    return h;
}

bool switch_toggle(const char* id, const ImVec2& pos, bool* checked, const char* label,
                   bool disabled)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("sw");

    ImFont* f = font_regular(text_sm);
    const float label_w = label ? text_width(f, label) + px(sp_3) : 0.f;
    const ImRect bb(pos, ImVec2(pos.x + px(switch_w) + label_w, pos.y + px(switch_h)));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    if (pressed && !disabled)
        *checked = !*checked;

    switch_state* st = ui_runtime::animation_state<switch_state>(item_id);

    const float alpha = disabled ? 0.6f : 1.f;
    const float t = st->travel.to(*checked ? 1.f : 0.f, k_thumb, dt);
    const float squish = st->squish.to((held && !disabled) ? 0.9f : 1.f, k_thumb, dt);

    const ImU32 track =
        st->track.update(*checked ? c_primary : mo::with_alpha(c_muted_foreground, 0.6f), dt, 0.2f);

    const ImRect rail(pos, ImVec2(pos.x + px(switch_w), pos.y + px(switch_h)));
    dl->AddRectFilled(rail.Min, rail.Max, mo::with_alpha(track, alpha), rail.GetHeight() * 0.5f);

    const float travel = px(switch_w - k_switch_pad * 2.f - k_thumb_size);
    const float thumb = px(k_thumb_size) * squish;
    const ImVec2 tc(rail.Min.x + px(k_switch_pad) + px(k_thumb_size) * 0.5f + travel * t,
                    rail.GetCenter().y);

    dl->AddRectFilled(ImVec2(tc.x - thumb * 0.5f, tc.y - thumb * 0.5f),
                      ImVec2(tc.x + thumb * 0.5f, tc.y + thumb * 0.5f),
                      mo::with_alpha(c_background, alpha), thumb * 0.5f);

    if (label)
        draw_text(dl, f, ImVec2(rail.Max.x + px(sp_3), rail.GetCenter().y - f->LegacySize * 0.5f),
                  mo::with_alpha(c_foreground, alpha), label);

    ImGui::PopID();
    return pressed && !disabled;
}

float tabs_height(tabs_variant variant)
{
    if (variant == tabs_underline)
        return px(44.f);
    return px(variant == tabs_pill ? 38.f : 32.f);
}

namespace
{
float tab_label_width(const char* label, tabs_variant variant)
{
    ImFont* f = font_medium(text_sm);
    const float pad = (variant == tabs_underline) ? px(sp_3) * 2.f : px(14.f) * 2.f;
    return text_width(f, label) + pad;
}
} // namespace

float tabs_width(const char* const* labels, int count, tabs_variant variant)
{
    float w = 0.f;
    for (int i = 0; i < count; i++)
        w += tab_label_width(labels[i], variant);

    if (variant == tabs_pill)
        w += px(4.f) * 2.f + px(4.f) * (float)ImMax(count - 1, 0);
    if (variant == tabs_segment)
        w += px(2.f) * 2.f;
    if (variant == tabs_underline)
        w += px(4.f) * (float)ImMax(count - 1, 0);
    return w;
}

bool tabs(const char* id, const ImVec2& pos, const char* const* labels, int count, int* active,
          tabs_variant variant)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    tabs_state* st = ui_runtime::animation_state<tabs_state>(window->GetID("tabs"));
    if ((int)st->text.size() != count)
        st->text.assign(count, color_tween());

    const float h = tabs_height(variant);
    const float total = tabs_width(labels, count, variant);
    const ImRect list(pos, ImVec2(pos.x + total, pos.y + h));

    if (variant == tabs_pill)
        dl->AddRectFilled(list.Min, list.Max, c_card, h * 0.5f);
    else if (variant == tabs_segment)
        dl->AddRectFilled(list.Min, list.Max, c_card, px(8.f));
    else
        dl->AddRectFilled(ImVec2(list.Min.x, list.Max.y - px(1.f)), list.Max, c_border);

    const float pad = (variant == tabs_pill) ? px(4.f) : (variant == tabs_segment ? px(2.f) : 0.f);
    const float gap = (variant == tabs_segment) ? 0.f : px(4.f);

    bool changed = false;
    float x = list.Min.x + pad;
    ImRect active_rect;

    for (int i = 0; i < count; i++)
    {
        const float w = tab_label_width(labels[i], variant);
        const ImRect bb(ImVec2(x, list.Min.y + pad), ImVec2(x + w, list.Max.y - pad));

        ImGui::PushID(i);
        const ImGuiID tid = window->GetID("tab");
        ImGui::SetCursorScreenPos(bb.Min);
        ImGui::ItemSize(ImVec2(0, 0));
        ImGui::ItemAdd(bb, tid);
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(bb, tid, &hovered, &held) && *active != i)
        {
            *active = i;
            changed = true;
        }
        ImGui::PopID();

        if (*active == i)
            active_rect = bb;

        x += w + gap;
    }

    if (count > 0)
    {
        if (!st->seeded)
        {
            st->x.snap(active_rect.Min.x);
            st->w.snap(active_rect.GetWidth());
            st->seeded = true;
        }
        const float ix = st->x.to(active_rect.Min.x, k_indicator, dt);
        const float iw = st->w.to(active_rect.GetWidth(), k_indicator, dt);

        if (variant == tabs_underline)
            dl->AddRectFilled(ImVec2(ix, list.Max.y - px(1.f)), ImVec2(ix + iw, list.Max.y),
                              c_primary);
        else
            dl->AddRectFilled(ImVec2(ix, active_rect.Min.y), ImVec2(ix + iw, active_rect.Max.y),
                              c_primary,
                              variant == tabs_pill ? active_rect.GetHeight() * 0.5f : px(8.f));
    }

    x = list.Min.x + pad;
    ImFont* f = font_medium(text_sm);
    for (int i = 0; i < count; i++)
    {
        const float w = tab_label_width(labels[i], variant);
        const bool is_active = (*active == i);
        const ImU32 target = (variant == tabs_underline)
                                 ? (is_active ? c_foreground : c_muted_foreground)
                                 : (is_active ? c_primary_foreground : c_muted_foreground);

        const ImU32 col = st->text[i].update(target, dt, 0.15f);
        const float lw = text_width(f, labels[i]);
        draw_text(dl, f, ImVec2(x + (w - lw) * 0.5f, list.GetCenter().y - f->LegacySize * 0.5f),
                  col, labels[i]);

        x += w + gap;
    }

    ImGui::SetCursorScreenPos(ImVec2(list.Min.x, list.Max.y));
    ImGui::PopID();
    return changed;
}

void select(const char* id, const ImVec2& pos, float width, const char* const* options, int count,
            int* value, const char* placeholder)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("sel");
    select_state* st = ui_runtime::animation_state<select_state>(item_id);

    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + px(select_h)));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    if (ImGui::ButtonBehavior(bb, item_id, &hovered, &held))
        st->open = !st->open;

    st->panel.update(st->open, dt, 0.18f);

    const ImU32 border =
        st->border.update(hovered || st->open ? c_border_strong : c_border, dt, 0.15f);
    dl->AddRectFilled(bb.Min, bb.Max, c_background, px(12.f));
    dl->AddRect(ImVec2(bb.Min.x + px(0.5f), bb.Min.y + px(0.5f)),
                ImVec2(bb.Max.x - px(0.5f), bb.Max.y - px(0.5f)), border, px(12.f), px(1.f),
                ImDrawFlags_None);

    ImFont* f = font_regular(text_sm);
    const bool has_value = (*value >= 0 && *value < count);
    draw_text(dl, f, ImVec2(bb.Min.x + px(sp_3), bb.GetCenter().y - f->LegacySize * 0.5f),
              has_value ? c_foreground : c_muted_foreground,
              has_value ? options[*value] : placeholder);

    const float rot =
        st->chevron.to(st->open ? 1.f : 0.f, mo::spring_cfg{260.f, 18.f, 1.f}, dt) * IM_PI;
    const float box = px(16.f);
    const ImVec2 cc(bb.Max.x - px(sp_3) - box * 0.5f, bb.GetCenter().y);
    const int rotation_start = draw_utils::rotation_start(dl);
    icons::draw(icons::id::chevron_down, dl, ImVec2(cc.x - box * 0.5f, cc.y - box * 0.5f), box,
                c_muted_foreground);
    draw_utils::rotate_vertices(dl, rotation_start, rot, cc);

    if (st->panel.mounted)
    {
        overlay ov;
        ov.state = st;
        ov.trigger = bb;
        ov.width = width;
        ov.value = value;
        ov.options = options;
        ov.option_count = count;
        overlays().push_back(ov);
    }

    ImGui::PopID();
}

bool range_slider(const char* id, const ImVec2& pos, float width, float* value, float min_value,
                  float max_value, int ticks)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("slider");
    slider_state* st = ui_runtime::animation_state<slider_state>(item_id);

    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + px(slider_h)));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    ImGui::ButtonBehavior(bb, item_id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);

    bool changed = false;
    if (held)
    {
        const float t =
            ImClamp((ImGui::GetIO().MousePos.x - bb.Min.x) / ImMax(bb.GetWidth(), 1.f), 0.f, 1.f);
        const float next = min_value + t * (max_value - min_value);
        if (next != *value)
        {
            *value = next;
            changed = true;
        }
    }
    const float norm =
        ImClamp((*value - min_value) / ImMax(max_value - min_value, 0.0001f), 0.f, 1.f);
    const float smooth = st->fill.to(norm, k_glide, dt);
    const float grow = st->handle.to(held ? 1.35f : 1.f, k_bouncy, dt);

    const float track_h = px(k_slider_track_h);
    const float ty = bb.GetCenter().y;
    const ImVec2 t0(bb.Min.x, ty - track_h * 0.5f);
    const ImVec2 t1(bb.Max.x, ty + track_h * 0.5f);
    const float track_r = track_h * 0.5f;

    dl->AddRectFilled(t0, t1, c_card, track_r);
    dl->AddRectFilled(t0, ImVec2(bb.Min.x + bb.GetWidth() * smooth, t1.y),
                      mo::with_alpha(c_foreground, 0.10f), track_r);

    if (ticks > 1)
        for (int i = 0; i < ticks; i++)
        {
            const float t = (float)i / (float)(ticks - 1);
            dl->AddCircleFilled(ImVec2(bb.Min.x + bb.GetWidth() * t, ty), px(2.f),
                                mo::with_alpha(c_foreground, 0.25f), 10);
        }

    const float hx = bb.Min.x + bb.GetWidth() * smooth;
    const float hw = px(6.f) * grow;
    const float hh = px(20.f) * grow;
    dl->AddRectFilled(ImVec2(hx - hw * 0.5f, bb.GetCenter().y - hh * 0.5f),
                      ImVec2(hx + hw * 0.5f, bb.GetCenter().y + hh * 0.5f), c_foreground, px(3.f));

    ImGui::PopID();
    return changed;
}

void toast(const char* title, const char* description, toast_status status)
{
    toast_entry e;
    e.title = title ? title : "";
    e.description = description ? description : "";
    e.status = status;

    toast_list().push_back(e);
}

void toasts_draw(const ImRect& area)
{
    std::vector<toast_entry>& list = toast_list();
    if (list.empty())
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float dt = ImGui::GetIO().DeltaTime;

    const float w = ImMin(area.GetWidth() - px(32.f), px(max_w_sm));
    const float x = area.Max.x - px(sp_4) - w;
    const float bottom = area.Max.y - px(sp_6);

    for (size_t i = 0; i < list.size(); i++)
    {
        toast_entry& t = list[i];
        t.age += dt;

        const int from_end = (int)(list.size() - 1 - i);
        if (!t.leaving && (t.age > k_toast_life || from_end >= k_toast_visible))
            t.leaving = true;
        if (t.leaving)
            t.leave_t += dt;
    }

    for (size_t i = 0; i < list.size();)
    {
        if (list[i].leaving && list[i].leave_t > k_toast_exit)
            list.erase(list.begin() + (ptrdiff_t)i);
        else
            i++;
    }

    float cursor = bottom;
    for (size_t i = 0; i < list.size(); i++)
    {
        toast_entry& t = list[i];

        const float body =
            ImMax(px(28.f), px(20.f) + (t.description.empty() ? 0.f : px(2.f) + px(16.f)));
        const float h = px(sp_3) * 2.f + body;

        const float target_y = cursor - h;
        cursor = target_y - px(sp_2);

        if (!t.seeded)
        {

            t.y.snap(target_y + px(22.f));
            t.scale.snap(0.96f);
            t.opacity.snap(0.f);
            t.seeded = true;
        }

        const float exit_p =
            t.leaving ? mo::EASE_OUT(ImClamp(t.leave_t / k_toast_exit, 0.f, 1.f)) : 0.f;

        const float ty = t.y.to(target_y, k_stack, dt);
        const float ts =
            t.leaving ? mo::lerp(t.scale.value, 0.96f, exit_p) : t.scale.to(1.f, k_stack, dt);
        const float to = t.leaving ? mo::lerp(1.f, 0.f, exit_p) : t.opacity.to(1.f, k_stack, dt);

        const float dx = t.leaving ? px(32.f) * exit_p : 0.f;
        const float blur = t.leaving ? px(8.f) * exit_p : px(10.f) * (1.f - ImClamp(to, 0.f, 1.f));

        if (to <= 0.004f)
            continue;

        const ImVec2 c(x + w * 0.5f + dx, ty + h * 0.5f);
        const ImRect r(ImVec2(c.x - w * 0.5f * ts, c.y - h * 0.5f * ts),
                       ImVec2(c.x + w * 0.5f * ts, c.y + h * 0.5f * ts));

        backdrop_blur(dl, r, px(24.f), px(16.f), to);
        dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_card, 0.95f * to), px(16.f));
        dl->AddRect(ImVec2(r.Min.x + px(0.5f), r.Min.y + px(0.5f)),
                    ImVec2(r.Max.x - px(0.5f), r.Max.y - px(0.5f)), mo::with_alpha(c_border, to),
                    px(16.f), px(1.f), ImDrawFlags_None);

        const float chip = px(28.f) * ts;
        const ImVec2 ci(r.Min.x + px(sp_3) * ts, r.Min.y + (px(sp_3) + px(2.f)) * ts);
        dl->AddRectFilled(ci, ImVec2(ci.x + chip, ci.y + chip),
                          mo::with_alpha(status_tint(t.status), to), chip * 0.5f);

        const float glyph = px(14.f) * ts;
        if (t.status == toast_loading)
            icons::loader(dl, ImVec2(ci.x + chip * 0.5f, ci.y + chip * 0.5f), glyph,
                          mo::with_alpha(status_colour(t.status), to), t.age * 2.f * IM_PI);
        else
            icons::draw(status_icon(t.status), dl,
                        ImVec2(ci.x + (chip - glyph) * 0.5f, ci.y + (chip - glyph) * 0.5f), glyph,
                        mo::with_alpha(status_colour(t.status), to));

        const float tx = ci.x + chip + px(sp_3) * ts;
        const float text_w = r.Max.x - px(sp_3) * ts - px(28.f) * ts - px(sp_3) * ts - tx;

        dl->PushClipRect(ImVec2(tx, r.Min.y), ImVec2(ImMax(tx, tx + text_w), r.Max.y), true);

        ImFont* tf = font_medium(text_sm);
        draw_text_blur(dl, tf, ImVec2(tx, r.Min.y + px(sp_3) * ts + line_top(tf, px(20.f))),
                       mo::with_alpha(c_foreground, to), t.title.c_str(), blur);

        if (!t.description.empty())
        {
            ImFont* df = font_regular(text_xs);
            draw_text_blur(
                dl, df,
                ImVec2(tx, r.Min.y + (px(sp_3) + px(20.f) + px(2.f)) * ts + line_top(df, px(16.f))),
                mo::with_alpha(c_muted_foreground, to), t.description.c_str(), blur);
        }
        dl->PopClipRect();

        const float close = px(28.f) * ts;
        const ImRect cb(
            ImVec2(r.Max.x - px(sp_3) * ts - close, r.Min.y + (px(sp_3) + px(2.f)) * ts),
            ImVec2(r.Max.x - px(sp_3) * ts, r.Min.y + (px(sp_3) + px(2.f)) * ts + close));

        ImGui::PushID((int)(1300 + i));
        const ImGuiID cid = ImGui::GetCurrentWindow()->GetID("x");
        ImGui::SetCursorScreenPos(cb.Min);
        ImGui::ItemSize(ImVec2(0, 0));
        ImGui::ItemAdd(cb, cid);
        bool ch = false, cheld = false;
        const bool cp = ImGui::ButtonBehavior(cb, cid, &ch, &cheld);
        ImGui::PopID();

        if (ch)
            dl->AddRectFilled(cb.Min, cb.Max, mo::with_alpha(c_primary, 0.06f * to), close * 0.5f);
        icons::draw(icons::id::cross, dl,
                    ImVec2(cb.GetCenter().x - glyph * 0.5f, cb.GetCenter().y - glyph * 0.5f), glyph,
                    mo::with_alpha(ch ? c_foreground : c_muted_foreground, to));

        if (cp && !t.leaving)
        {
            t.leaving = true;
            t.leave_t = 0.f;
        }
    }
}

namespace
{
bool g_overlay_open = false;
}

bool overlay_open()
{
    return g_overlay_open;
}

void flush_overlays()
{
    std::vector<overlay>& queue = overlays();
    if (queue.empty())
    {
        g_overlay_open = false;
        return;
    }

    g_overlay_open = false;
    for (size_t i = 0; i < queue.size(); i++)
        if (queue[i].state && queue[i].state->open)
            g_overlay_open = true;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        for (size_t i = 0; i < queue.size(); i++)
            if (queue[i].state)
                queue[i].state->open = false;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (overlay& ov : queue)
    {
        select_state* st = ov.state;
        const int count = ov.option_count;

        const float row = px(30.f);
        const float pad = px(4.f);
        const float h = pad * 2.f + row * (float)count;

        const bool exiting = st->panel.exiting;
        const float p =
            mo::EASE_OUT(ImClamp((exiting ? st->panel.out : st->panel.in) / 0.18f, 0.f, 1.f));
        const float open = exiting ? 1.f - p : p;

        const ImRect panel(ImVec2(ov.trigger.Min.x, ov.trigger.Max.y + px(6.f)),
                           ImVec2(ov.trigger.Min.x + ov.width, ov.trigger.Max.y + px(6.f) + h));

        dl->AddRectFilled(panel.Min, panel.Max, mo::with_alpha(c_background, open), px(12.f));
        dl->AddRect(ImVec2(panel.Min.x + px(0.5f), panel.Min.y + px(0.5f)),
                    ImVec2(panel.Max.x - px(0.5f), panel.Max.y - px(0.5f)),
                    mo::with_alpha(c_border, open), px(12.f), px(1.f), ImDrawFlags_None);

        ImFont* f = font_regular(text_sm);
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        for (int i = 0; i < count; i++)
        {
            const float delay = k_list_delay + (float)i * k_list_stagger;
            const float ip = mo::EASE_OUT(ImClamp(
                ((exiting ? st->panel.out : st->panel.in) - delay) / k_item_move, 0.f, 1.f));
            const float item_o = (exiting ? 1.f - ip : ip) * open;
            const float dy = -6.f * (exiting ? ip : 1.f - ip);
            const float blur = k_item_blur * (exiting ? ip : 1.f - ip);

            const ImRect r(
                ImVec2(panel.Min.x + pad, panel.Min.y + pad + row * (float)i + px(dy)),
                ImVec2(panel.Max.x - pad, panel.Min.y + pad + row * (float)(i + 1) + px(dy)));

            const bool hot = !exiting && r.Contains(mouse);
            if (hot)
            {
                dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_card, item_o), px(8.f));

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    *ov.value = i;
                    st->open = false;
                }
            }

            const ImU32 col =
                mo::with_alpha(hot || *ov.value == i ? c_foreground : c_muted_foreground, item_o);
            if (blur > 0.25f)
                draw_text_blur(dl, f,
                               ImVec2(r.Min.x + px(10.f), r.GetCenter().y - f->LegacySize * 0.5f),
                               col, ov.options[i], px(blur));
            else
                draw_text(dl, f, ImVec2(r.Min.x + px(10.f), r.GetCenter().y - f->LegacySize * 0.5f),
                          col, ov.options[i]);

            if (*ov.value == i)
                icons::draw(icons::id::check, dl,
                            ImVec2(r.Max.x - px(10.f) - px(14.f), r.GetCenter().y - px(7.f)),
                            px(14.f), col);
        }

        if (!exiting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !panel.Contains(mouse) &&
            !ov.trigger.Contains(mouse))
            st->open = false;
    }

    queue.clear();
}
} // namespace solace
