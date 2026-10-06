#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "ui/foundation/icons.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/theme.h"

#include <cmath>

namespace solace
{

float text_width(ImFont* f, const char* s);
float text_width(ImFont* f, const char* s, const char* end);
void draw_text(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
               const char* end = nullptr);

int wrapped_line_count(ImFont* f, const char* s, float wrap_width);
void draw_text_wrapped(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                       float wrap_width, float line_height);

template <typename F> void for_each_blur_tap(float blur, ImU32 col, F&& fn)
{
    constexpr int k_taps = 9;
    const float sigma = ImMax(blur * 0.85f, 0.01f);
    const float step = blur * 0.5f;

    float weights[k_taps];
    for (int i = 0; i < k_taps; i++)
    {
        const float d = (float)(i - k_taps / 2) * step;
        weights[i] = expf(-(d * d) / (2.f * sigma * sigma));
    }

    float total = 0.f;
    for (int y = 0; y < k_taps; y++)
        for (int x = 0; x < k_taps; x++)
            total += weights[x] * weights[y];

    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(col);
    for (int ty = 0; ty < k_taps; ty++)
        for (int tx = 0; tx < k_taps; tx++)
        {
            const float w = (weights[tx] * weights[ty]) / total;
            if (w < 0.004f)
                continue;

            ImVec4 c = base;
            c.w *= w;
            fn(ImVec2((float)(tx - k_taps / 2) * step, (float)(ty - k_taps / 2) * step),
               ImGui::ColorConvertFloat4ToU32(c));
        }
}

void draw_text_blur(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                    float blur);
void draw_text_wrapped_blur(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                            float wrap_width, float line_height, float blur);

float draw_text_ellipsis(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                         float max_width);

void claim_pointer();
bool pointer_claimed();

void backdrop_blur(ImDrawList* dl, const ImRect& rect, float radius, float rounding,
                   float alpha = 1.f);

void draw_text_tracked(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                       float tracking);

void draw_border(ImDrawList* dl, const ImRect& box, float rounding, float width, ImU32 border,
                 ImU32 inner);

struct color_tween
{
    ImVec4 current{}, from{}, to{};
    float t = 0.f;
    bool seeded = false;

    ImU32 update(ImU32 target, float dt, float duration = 0.2f);
};

} // namespace solace
