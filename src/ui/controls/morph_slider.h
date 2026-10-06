#pragma once
#include "imgui.h"
#include "imgui_internal.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::slides
{
enum morph_transition
{
    morph_melt = 0,
    morph_ripple,
    morph_shear,
    morph_swirl
};

struct morph_slider_options
{

    int transition = morph_melt;
    float duration = 1.1f;
    float intensity = 0.55f;
    float scale = 2.4f;
    float aberration = 0.35f;
    float drift = 0.4f;
    bool autoplay = false;
    float autoplay_delay = 4.f;
    bool loop = true;
    float radius = 16.f;
    ImU32 overlay = IM_COL32(0, 0, 0, 255);
    bool show_captions = true;
    bool show_controls = true;
    bool show_indicators = true;

    bool indicators_right = false;
    float indicator_pad_x = 18.f;
    float indicator_pad_y = 18.f;

    float fade_left = 0.f;
    float fade_top = 0.f;
    float fade_bottom = 0.f;

    bool show_progress = false;

    bool pause_on_hover = true;

    bool keyboard = true;
};

struct morph_slider_status
{
    int shown = 0;
    int previous = 0;
    int count = 0;
    bool animating = false;
    float progress = 0.f;
    float swap = 1.f;
    float until_next = 0.f;
};

const morph_slider_status& morph_slider_state();

[[nodiscard]] bool morph_slider_init(ID3D11Device* device, ID3D11DeviceContext* context);
void morph_slider_shutdown();

void morph_slider(ImDrawList* draw_list, const ImRect& rect, const ImRect& card,
                  float card_rounding,
                  const morph_slider_options& options = morph_slider_options());
} // namespace solace::slides
