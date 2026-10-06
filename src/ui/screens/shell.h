#pragma once

#include "imgui.h"
#include "imgui_internal.h"

namespace solace
{
namespace shell
{
inline constexpr float width = 1120.f;
inline constexpr float height = 720.f;
inline constexpr float rounding = 16.f;

ImVec2 animate_size(const ImVec2& target);
ImRect plate(float background_opacity = 1.f);
} // namespace shell

enum class shell_page { camera, health, connection };

struct shell_extension
{
    void (*content)(shell_page page, const ImRect& body, float alpha, void* context) = nullptr;
    void (*close)(void* context) = nullptr;
    void (*appearance)(void* context) = nullptr;
    shell_page* active_page = nullptr;
    float frame_opacity = 1.f;
    void* context = nullptr;
    const char* status = "Waiting for Deadlock";
};
bool menu_screen(float alpha, const shell_extension* extension = nullptr);
} // namespace solace
