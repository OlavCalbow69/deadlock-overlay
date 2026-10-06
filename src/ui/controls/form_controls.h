#pragma once

#include "imgui.h"

#include "ui/controls/caret.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"

#include <string>

namespace solace
{
bool link(const char* id, ImDrawList* draw_list, ImFont* font, const ImVec2& pos, const char* text,
          ImU32 color, float alpha, bool underline = true);

bool link_span(const char* id, ImDrawList* draw_list, ImFont* font, const ImVec2& pos, float width,
               ImU32 color, float alpha);

enum icon_id
{
    icon_none = 0,
    icon_user,
    icon_mail,
    icon_lock
};

struct input_state
{
    solace::caret caret;
    color_tween border_col, ring_col;
    mo::presence error_presence;
    std::string error_text;
    float shake = 1e6f;
    bool had_error = false;
    bool success_mounted = false;
    mo::clock success_draw;
    bool focused = false;

    mo::spring reveal_t, reveal_scale, reveal_hover;
    float reveal_pop = 1e6f;
};

struct input_desc
{
    const char* label = nullptr;
    const char* placeholder = nullptr;
    char* buf = nullptr;
    int buf_size = 0;
    icon_id left = icon_none;
    bool mask = false;
    bool reveal_toggle = false;
    bool* reveal = nullptr;
    const char* error = nullptr;
    bool success = false;
    bool disabled = false;
};

void input_update(input_state& state, const input_desc& description, float delta_time);
float input_height(const input_state& state);
bool input_draw(const char* id, input_state& state, const input_desc& description,
                const ImVec2& pos, float width, bool* out_blurred);

struct checkbox_state
{
    color_tween fill, border;
    mo::spring press;
    mo::presence mark;
    mo::clock mark_draw;
};

void checkbox_update(checkbox_state& state, bool checked, float delta_time);
bool checkbox_draw(const char* id, checkbox_state& state, bool* checked, const char* label,
                   const ImVec2& pos, bool disabled);

enum button_state
{
    btn_idle = 0,
    btn_loading,
    btn_success,
    btn_error
};

struct icon_slot
{
    mo::presence pres;
    mo::spring width, scale, opacity, blur;
    float exit_width = 0.f, exit_scale = 1.f, exit_opacity = 1.f, exit_blur = 0.f;
    bool exit_captured = false;
};

struct cascade_layer
{
    std::string text;
    bool exiting = false;
    float t = 0.f;
    mo::spring letter[64];
    bool live = false;
};

struct stateful_button_state
{
    mo::spring press;
    color_tween background;
    icon_slot loading, success, error;
    cascade_layer layers[2];
    mo::spring text_width;
    float spin = 0.f;
    int last_state = -1;
    std::string last_label;
};

void stateful_button_update(stateful_button_state& state, button_state status, const char* label,
                            float delta_time);
bool stateful_button_draw(const char* id, stateful_button_state& state, button_state status,
                          const ImVec2& pos, float width, bool disabled);
} // namespace solace
