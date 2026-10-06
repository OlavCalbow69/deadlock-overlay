#pragma once
#include "imgui.h"

#include <filesystem>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::avatars
{
void load(const std::filesystem::path& people_directory,
          const std::filesystem::path& logo_directory, const std::filesystem::path& brand_directory,
          ID3D11Device* device, ID3D11DeviceContext* context);
void shutdown();

ImTextureID me();
ImTextureID other(int index);
ImTextureID logo(int index);

ImTextureID brand(const char* name);

bool draw(ImDrawList* draw_list, ImTextureID texture, const ImVec2& top_left, float size,
          float alpha, float rounding = -1.f);
} // namespace solace::avatars
