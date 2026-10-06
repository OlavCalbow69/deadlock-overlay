#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace solace::svg
{
struct sub_path
{
    ImVector<ImVec2> points;
    ImVector<float> cumulative;
    float length = 0.f;
    bool closed = false;
};

struct shape
{
    ImVector<sub_path> subs;
    float length = 0.f;
};

const shape& path(const char* d);

const shape& circle(float cx, float cy, float r);
const shape& rounded_rect(float x, float y, float w, float h, float rx);

void stroke(ImDrawList* draw_list, const shape& s, const ImVec2& origin, float scale, ImU32 col,
            float width, float fraction = 1.f);
} // namespace solace::svg
