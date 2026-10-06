#include "ui/controls/form_controls.h"

#include "ui/foundation/icons.h"
#include "ui/foundation/runtime.h"
#include "ui/foundation/theme.h"

#include "imgui_internal.h"

#include <cmath>

namespace solace
{
bool link_span(const char* id, ImDrawList* dl, ImFont* f, const ImVec2& pos, float width, ImU32 col,
               float alpha)
{
    if (!f || width <= 0.f)
        return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImRect bb(ImVec2(pos.x, pos.y - px(2.f)),
                    ImVec2(pos.x + width, pos.y + f->LegacySize + px(4.f)));

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("a");
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    ImGui::PopID();

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const float baseline = pos.y + f->LegacySize * (1005.f / 1300.f);
    const float uy = ImFloor(baseline + px(4.f) + 0.5f);

    ImGuiStorage* store = ImGui::GetStateStorage();
    float t = store->GetFloat(item_id, 0.f);
    t += ((hovered ? 1.f : 0.f) - t) * ImMin(1.f, ImGui::GetIO().DeltaTime * 16.f);
    store->SetFloat(item_id, t);

    dl->AddRectFilled(ImVec2(pos.x, uy), ImVec2(pos.x + width, uy + px(1.f)),
                      mo::with_alpha(col, 0.55f * alpha));
    if (t > 0.004f)
        dl->AddRectFilled(ImVec2(pos.x, uy), ImVec2(pos.x + width * mo::EASE_OUT(t), uy + px(1.f)),
                          mo::with_alpha(col, alpha));

    return pressed;
}

bool link(const char* id, ImDrawList* dl, ImFont* f, const ImVec2& pos, const char* text, ImU32 col,
          float alpha, bool underline)
{
    if (!f || !text || !*text)
        return false;

    const float w = text_width(f, text);
    draw_text(dl, f, pos, mo::with_alpha(col, alpha), text);

    if (underline)
        return link_span(id, dl, f, pos, w, col, alpha);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImRect bb(ImVec2(pos.x, pos.y - px(2.f)),
                    ImVec2(pos.x + w, pos.y + f->LegacySize + px(4.f)));

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("a");
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    ImGui::PopID();

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    return pressed;
}

namespace
{

void draw_ring(ImDrawList* dl, const ImRect& box, float rounding, ImU32 col, float width)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    const float half = width * 0.5f;
    dl->AddRect(ImVec2(box.Min.x - half, box.Min.y - half),
                ImVec2(box.Max.x + half, box.Max.y + half), col, rounding + half, width,
                ImDrawFlags_None);
}

void draw_left_icon(ImDrawList* dl, icon_id id, const ImVec2& tl, float box, ImU32 col)
{
    switch (id)
    {
    case icon_user:
        icons::user(dl, tl, box, col);
        break;
    case icon_mail:
        icons::mail(dl, tl, box, col);
        break;
    case icon_lock:
        icons::lock(dl, tl, box, col);
        break;
    default:
        break;
    }
}

constexpr float k_shake[] = {0.f, -6.f, 6.f, -4.f, 4.f, -2.f, 0.f};
constexpr float k_shake_duration = 0.45f;

constexpr float k_error_duration = 0.2f;
constexpr float k_error_blur = 4.f;
constexpr float k_error_shift = 4.f;
} // namespace

void input_update(input_state& st, const input_desc& d, float dt)
{
    const bool has_error = d.error != nullptr && d.error[0] != '\0';

    if (has_error && !st.had_error)
        st.shake = 0.f;
    st.had_error = has_error;

    if (st.shake < k_shake_duration)
        st.shake += dt;

    if (has_error)
        st.error_text = d.error;

    st.error_presence.update(has_error, dt, k_error_duration);

    if (d.success && !st.success_mounted)
        st.success_draw.reset();
    st.success_mounted = d.success;
    if (st.success_mounted)
        st.success_draw.advance(dt);
}

float input_height(const input_state& st)
{

    float h = leading_sm + sp_1_5 + sp_11;
    if (st.error_presence.mounted)
        h += sp_1_5 + leading_xs;
    return px(h);
}

bool input_draw(const char* id, input_state& st, const input_desc& d, const ImVec2& pos,
                float width, bool* out_blurred)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    if (out_blurred)
        *out_blurred = false;

    ImGui::PushID(id);

    const bool has_error = d.error != nullptr && d.error[0] != '\0';
    const float alpha = d.disabled ? 0.6f : 1.f;

    if (d.label)
    {
        ImFont* f = font_medium(text_sm);
        draw_text(dl, f, ImVec2(pos.x + px(sp_1), pos.y + line_top(f, px(leading_sm))),
                  mo::with_alpha(c_foreground, alpha), d.label);
    }

    const float shake = st.shake < k_shake_duration
                            ? mo::keyframes(k_shake, IM_ARRAYSIZE(k_shake), st.shake,
                                            k_shake_duration, mo::EASE_IN_OUT_NAMED)
                            : 0.f;

    const float field_top = pos.y + px(leading_sm + sp_1_5);
    const ImRect field(ImVec2(pos.x + px(shake), field_top),
                       ImVec2(pos.x + px(shake) + width, field_top + px(sp_11)));
    const float rounding = field.GetHeight() * 0.5f;

    ImU32 border_target = c_border;
    ImU32 ring_target = IM_COL32(0, 0, 0, 0);
    if (has_error)
    {
        border_target = c_destructive;
        ring_target = mo::with_alpha(c_destructive, 0.25f);
    }
    else if (st.focused)
    {
        border_target = mo::with_alpha(c_foreground, 0.4f);
        ring_target = mo::with_alpha(c_border_strong, 0.4f);
    }

    const ImU32 border_col = st.border_col.update(border_target, ImGui::GetIO().DeltaTime, 0.2f);
    const ImU32 ring_col = st.ring_col.update(ring_target, ImGui::GetIO().DeltaTime, 0.2f);

    draw_ring(dl, field, rounding, mo::with_alpha(ring_col, alpha), px(2.f));
    dl->AddRect(field.Min, field.Max, mo::with_alpha(border_col, alpha), rounding, px(1.f),
                ImDrawFlags_None);

    const float border_w = px(1.f);
    const ImRect inner(ImVec2(field.Min.x + border_w, field.Min.y + border_w),
                       ImVec2(field.Max.x - border_w, field.Max.y - border_w));

    const float icon_box = px(16.f);
    if (d.left != icon_none)
        draw_left_icon(dl, d.left,
                       ImVec2(inner.Min.x + px(sp_3), field.GetCenter().y - icon_box * 0.5f),
                       icon_box, mo::with_alpha(c_muted_foreground, alpha));

    const bool right_slot = d.reveal_toggle && !d.success;
    const float pad_left = border_w + px(d.left != icon_none ? sp_10 : sp_3_5);

    ImFont* text_font = font_regular(text_base);
    const float item_width = width - (right_slot ? px(sp_11) : 0.f);

    ui_runtime::push_font(text_font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(pad_left, (field.GetHeight() - text_font->LegacySize) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleColor(ImGuiCol_Text, mo::with_alpha(c_foreground, alpha));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, mo::with_alpha(c_muted_foreground, 0.6f * alpha));

    ImGui::SetCursorScreenPos(field.Min);
    ImGui::SetNextItemWidth(item_width);

    ImGuiInputTextFlags flags = 0;
    const bool mask = d.mask && !(d.reveal && *d.reveal);
    if (mask)
        flags |= ImGuiInputTextFlags_Password;
    if (d.disabled)
        flags |= ImGuiInputTextFlags_ReadOnly;

    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, IM_COL32_BLACK_TRANS);
    const bool changed = ImGui::InputText("##field", d.buf, (size_t)d.buf_size, flags);
    ImGui::PopStyleColor();

    const bool was_focused = st.focused;
    st.focused = ImGui::IsItemActive();
    if (was_focused && !st.focused && out_blurred)
        *out_blurred = true;

    draw_caret(dl, st.caret, field, px(leading_sm), st.focused && !d.disabled, c_foreground, alpha);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ui_runtime::pop_font();

    if (d.buf[0] == 0 && d.placeholder)
        draw_text(
            dl, text_font,
            ImVec2(field.Min.x + pad_left, field.GetCenter().y - text_font->LegacySize * 0.5f),
            mo::with_alpha(c_muted_foreground, 0.6f * alpha), d.placeholder);

    if (d.success)
    {

        const float box = px(20.f);
        const float fraction = mo::EASE_OUT_NAMED(st.success_draw.progress(0.35f));
        icons::stroke_path(dl, "M5 12.5l4.5 4.5L19 7.5",
                           ImVec2(inner.Max.x - px(sp_3_5) - box, field.GetCenter().y - box * 0.5f),
                           box, mo::with_alpha(c_success, alpha), 2.5f, fraction);
    }
    else if (right_slot)
    {

        const ImRect btn(ImVec2(inner.Max.x - px(sp_11), field.Min.y),
                         ImVec2(inner.Max.x, field.Max.y));
        const ImGuiID btn_id = window->GetID("reveal");

        ImGui::SetCursorScreenPos(btn.Min);
        ImGui::ItemSize(btn.GetSize());
        ImGui::ItemAdd(btn, btn_id);

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(btn, btn_id, &hovered, &held);
        if (pressed && !d.disabled && d.reveal)
        {
            *d.reveal = !*d.reveal;
            st.reveal_pop = 0.f;
        }

        const float dt = ImGui::GetIO().DeltaTime;
        const bool on = d.reveal && *d.reveal;

        st.reveal_pop += dt;
        const float pop = ImClamp(st.reveal_pop / 0.18f, 0.f, 1.f);
        const float pulse = sinf(pop * IM_PI);

        const float t = ImClamp(st.reveal_t.to(on ? 1.f : 0.f, mo::SPRING_SWAP, dt), 0.f, 1.f);
        const float press_t = st.reveal_scale.to(held ? 0.90f : 1.f, mo::SPRING_PRESS, dt);
        const float hover_t = st.reveal_hover.to(hovered ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);
        const float scale = press_t * (1.f + 0.06f * hover_t) * (1.f - 0.08f * pulse);

        if (hover_t > 0.004f)
            dl->AddCircleFilled(btn.GetCenter(), px(16.f),
                                mo::with_alpha(c_foreground, 0.055f * hover_t * alpha));
        if (pulse > 0.004f)
            dl->AddCircleFilled(btn.GetCenter(), px(13.f + 2.f * pulse),
                                mo::with_alpha(c_foreground, 0.025f * pulse * alpha));

        const float box = icon_box * scale;
        const ImVec2 icon_tl(btn.GetCenter().x - box * 0.5f, btn.GetCenter().y - box * 0.5f);
        const ImU32 base = mo::mix(c_muted_foreground, c_foreground, hover_t);
        const int icon_vtx_begin = dl->VtxBuffer.Size;

        // Swap paths only while the eye is almost closed. Drawing one path at
        // a time avoids the muddy double-outline produced by a crossfade.
        if (t < 0.5f)
            icons::eye(dl, icon_tl, box, mo::with_alpha(base, alpha));
        else
            icons::eye_off(dl, icon_tl, box, mo::with_alpha(base, alpha));

        // Keep a quarter-height lid at the midpoint so the 16px stroke stays
        // crisp instead of collapsing into sub-pixel fragments.
        const float blink_y = 0.25f + 0.75f * ImFabs(2.f * t - 1.f);
        const ImVec2 center = btn.GetCenter();
        for (int i = icon_vtx_begin; i < dl->VtxBuffer.Size; i++)
        {
            ImDrawVert& vertex = dl->VtxBuffer[i];
            vertex.pos.y = center.y + (vertex.pos.y - center.y) * blink_y;
        }
    }

    if (st.error_presence.mounted && !st.error_text.empty())
    {
        const bool exiting = st.error_presence.exiting;
        const float p = mo::EASE_OUT_NAMED(ImClamp(
            (exiting ? st.error_presence.out : st.error_presence.in) / k_error_duration, 0.f, 1.f));
        const float k = exiting ? p : 1.f - p;

        const float opacity = exiting ? 1.f - p : p;
        const float shift = -k_error_shift * k;
        const float blur = k_error_blur * k;

        ImFont* f = font_regular(text_xs);
        const ImVec2 at(pos.x + px(sp_1),
                        field_top + px(sp_11 + sp_1_5) + line_top(f, px(leading_xs)) + px(shift));

        draw_text_blur(dl, f, at, mo::with_alpha(c_destructive, opacity * alpha),
                       st.error_text.c_str(), px(blur));
    }

    ImGui::PopID();
    return changed;
}

namespace
{
constexpr float k_mark_duration = 0.16f;
constexpr float k_mark_draw_duration = 0.3f;
constexpr float k_mark_draw_delay = 0.04f;
constexpr float k_mark_exit_blur = 4.f;
} // namespace

void checkbox_update(checkbox_state& st, bool checked, float dt)
{
    const bool initial_checked = checked && !st.mark.mounted && st.mark.skip_enter;
    const bool was = st.mark.mounted && !st.mark.exiting;
    st.mark.update(checked, dt, k_mark_duration);

    if (checked && !was)
    {
        st.mark_draw.reset();
        if (initial_checked)
            st.mark_draw.advance(k_mark_draw_delay + k_mark_draw_duration);
    }
    if (checked)
        st.mark_draw.advance(dt);
}

bool checkbox_draw(const char* id, checkbox_state& st, bool* checked, const char* label,
                   const ImVec2& pos, bool disabled)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("box");

    ImFont* label_font = font_regular(text_sm);
    const float label_w = label ? text_width(label_font, label) : 0.f;
    const float box_size = px(20.f);

    const ImRect bb(
        pos, ImVec2(pos.x + box_size + (label ? px(sp_3) + label_w : 0.f), pos.y + px(leading_sm)));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    if (pressed && !disabled)
        *checked = !*checked;

    const float alpha = disabled ? 0.6f : 1.f;
    const bool show_mark = *checked;

    const float scale = st.press.to((held && !disabled) ? 0.92f : 1.f, mo::SPRING_PRESS, dt);

    const ImU32 fill_target = show_mark ? c_primary : c_background;
    const ImU32 border_target =
        show_mark ? c_primary
                  : mo::with_alpha(c_muted_foreground, (hovered && !disabled) ? 1.f : 0.5f);

    const ImU32 fill = st.fill.update(fill_target, dt, 0.2f);
    const ImU32 border = st.border.update(border_target, dt, 0.2f);

    const ImVec2 centre(pos.x + box_size * 0.5f, pos.y + px(leading_sm) * 0.5f);
    const float half = box_size * 0.5f * scale;
    const ImRect box(ImVec2(centre.x - half, centre.y - half),
                     ImVec2(centre.x + half, centre.y + half));
    const float rounding = px(rounded_md) * scale;
    const float border_w = px(2.f) * scale;

    draw_border(dl, box, rounding, border_w, mo::with_alpha(border, alpha),
                mo::with_alpha(fill, alpha));

    if (st.mark.mounted)
    {
        const bool exiting = st.mark.exiting;
        const float p =
            mo::EASE_OUT(ImClamp((exiting ? st.mark.out : st.mark.in) / k_mark_duration, 0.f, 1.f));

        const float opacity = exiting ? 1.f - p : p;
        const float mark_scale = exiting ? mo::lerp(1.f, 0.5f, p) : mo::lerp(0.5f, 1.f, p);
        const float blur = exiting ? k_mark_exit_blur * p : 0.f;

        const float mark_box = px(12.f) * mark_scale * scale;
        const ImVec2 tl(centre.x - mark_box * 0.5f, centre.y - mark_box * 0.5f);

        const float fraction =
            exiting ? 1.f
                    : mo::EASE_OUT(st.mark_draw.progress(k_mark_draw_duration, k_mark_draw_delay));

        const ImU32 col = mo::with_alpha(c_primary_foreground, opacity * alpha);

        if (blur > 0.25f)
        {

            for (int i = 0; i < 5; i++)
            {
                const float a = (float)i / 5.f * 2.f * IM_PI;
                icons::stroke_path(
                    dl, "M5 13l4 4L19 7",
                    ImVec2(tl.x + ImCos(a) * px(blur) * 0.5f, tl.y + ImSin(a) * px(blur) * 0.5f),
                    mark_box, mo::with_alpha(col, 0.34f), 3.f, fraction);
            }
        }
        else
        {
            icons::stroke_path(dl, "M5 13l4 4L19 7", tl, mark_box, col, 3.f, fraction);
        }
    }

    if (label)
        draw_text(dl, label_font,
                  ImVec2(pos.x + box_size + px(sp_3), pos.y + line_top(label_font, px(leading_sm))),
                  mo::with_alpha(c_foreground, alpha), label);

    ImGui::PopID();
    return pressed;
}

namespace
{
constexpr float k_icon_slot_width = 24.f;
constexpr float k_icon_size = 16.f;
constexpr float k_slot_exit = 0.16f;
constexpr float k_roll_blur = 6.f;
constexpr float k_cascade_stagger = 0.025f;
} // namespace

static void icon_slot_update(icon_slot& slot, bool want, float dt)
{
    const bool exiting_before = slot.pres.exiting;
    slot.pres.update(want, dt, k_slot_exit);

    if (slot.pres.exiting && !exiting_before)
    {

        slot.exit_width = slot.width.value;
        slot.exit_scale = slot.scale.value;
        slot.exit_opacity = slot.opacity.value;
        slot.exit_blur = slot.blur.value;
        slot.exit_captured = true;
    }

    if (!slot.pres.mounted)
    {
        slot.width.snap(0.f);
        slot.scale.snap(0.7f);
        slot.opacity.snap(0.f);
        slot.blur.snap(k_roll_blur);
        slot.exit_captured = false;
        return;
    }

    if (slot.pres.exiting)
    {
        const float p = mo::EASE_OUT(ImClamp(slot.pres.out / k_slot_exit, 0.f, 1.f));
        slot.width.snap(mo::lerp(slot.exit_width, 0.f, p));
        slot.scale.snap(mo::lerp(slot.exit_scale, 0.7f, p));
        slot.opacity.snap(mo::lerp(slot.exit_opacity, 0.f, p));
        slot.blur.snap(mo::lerp(slot.exit_blur, k_roll_blur, p));
        return;
    }

    if (slot.pres.in <= 0.f && !slot.width.seeded)
    {
        slot.width.snap(0.f);
        slot.scale.snap(0.7f);
        slot.opacity.snap(0.f);
        slot.blur.snap(k_roll_blur);
    }

    slot.width.to(k_icon_slot_width, mo::SPRING_SWAP, dt);
    slot.scale.to(1.f, mo::SPRING_SWAP, dt);
    slot.opacity.to(1.f, mo::SPRING_SWAP, dt);
    slot.blur.to(0.f, mo::SPRING_SWAP, dt);
}

void stateful_button_update(stateful_button_state& st, button_state state, const char* label,
                            float dt)
{
    icon_slot_update(st.loading, state == btn_loading, dt);
    icon_slot_update(st.success, state == btn_success, dt);
    icon_slot_update(st.error, state == btn_error, dt);

    st.spin += dt * 2.f * IM_PI;
    if (st.spin > 2.f * IM_PI)
        st.spin -= 2.f * IM_PI;

    const std::string next = label ? label : "";
    if (next != st.last_label || st.last_state != (int)state)
    {
        const bool initial_label = st.last_state < 0;

        if (st.layers[0].live)
        {
            st.layers[1] = st.layers[0];
            st.layers[1].exiting = true;
            st.layers[1].t = 0.f;
        }

        st.layers[0] = cascade_layer();
        st.layers[0].text = next;
        st.layers[0].live = true;
        st.layers[0].t = 0.f;

        if (initial_label)
        {
            for (int i = 0; i < 64; i++)
                st.layers[0].letter[i].snap(1.f);

            // The update loop below honors the per-letter stagger. Seed the
            // first label past it so it cannot collapse back to one glyph.
            const int count = (int)ImMin(next.size(), (size_t)64);
            st.layers[0].t = (float)count * k_cascade_stagger;
        }

        st.last_label = next;
        st.last_state = (int)state;
    }

    for (cascade_layer& layer : st.layers)
    {
        if (!layer.live)
            continue;

        layer.t += dt;

        if (layer.exiting)
        {
            if (layer.t >= k_slot_exit + (float)layer.text.size() * k_cascade_stagger * 0.5f)
                layer.live = false;
        }
        else
        {
            const int count = (int)ImMin(layer.text.size(), (size_t)64);
            for (int i = 0; i < count; i++)
            {
                if (layer.t < (float)i * k_cascade_stagger)
                {
                    layer.letter[i].snap(0.f);
                    continue;
                }
                layer.letter[i].to(1.f, mo::SPRING_SWAP, dt);
            }
        }
    }
}

bool stateful_button_draw(const char* id, stateful_button_state& st, button_state state,
                          const ImVec2& pos, float width, bool disabled)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float dt = ImGui::GetIO().DeltaTime;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("button");

    const float height = px(sp_12);
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    ImGui::ItemAdd(bb, item_id);

    const bool interactive = !disabled && state != btn_loading;

    bool hovered = false, held = false;
    bool pressed = false;
    if (interactive)
        pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);

    const float scale = st.press.to(held ? 0.93f : 1.f, mo::SPRING_PRESS, dt);

    const ImU32 bg_target = hovered ? mo::mix(c_background, c_primary, 0.9f) : c_primary;
    const ImU32 bg = st.background.update(bg_target, dt, 0.15f);

    const float alpha = (disabled || state == btn_loading) ? 0.5f : 1.f;

    const ImVec2 centre = bb.GetCenter();
    const ImVec2 half(bb.GetWidth() * 0.5f * scale, bb.GetHeight() * 0.5f * scale);
    const ImRect surface(ImVec2(centre.x - half.x, centre.y - half.y),
                         ImVec2(centre.x + half.x, centre.y + half.y));

    dl->AddRectFilled(surface.Min, surface.Max, mo::with_alpha(bg, alpha),
                      surface.GetHeight() * 0.5f);

    ImFont* f = font_medium(text_base);

    float text_target = 0.f;
    if (st.layers[0].live)
        for (size_t i = 0; i < st.layers[0].text.size(); i++)
            text_target +=
                f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0.f, st.layers[0].text.c_str() + i,
                                 st.layers[0].text.c_str() + i + 1)
                    .x;

    const float text_w = st.text_width.to(text_target, mo::SPRING_SWAP, dt);

    const float icons_w =
        px(st.loading.width.value + st.success.width.value + st.error.width.value) * scale;
    const float content_w = icons_w + text_w * scale;
    const float line_h = px(leading_base) * scale;

    float x = centre.x - content_w * 0.5f;
    const float top = centre.y - line_h * 0.5f;

    const ImU32 fg = mo::with_alpha(c_primary_foreground, alpha);

    struct slot_ref
    {
        icon_slot* slot;
        int kind;
    };
    const slot_ref slots[] = {{&st.loading, 0}, {&st.success, 1}, {&st.error, 2}};

    for (const slot_ref& s : slots)
    {
        if (!s.slot->pres.mounted)
            continue;

        const float w = px(s.slot->width.value) * scale;
        if (w <= 0.01f)
            continue;

        dl->PushClipRect(ImVec2(x, top), ImVec2(x + w, top + line_h), true);

        const float box = px(k_icon_size) * s.slot->scale.value * scale;
        const ImVec2 icon_centre(x + w * 0.5f, centre.y);
        const ImVec2 tl(icon_centre.x - box * 0.5f, icon_centre.y - box * 0.5f);
        const ImU32 col = mo::with_alpha(fg, s.slot->opacity.value);

        if (s.kind == 0)
            icons::loader(dl, icon_centre, box, col, st.spin);
        else if (s.kind == 1)
            icons::check(dl, tl, box, col);
        else
            icons::cross(dl, tl, box, col);

        dl->PopClipRect();
        x += w;
    }

    {
        const float w = text_w * scale;
        const float glyph_top = (line_h - f->LegacySize * scale) * 0.5f;
        dl->PushClipRect(ImVec2(x, top), ImVec2(x + w, top + line_h), true);

        for (const cascade_layer& layer : st.layers)
        {
            if (!layer.live || layer.text.empty())
                continue;

            const int count = (int)ImMin(layer.text.size(), (size_t)64);
            float lx = x;

            for (int i = 0; i < count; i++)
            {
                const char* ch = layer.text.c_str() + i;
                const float advance =
                    f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0.f, ch, ch + 1).x * scale;

                float opacity, offset, blur;
                if (layer.exiting)
                {

                    const float delay = (float)i * k_cascade_stagger * 0.5f;
                    const float p =
                        mo::EASE_OUT(ImClamp((layer.t - delay) / k_slot_exit, 0.f, 1.f));
                    opacity = 1.f - p;
                    offset = -1.05f * line_h * p;
                    blur = k_roll_blur * p;
                }
                else
                {
                    const float p = layer.letter[i].value;
                    opacity = p;
                    offset = 1.05f * line_h * (1.f - p);
                    blur = k_roll_blur * (1.f - p);
                }

                if (opacity > 0.004f)
                {
                    char glyph[2] = {*ch, '\0'};
                    draw_text_blur(dl, f, ImVec2(lx, top + glyph_top + offset),
                                   mo::with_alpha(fg, opacity), glyph, px(blur) * scale);
                }

                lx += advance;
            }
        }

        dl->PopClipRect();
    }

    ImGui::PopID();
    return pressed;
}
} // namespace solace
