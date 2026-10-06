#pragma once

#include <cstddef>
#include <string_view>

namespace solace::auth
{
inline constexpr std::size_t minimum_password_length = 8;

enum class password_quality : unsigned char
{
    too_short = 0,
    weak,
    fair,
    good,
    strong,
};

[[nodiscard]] bool is_blank(std::string_view value) noexcept;
[[nodiscard]] bool is_valid_email(std::string_view value) noexcept;
[[nodiscard]] bool has_minimum_password_length(std::string_view value) noexcept;
[[nodiscard]] password_quality evaluate_password(std::string_view value) noexcept;
} // namespace solace::auth
