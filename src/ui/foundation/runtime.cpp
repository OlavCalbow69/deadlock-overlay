#include "ui/foundation/runtime.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace solace::ui_runtime
{
namespace
{
std::vector<detail::animation_bucket_base*>& animation_buckets()
{
    static std::vector<detail::animation_bucket_base*> buckets;
    return buckets;
}

float scaled(float value)
{
    return std::round(value * scale);
}
} // namespace

void detail::register_animation_bucket(animation_bucket_base* bucket)
{
    if (!bucket)
        return;

    auto& buckets = animation_buckets();
    if (std::find(buckets.begin(), buckets.end(), bucket) == buckets.end())
        buckets.push_back(bucket);
}

void collect_animation_states(int max_unused_frames)
{
    if (!ImGui::GetCurrentContext() || max_unused_frames < 0)
        return;

    static int last_collection_frame = -1;
    const int current_frame = ImGui::GetFrameCount();
    if (last_collection_frame >= 0 && current_frame >= last_collection_frame &&
        current_frame - last_collection_frame < 120)
        return;

    last_collection_frame = current_frame;
    for (detail::animation_bucket_base* bucket : animation_buckets())
        bucket->collect(current_frame, max_unused_frames);
}

void clear_animation_states() noexcept
{
    for (detail::animation_bucket_base* bucket : animation_buckets())
        bucket->clear();
}

void apply_style()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(scaled(10.f), scaled(10.f));
    style.ItemSpacing = ImVec2(scaled(10.f), scaled(10.f));
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 0.f;
    style.FrameBorderSize = 0.f;
    style.FrameRounding = scaled(8.f);
    style.WindowRounding = scaled(14.f);
    style.ScrollbarSize = scaled(10.f);

    style.Colors[ImGuiCol_WindowBg] = ImColor(25, 25, 28).Value;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    style.Colors[ImGuiCol_Text] = ImColor(255, 255, 255).Value;
    style.Colors[ImGuiCol_TextDisabled] = ImColor(110, 110, 129).Value;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.f, 0.f, 0.f, 0.f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.f, 0.f, 0.f, 0.f);
    style.Colors[ImGuiCol_Border] = ImColor(35, 35, 44).Value;
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(176.f / 255.f, 180.f / 255.f, 1.f, 0.28f);
    style.Colors[ImGuiCol_InputTextCursor] = ImColor(176, 180, 255).Value;
    style.Colors[ImGuiCol_NavCursor] = ImVec4(0.f, 0.f, 0.f, 0.f);
}
} // namespace solace::ui_runtime
