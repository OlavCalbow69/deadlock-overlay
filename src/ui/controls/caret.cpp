#include "ui/controls/caret.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/theme.h"

namespace solace
{
namespace
{

constexpr float k_alpha_speed = 7.f;
constexpr float k_glide_speed = 24.f;

void step_to(float& value, float target, float step)
{
    if (value < target)
        value = ImMin(value + step, target);
    else if (value > target)
        value = ImMax(value - step, target);
}

void glide_to(float& value, float target, float t)
{
    value = ImLerp(value, target, ImClamp(t, 0.f, 1.f));
}
} // namespace

void draw_caret(ImDrawList* dl, caret& c, const ImRect& field, float height, bool active, ImU32 col,
                float alpha)
{
    const float dt = ImGui::GetIO().DeltaTime;

    step_to(c.alpha, active ? 1.f : 0.f, k_alpha_speed * dt);

    if (active)
    {

        const ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
        c.target = ImFloor(ime.InputPos.x + 1.f - field.Min.x + 0.5f);
    }

    if (c.alpha <= 0.01f)
        return;

    glide_to(c.offset, c.target, k_glide_speed * dt);

    const float x = field.Min.x + c.offset;
    const float y = field.GetCenter().y;
    const float w = ImMax(px(1.f), 1.f);

    dl->AddRectFilled(ImVec2(x, y - height * 0.5f), ImVec2(x + w, y + height * 0.5f),
                      mo::with_alpha(col, c.alpha * alpha));
}
} // namespace solace
