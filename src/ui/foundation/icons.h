#pragma once
#include "imgui.h"
#include "imgui_internal.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::icons
{

enum class id
{
    none = 0,

    user,
    mail,
    lock,
    eye,
    eye_off,
    check,
    cross,
    loader,

    search,
    sparkles,
    inbox,
    circle_user_round,
    building_2,
    target,
    list_todo,
    notebook_tabs,
    workflow,
    layout_grid,
    chevron_right,
    chevron_down,
    chevrons_up_down,
    panel_left,
    command,

    sun,
    moon,

    bell,
    info,
    circle_alert,

    clock,
    star,
    settings,
    user_plus,
    log_out,
    plus,
    crosshair_simple,
    assassinate,
    code,
};

void initialize_textures(ID3D11Device* device, ID3D11DeviceContext* context);
void shutdown_textures();
void draw(id which, ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);

void user(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void mail(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void lock(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void eye(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void eye_off(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void check(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);
void cross(ImDrawList* dl, const ImVec2& top_left, float box, ImU32 col);

void loader(ImDrawList* dl, const ImVec2& center, float box, ImU32 col, float angle);

void stroke_path(ImDrawList* dl, const char* d, const ImVec2& top_left, float box, ImU32 col,
                 float stroke_width, float fraction = 1.f);
} // namespace solace::icons
