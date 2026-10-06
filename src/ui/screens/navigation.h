#pragma once

namespace solace
{
enum class route : int
{
    search = 0,
    assistant,
    messages,
    settings,
    presets,
    patches,
    tasks,
    notes,
    automation,
    dashboard,
    profile,
    notifications,
    preferences,
    count,
};

inline constexpr int route_count = static_cast<int>(route::count);

[[nodiscard]] constexpr int route_index(route value) noexcept
{
    return static_cast<int>(value);
}

[[nodiscard]] constexpr route route_from_index(int value) noexcept
{
    return value < 0              ? route::search
           : value >= route_count ? route::preferences
                                  : static_cast<route>(value);
}

[[nodiscard]] constexpr bool is_account_route(route value) noexcept
{
    return value == route::profile || value == route::notifications || value == route::preferences;
}
} // namespace solace
