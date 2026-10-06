#include "ui/foundation/theme.h"

#include "generated/fonts/geist_data.h"
#include "ui/foundation/typography/font_cache.h"

namespace solace
{
ImFont* font_regular(float size)
{
    return fonts.get(geist_regular, size);
}

ImFont* font_medium(float size)
{
    return fonts.get(geist_medium, size);
}

ImFont* font_semibold(float size)
{
    return fonts.get(geist_semibold, size);
}

namespace
{
struct palette
{
    ImU32 background, foreground, card, muted_foreground;
    ImU32 border, border_strong;
};

constexpr palette k_light{
    IM_COL32(0xFC, 0xFC, 0xFC, 0xFF), IM_COL32(0x0B, 0x0B, 0x0B, 0xFF),
    IM_COL32(0xF5, 0xF5, 0xF5, 0xFF), IM_COL32(0x63, 0x63, 0x63, 0xFF),
    IM_COL32(0x0B, 0x0B, 0x0B, 0x0F), IM_COL32(0x0B, 0x0B, 0x0B, 0x1F),
};

constexpr palette k_dark{
    IM_COL32(0x15, 0x15, 0x15, 0xFF), IM_COL32(0xF2, 0xF2, 0xF2, 0xFF),
    IM_COL32(0x1C, 0x1C, 0x1C, 0xFF), IM_COL32(0x86, 0x86, 0x86, 0xFF),
    IM_COL32(0xFF, 0xFF, 0xFF, 0x0D), IM_COL32(0xFF, 0xFF, 0xFF, 0x1A),
};

bool g_dark = true;
} // namespace

bool is_dark()
{
    return g_dark;
}

void set_dark(bool dark)
{
    g_dark = dark;
    const palette& p = dark ? k_dark : k_light;

    c_background = p.background;
    c_foreground = p.foreground;
    c_card = p.card;
    c_muted_foreground = p.muted_foreground;
    c_border = p.border;
    c_border_strong = p.border_strong;

    c_primary = p.foreground;
    c_primary_foreground = p.background;
}
} // namespace solace
