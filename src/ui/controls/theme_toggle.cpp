#include "ui/controls/theme_toggle.h"
#include "graphics/snapshot.h"
#include "ui/foundation/icons.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/theme.h"

#include <algorithm>

namespace solace
{
namespace
{

constexpr float k_swap_duration = 0.2f;
constexpr float k_swap_scale = 0.25f;
constexpr float k_swap_blur = 8.f;

constexpr float k_rect_duration = 0.4f;
constexpr float k_circle_duration = 0.7f;

enum phase
{
    phase_idle = 0,
    phase_awaiting,
    phase_revealing
};

struct toggle_state
{
    phase stage = phase_idle;
    float t = 0.f;
    bool target_dark = true;
    theme_variant variant = theme_rectangle;
    rect_start start = start_bottom_up;

    bool was_dark = true;
    bool has_previous = false;
    float swap_t = 1e6f;

    mo::spring press;
};

toggle_state& state()
{
    static toggle_state s;
    return s;
}

void draw_icon(ImDrawList* dl, icons::id which, const ImVec2& centre, float box, ImU32 col,
               float scale, float blur)
{
    const float size = box * scale;
    const ImVec2 tl(centre.x - size * 0.5f, centre.y - size * 0.5f);

    if (blur < 0.25f)
    {
        icons::draw(which, dl, tl, size, col);
        return;
    }

    for_each_blur_tap(blur, col, [&](const ImVec2& off, ImU32 tap)
                      { icons::draw(which, dl, ImVec2(tl.x + off.x, tl.y + off.y), size, tap); });
}

void rect_from(rect_start start, float out[4])
{
    switch (start)
    {
    case start_top_left:
        out[0] = 0.f;
        out[1] = 1.f;
        out[2] = 1.f;
        out[3] = 0.f;
        break;
    case start_top_right:
        out[0] = 0.f;
        out[1] = 0.f;
        out[2] = 1.f;
        out[3] = 1.f;
        break;
    case start_bottom_left:
        out[0] = 1.f;
        out[1] = 1.f;
        out[2] = 0.f;
        out[3] = 0.f;
        break;
    case start_bottom_right:
        out[0] = 1.f;
        out[1] = 0.f;
        out[2] = 0.f;
        out[3] = 1.f;
        break;
    case start_center:
        out[0] = .5f;
        out[1] = .5f;
        out[2] = .5f;
        out[3] = .5f;
        break;
    default:
        out[0] = 1.f;
        out[1] = 0.f;
        out[2] = 0.f;
        out[3] = 0.f;
        break;
    }
}

ImVec2 circle_origin(rect_start start, const ImRect& r)
{
    const ImVec2 size = r.GetSize();
    switch (start)
    {
    case start_top_left:
        return r.Min;
    case start_top_right:
        return ImVec2(r.Max.x, r.Min.y);
    case start_bottom_left:
        return ImVec2(r.Min.x, r.Max.y);
    case start_bottom_right:
        return r.Max;
    case start_center:
        return r.GetCenter();
    default:
        return ImVec2(r.Min.x + size.x * 0.5f, r.Max.y);
    }
}

float exit_distance(const ImVec2& origin, const ImVec2& dir, const ImRect& r)
{
    float best = FLT_MAX;
    if (dir.x > 1e-6f)
        best = ImMin(best, (r.Max.x - origin.x) / dir.x);
    else if (dir.x < -1e-6f)
        best = ImMin(best, (r.Min.x - origin.x) / dir.x);
    if (dir.y > 1e-6f)
        best = ImMin(best, (r.Max.y - origin.y) / dir.y);
    else if (dir.y < -1e-6f)
        best = ImMin(best, (r.Min.y - origin.y) / dir.y);
    return best == FLT_MAX ? 0.f : ImMax(best, 0.f);
}

void draw_outside_circle(ImDrawList* dl, const ImRect& r, const ImVec2& origin, float radius)
{
    constexpr int k_segments = 96;

    ImVector<float> angles;
    angles.reserve(k_segments + 4);
    for (int i = 0; i < k_segments; i++)
        angles.push_back((float)i / (float)k_segments * 2.f * IM_PI);

    const ImVec2 corners[4] = {r.Min, ImVec2(r.Max.x, r.Min.y), r.Max, ImVec2(r.Min.x, r.Max.y)};
    for (int i = 0; i < 4; i++)
    {
        float a = atan2f(corners[i].y - origin.y, corners[i].x - origin.x);
        if (a < 0.f)
            a += 2.f * IM_PI;
        angles.push_back(a);
    }
    std::sort(angles.begin(), angles.end());

    const ImVec2 size = r.GetSize();
    auto uv_of = [&](const ImVec2& p)
    { return ImVec2((p.x - r.Min.x) / size.x, (p.y - r.Min.y) / size.y); };

    dl->PushTexture(ImTextureRef(snapshot::texture()));
    for (int i = 0; i < angles.Size; i++)
    {
        const float a0 = angles[i];
        const float a1 = (i + 1 < angles.Size) ? angles[i + 1] : angles[0] + 2.f * IM_PI;
        if (a1 - a0 < 1e-5f)
            continue;

        const ImVec2 d0(ImCos(a0), ImSin(a0));
        const ImVec2 d1(ImCos(a1), ImSin(a1));
        const float e0 = exit_distance(origin, d0, r);
        const float e1 = exit_distance(origin, d1, r);
        const float r0 = ImMin(radius, e0);
        const float r1 = ImMin(radius, e1);
        if (r0 >= e0 && r1 >= e1)
            continue;

        const ImVec2 p[4] = {
            ImVec2(origin.x + d0.x * r0, origin.y + d0.y * r0),
            ImVec2(origin.x + d1.x * r1, origin.y + d1.y * r1),
            ImVec2(origin.x + d1.x * e1, origin.y + d1.y * e1),
            ImVec2(origin.x + d0.x * e0, origin.y + d0.y * e0),
        };

        dl->PrimReserve(6, 4);
        for (int k = 0; k < 4; k++)
            dl->PrimWriteVtx(p[k], uv_of(p[k]), IM_COL32_WHITE);

        const ImDrawIdx base = (ImDrawIdx)(dl->_VtxCurrentIdx - 4);
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(base + 1);
        dl->PrimWriteIdx(base + 2);
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(base + 2);
        dl->PrimWriteIdx(base + 3);
    }
    dl->PopTexture();
}
} // namespace

void theme_tick(float dt)
{
    toggle_state& s = state();
    s.swap_t += dt;

    if (s.stage == phase_awaiting)
    {

        if (snapshot::ready())
        {
            set_dark(s.target_dark);
            s.stage = phase_revealing;
            s.t = 0.f;
        }
        return;
    }

    if (s.stage == phase_revealing)
    {
        s.t += dt;
        const float duration = (s.variant == theme_circle) ? k_circle_duration : k_rect_duration;
        if (s.t >= duration)
        {
            s.stage = phase_idle;
            snapshot::release();
        }
    }
}

bool theme_toggle(const char* id, const ImRect& rect, float icon_box, float alpha,
                  theme_variant variant, rect_start start)
{
    toggle_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;

    ImGui::PushID(id);
    const ImGuiID wid = window->GetID("b");
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(rect, wid);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(rect, wid, &hovered, &held);
    ImGui::PopID();

    bool started = false;
    if (pressed && s.stage == phase_idle)
    {
        s.target_dark = !is_dark();
        s.variant = variant;
        s.start = start;
        s.stage = phase_awaiting;
        snapshot::request();
        started = true;
    }

    const float scale = s.press.to(held ? 0.97f : 1.f, mo::SPRING_PRESS, dt);

    const bool dark = is_dark();
    if (dark != s.was_dark)
    {
        s.was_dark = dark;
        s.has_previous = true;
        s.swap_t = 0.f;
    }

    if (hovered)
        dl->AddRectFilled(rect.Min, rect.Max, mo::with_alpha(c_card, alpha), px(10.f));

    const ImVec2 centre = rect.GetCenter();
    const float box = px(icon_box) * scale;
    const float p = mo::EASE_IN_OUT_NAMED(ImClamp(s.swap_t / k_swap_duration, 0.f, 1.f));

    const icons::id entering = dark ? icons::id::sun : icons::id::moon;
    const icons::id leaving = dark ? icons::id::moon : icons::id::sun;
    const ImU32 col = hovered ? c_foreground : c_muted_foreground;

    if (s.has_previous && p < 1.f)
        draw_icon(dl, leaving, centre, box, mo::with_alpha(col, (1.f - p) * alpha),
                  mo::lerp(1.f, k_swap_scale, p), mo::lerp(0.f, k_swap_blur, p));

    draw_icon(dl, entering, centre, box, mo::with_alpha(col, (s.has_previous ? p : 1.f) * alpha),
              s.has_previous ? mo::lerp(k_swap_scale, 1.f, p) : 1.f,
              s.has_previous ? mo::lerp(k_swap_blur, 0.f, p) : 0.f);

    return started;
}

void theme_reveal_draw(ImDrawList* dl, const ImRect& r)
{
    const toggle_state& s = state();
    if (s.stage != phase_revealing || !snapshot::ready())
        return;

    const ImVec2 size = r.GetSize();
    if (size.x <= 0.f || size.y <= 0.f)
        return;

    if (s.variant == theme_circle)
    {

        const float t = mo::EASE_STANDARD(ImClamp(s.t / k_circle_duration, 0.f, 1.f));
        const float full = 1.5f * ImSqrt(size.x * size.x + size.y * size.y) / 1.41421356f;
        draw_outside_circle(dl, r, circle_origin(s.start, r), full * t);
        return;
    }

    const float t = mo::EASE_OUT_NAMED(ImClamp(s.t / k_rect_duration, 0.f, 1.f));

    float from[4];
    rect_from(s.start, from);

    const float top = r.Min.y + size.y * mo::lerp(from[0], 0.f, t);
    const float right = r.Max.x - size.x * mo::lerp(from[1], 0.f, t);
    const float bottom = r.Max.y - size.y * mo::lerp(from[2], 0.f, t);
    const float left = r.Min.x + size.x * mo::lerp(from[3], 0.f, t);

    const ImRect bands[4] = {
        ImRect(r.Min.x, r.Min.y, r.Max.x, top),
        ImRect(r.Min.x, bottom, r.Max.x, r.Max.y),
        ImRect(r.Min.x, top, left, bottom),
        ImRect(right, top, r.Max.x, bottom),
    };

    for (int i = 0; i < 4; i++)
    {
        const ImRect& b = bands[i];
        if (b.Max.x - b.Min.x <= 0.f || b.Max.y - b.Min.y <= 0.f)
            continue;

        dl->AddImage(ImTextureRef(snapshot::texture()), b.Min, b.Max,
                     ImVec2((b.Min.x - r.Min.x) / size.x, (b.Min.y - r.Min.y) / size.y),
                     ImVec2((b.Max.x - r.Min.x) / size.x, (b.Max.y - r.Min.y) / size.y));
    }
}
} // namespace solace
