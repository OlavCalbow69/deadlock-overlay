#include "ui/controls/scroll.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/theme.h"

#include <cmath>

namespace solace
{
namespace
{

constexpr float k_duration = 1.2f;
constexpr float k_wheel_multiplier = 1.f;

constexpr float k_px_per_notch = 100.f;
} // namespace

static float ease_scroll(float t)
{
    return ImMin(1.f, 1.001f - powf(2.f, -10.f * t));
}

void smooth_scroll::scroll_to(double y, double limit)
{
    const double clamped = y < 0.0 ? 0.0 : (y > limit ? limit : y);
    if (clamped == target && running)
        return;

    target = clamped;
    from = animated;
    t = 0.f;
    running = true;
}

void smooth_scroll::scroll_by(double delta, double limit)
{

    double next = target + delta;
    if (next < 0.0)
        next = 0.0;
    if (next > limit)
        next = limit;

    target = next;
    from = animated;
    t = 0.f;
    running = true;
}

double smooth_scroll::update(float dt, double limit)
{
    if (target > limit)
        target = limit;
    if (target < 0.0)
        target = 0.0;

    if (running)
    {
        t += dt;
        const float p = ImClamp(t / k_duration, 0.f, 1.f);
        animated = from + (target - from) * (double)ease_scroll(p);
        if (p >= 1.f)
        {
            animated = target;
            running = false;
        }
    }
    else
    {
        animated = target;
    }

    if (animated < 0.0)
        animated = 0.0;
    if (animated > limit)
        animated = limit;
    return animated;
}

float scroll_area(smooth_scroll& state, const ImRect& area, float content_height, bool owns_pointer)
{
    const ImGuiIO& io = ImGui::GetIO();
    const double limit = (double)ImMax(content_height - area.GetHeight(), 0.f);

    if (limit > 0.0 && io.MouseWheel != 0.f && area.Contains(io.MousePos) &&
        (owns_pointer || !pointer_claimed()))
        state.scroll_by(-(double)io.MouseWheel * k_px_per_notch * k_wheel_multiplier, limit);

    return (float)state.update(io.DeltaTime, limit);
}

void scrollbar(ImDrawList* dl, const ImRect& area, float content_height, float offset, float alpha)
{
    const float view = area.GetHeight();
    if (content_height <= view + 0.5f)
        return;

    const float track = view - px(8.f);
    const float thumb = ImMax(track * (view / content_height), px(24.f));
    const float travel = track - thumb;
    const float at = travel * ImClamp(offset / (content_height - view), 0.f, 1.f);

    const float w = px(4.f);
    const float x = area.Max.x + px(9.f);
    const float y = area.Min.y + px(4.f) + at;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + thumb),
                      mo::with_alpha(c_foreground, 0.16f * alpha), w * 0.5f);
}
} // namespace solace
