#pragma once

#include <string_view>

namespace solace::diagnostics
{
enum class exit_code : int
{
    success = 0,
    window_initialization = 10,
    renderer_initialization = 20,
    imgui_initialization = 30,
    ui_initialization = 40,
    rendering_failure = 50,
};

void info(std::string_view subsystem, std::string_view message) noexcept;
void warning(std::string_view subsystem, std::string_view message, long system_code = 0) noexcept;
void error(std::string_view subsystem, std::string_view message, long system_code = 0) noexcept;

[[nodiscard]] constexpr int to_process_exit_code(exit_code value) noexcept
{
    return static_cast<int>(value);
}
} // namespace solace::diagnostics
