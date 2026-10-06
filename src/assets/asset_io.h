#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace solace::asset_io
{
std::filesystem::path asset_directory(const wchar_t* name, const wchar_t* environment_key);

std::vector<std::filesystem::path> image_files(const std::filesystem::path& directory);

std::vector<unsigned char> read_binary(const std::filesystem::path& path);
std::string stem_utf8(const std::filesystem::path& path);
} // namespace solace::asset_io
