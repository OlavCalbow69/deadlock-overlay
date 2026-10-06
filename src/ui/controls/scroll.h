#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace solace
{

struct smooth_scroll
{
    double target = 0.0;
    double animated = 0.0;
    double from = 0.0;
    float t = 0.f;
    bool running = false;

    void scroll_by(double delta, double limit);
    void scroll_to(double y, double limit);
    void jump(double y)
    {
        target = animated = from = y;
        running = false;
    }

    double update(float dt, double limit);
};

float scroll_area(smooth_scroll& state, const ImRect& area, float content_height,
                  bool owns_pointer = false);

void scrollbar(ImDrawList* dl, const ImRect& area, float content_height, float offset, float alpha);
} // namespace solace
