#pragma once
#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace solace::images
{
struct texture
{
    ImTextureID id = ImTextureID_Invalid;
    int width = 0;
    int height = 0;
    std::string name;
};

struct options
{
    int max_edge = 512;
    float aspect = 416.f / 650.f;
    float radius_ratio = 0.f;
    float saturate = 1.f;
};

void load_folder(const std::filesystem::path& directory, const options& opts = options());

void update(ID3D11Device* device, ID3D11DeviceContext* context);

const std::vector<texture>& ready();

void shutdown();
} // namespace solace::images
