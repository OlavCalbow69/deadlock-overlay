#pragma once

#include "imgui.h"

namespace solace::draw_utils
{
int rotation_start(const ImDrawList* draw_list);

void rotate_vertices(ImDrawList* draw_list, int first_vertex, float radians, const ImVec2& center);
} // namespace solace::draw_utils
