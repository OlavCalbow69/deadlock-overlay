#pragma once
#include "imgui.h"

#include "ui/foundation/runtime.h"

namespace solace
{

inline ImU32 c_background = IM_COL32(0x15, 0x15, 0x15, 0xFF);
inline ImU32 c_foreground = IM_COL32(0xF2, 0xF2, 0xF2, 0xFF);
inline ImU32 c_card = IM_COL32(0x1C, 0x1C, 0x1C, 0xFF);
inline ImU32 c_muted_foreground = IM_COL32(0x86, 0x86, 0x86, 0xFF);
inline ImU32 c_border = IM_COL32(0xFF, 0xFF, 0xFF, 0x0D);
inline ImU32 c_border_strong = IM_COL32(0xFF, 0xFF, 0xFF, 0x1A);
inline ImU32 c_primary = IM_COL32(0xF2, 0xF2, 0xF2, 0xFF);
inline ImU32 c_primary_foreground = IM_COL32(0x15, 0x15, 0x15, 0xFF);

inline constexpr ImU32 c_destructive = IM_COL32(0xEE, 0x34, 0x3B, 0xFF);
inline constexpr ImU32 c_success = IM_COL32(0x00, 0xBD, 0x6C, 0xFF);
inline constexpr ImU32 c_amber_500 = IM_COL32(0xF9, 0x9C, 0x00, 0xFF);
inline constexpr ImU32 c_amber_400 = IM_COL32(0xFC, 0xBB, 0x00, 0xFF);

inline constexpr ImU32 c_emerald_400 = IM_COL32(0x34, 0xD3, 0x99, 0xFF);
inline constexpr ImU32 c_emerald_500 = IM_COL32(0x10, 0xB9, 0x81, 0xFF);
inline constexpr ImU32 c_emerald_600 = IM_COL32(0x05, 0x96, 0x69, 0xFF);

void set_dark(bool dark);
bool is_dark();

inline constexpr float text_xs = 12.f, leading_xs = 16.f;
inline constexpr float text_sm = 14.f, leading_sm = 20.f;
inline constexpr float text_base = 16.f, leading_base = 24.f;
inline constexpr float text_xl = 20.f, leading_xl = 28.f;

inline constexpr float tracking_tight = -0.025f;

inline constexpr float sp_1 = 4.f;
inline constexpr float sp_1_5 = 6.f;
inline constexpr float sp_2 = 8.f;
inline constexpr float sp_3 = 12.f;
inline constexpr float sp_3_5 = 14.f;
inline constexpr float sp_4 = 16.f;
inline constexpr float sp_5 = 20.f;
inline constexpr float sp_6 = 24.f;
inline constexpr float sp_10 = 40.f;
inline constexpr float sp_11 = 44.f;
inline constexpr float sp_12 = 48.f;

inline constexpr float max_w_sm = 384.f;
inline constexpr float rounded_3xl = 24.f;
inline constexpr float rounded_md = 6.f;

inline float px(float v)
{
    return v * ui_runtime::scale;
}
inline ImVec2 px(float x, float y)
{
    return ImVec2(x * ui_runtime::scale, y * ui_runtime::scale);
}

inline float line_top(ImFont* f, float line_height)
{
    return (line_height - (f ? f->LegacySize : 0.f)) * 0.5f;
}

ImFont* font_regular(float size);
ImFont* font_medium(float size);
ImFont* font_semibold(float size);
} // namespace solace
