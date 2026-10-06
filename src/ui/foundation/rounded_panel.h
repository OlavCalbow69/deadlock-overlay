#pragma once

#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::rounded_panel
{
[[nodiscard]] bool init(ID3D11Device* device, ID3D11DeviceContext* context);
void shutdown();

// Draws an analytic rounded silhouette with an 8x8 filtered subpixel edge.
void draw(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, ImU32 fill, float radius);
} // namespace solace::rounded_panel
