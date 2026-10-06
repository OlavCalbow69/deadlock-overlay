#pragma once

#include "imgui.h"

struct ImRect;

namespace solace
{

inline constexpr float switch_w = 48.f;
inline constexpr float switch_h = 28.f;

bool switch_toggle(const char* id, const ImVec2& pos, bool* checked, const char* label = nullptr,
                   bool disabled = false);

enum tabs_variant
{
    tabs_pill = 0,
    tabs_underline,
    tabs_segment
};

float tabs_height(tabs_variant variant);
float tabs_width(const char* const* labels, int count, tabs_variant variant);

bool tabs(const char* id, const ImVec2& pos, const char* const* labels, int count, int* active,
          tabs_variant variant = tabs_pill);

inline constexpr float select_h = 38.f;

void select(const char* id, const ImVec2& pos, float width, const char* const* options, int count,
            int* value, const char* placeholder);

inline constexpr float slider_h = 40.f;
inline constexpr float k_slider_track_h = 10.f;

bool range_slider(const char* id, const ImVec2& pos, float width, float* value, float min_value,
                  float max_value, int ticks = 0);

float number_value(const char* id, float target, float duration = 1.2f);

enum badge_status
{
    badge_neutral = 0,
    badge_info,
    badge_warn,
    badge_bad,
    badge_good
};

float badge_width(const char* label, bool medium = false);
void badge(const char* id, ImDrawList* draw_list, const ImVec2& pos, const char* label,
           badge_status status = badge_neutral, bool medium = false, float alpha = 1.f);

inline constexpr float accordion_trigger_h = 54.f;

float accordion(const char* id, ImDrawList* draw_list, const ImVec2& pos, float width,
                const char* title, bool* open, float body_h, float alpha = 1.f,
                float* out_text_alpha = nullptr);

inline constexpr float otp_cell_w = 48.f;
inline constexpr float otp_cell_h = 56.f;
inline constexpr float otp_gap = 8.f;

enum otp_status
{
    otp_idle = 0,
    otp_error,
    otp_success
};

float otp_width(int length);

bool otp_input(const char* id, ImDrawList* draw_list, const ImVec2& pos, char* buf, int length,
               otp_status status = otp_idle, float alpha = 1.f);

enum toast_status
{
    toast_neutral = 0,
    toast_info,
    toast_loading,
    toast_success,
    toast_error
};

void toast(const char* title, const char* description, toast_status status = toast_neutral);
void toasts_draw(const ImRect& area);

void flush_overlays();

bool overlay_open();
} // namespace solace
