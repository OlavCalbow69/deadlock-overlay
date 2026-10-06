#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace solace
{
enum theme_variant
{
    theme_rectangle = 0,
    theme_circle,
};

enum rect_start
{
    start_top_left = 0,
    start_top_right,
    start_bottom_left,
    start_bottom_right,
    start_center,
    start_bottom_up,
};

void theme_tick(float dt);

bool theme_toggle(const char* id, const ImRect& rect, float icon_box, float alpha,
                  theme_variant variant = theme_rectangle, rect_start start = start_bottom_up);

void theme_reveal_draw(ImDrawList* dl, const ImRect& window_rect);
} // namespace solace
