#pragma once
#include "imgui.h"
#include "imgui_internal.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::glass
{
struct cursor_options
{

    float dampening = 0.f;

    float blob_radius = 22.f;

    int trail_length = 40;
    float trail_seconds = 0.10f;

    float tail_fade = 0.3f;

    float threshold = 0.60f;

    float warp_amount = 25.f;
    float warp_scale = 5.f;
    float warp_speed = 0.9f;

    float flow_amount = 0.f;
    float flow_speed = 4.5f;

    float refraction = 0.30f;
    float blur_spread = 0.3f;
    float border_glow = 0.05f;
    float specular_gain = 0.2f;
    float opacity = 0.62f;

    float chromatic = 0.05f;
    float tint = 0.18f;
    float saturation = 0.f;
    float brightness = 0.f;

    ImU32 background = IM_COL32(0, 0, 0, 255);

    bool follow_pointer = true;
    ImVec2 pointer{0.f, 0.f};
};

[[nodiscard]] bool cursor_init(ID3D11Device* device, ID3D11DeviceContext* context);
void cursor_shutdown();

void cursor(ImDrawList* draw_list, const ImRect& viewport,
            const cursor_options& options = cursor_options());

cursor_options& settings();
bool& enabled();

bool cursor_live();
} // namespace solace::glass
