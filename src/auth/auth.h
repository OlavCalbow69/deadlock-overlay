#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace solace
{
enum class auth_action
{
    none = 0,
    done,
    switch_form,
    terms,
    privacy,
};

struct auth_view_effect
{
    float opacity = 1.f;
    ImVec2 offset{};
    bool interactive = true;
};

const auth_view_effect& auth_effect();
void auth_apply_effect(ImDrawList* draw_list, int first_vertex);

namespace auth_layout
{
inline constexpr float stage_width = 416.f;

struct frame
{
    ImGuiWindow* window = nullptr;
    ImDrawList* draw_list = nullptr;
    ImRect card;
};

frame begin(const char* name, const ImVec2& card_size, bool interactive = true);
void end();
} // namespace auth_layout

auth_action signup_screen();
auth_action signin_screen();

void signup_reset();
void signin_reset();

void auth_stage(ImDrawList* draw_list, const ImRect& stage, const ImRect& card, float rounding);

enum class legal_document
{
    terms = 0,
    privacy,
};

bool legal_screen(legal_document document);
} // namespace solace
