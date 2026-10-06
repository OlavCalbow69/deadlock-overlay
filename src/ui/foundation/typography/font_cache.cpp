#include "ui/foundation/typography/font_cache.h"
#include "ui/foundation/runtime.h"
#include "ui/foundation/typography/kerning.h"

#include <cmath>
#include <cstring>

#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif

namespace
{

float ttf_em_scale(const std::vector<unsigned char>& blob)
{
    auto u16 = [&](size_t at) -> unsigned int
    { return at + 1 < blob.size() ? (unsigned int)((blob[at] << 8) | blob[at + 1]) : 0u; };
    auto s16 = [&](size_t at) -> int
    {
        const int v = (int)u16(at);
        return v >= 0x8000 ? v - 0x10000 : v;
    };
    auto u32 = [&](size_t at) -> unsigned int
    {
        return at + 3 < blob.size()
                   ? ((unsigned int)blob[at] << 24) | ((unsigned int)blob[at + 1] << 16) |
                         ((unsigned int)blob[at + 2] << 8) | blob[at + 3]
                   : 0u;
    };

    if (blob.size() < 12)
        return 1.f;

    size_t head = 0, hhea = 0;
    const unsigned int table_count = u16(4);
    for (unsigned int i = 0; i < table_count; i++)
    {
        const size_t rec = 12 + (size_t)i * 16;
        if (rec + 16 > blob.size())
            break;

        const char tag[5] = {(char)blob[rec], (char)blob[rec + 1], (char)blob[rec + 2],
                             (char)blob[rec + 3], 0};
        if (strcmp(tag, "head") == 0)
            head = u32(rec + 8);
        else if (strcmp(tag, "hhea") == 0)
            hhea = u32(rec + 8);
    }

    if (head == 0 || hhea == 0)
        return 1.f;

    const float units_per_em = (float)u16(head + 18);
    const float ascent = (float)s16(hhea + 4);
    const float descent = (float)s16(hhea + 6);

    if (units_per_em <= 0.f || ascent - descent <= 0.f)
        return 1.f;

    return (ascent - descent) / units_per_em;
}
} // namespace

namespace solace
{
void font_cache::update()
{
    if (!solace::ui_runtime::fonts_dirty)
        return;

    ImGui::GetIO().Fonts->Clear();

    const std::vector<font_entry> previous = data;
    data.clear();
    for (const font_entry& entry : previous)
        add(*entry.source, entry.size);

    solace::ui_runtime::fonts_dirty = false;
}

ImFont* font_cache::get(const std::vector<unsigned char>& family, float size)
{
    for (const font_entry& entry : data)
        if (entry.source == &family && entry.size == size)
            return entry.font;

    return add(family, size);
}

static float kerning_hook(ImFont* f, float size, unsigned int c_prev, unsigned int c, void*)
{
    const std::vector<unsigned char>* src = fonts.source_of(f);
    if (src == nullptr)
        return 0.f;

    const solace::kerning::data& kd = solace::kerning::get(*src);
    const float v = solace::kerning::pair(kd, c_prev, c) * size / solace::kerning::layout_units(kd);
    return v > 0.f ? floorf(v + 0.5f) : 0.f;
}

void font_cache::install_kerning()
{
    ImGui::GetIO().Fonts->KerningFunc = kerning_hook;
}

const std::vector<unsigned char>* font_cache::source_of(ImFont* f) const
{
    for (const font_entry& entry : data)
        if (entry.font == f)
            return entry.source;
    return nullptr;
}

ImFont* font_cache::add(const std::vector<unsigned char>& family, float size)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;

#ifdef IMGUI_ENABLE_FREETYPE

    cfg.FontLoaderFlags = 0;
#endif

    const float pixels = floorf(size * ttf_em_scale(family) * solace::ui_runtime::scale + 0.5f);
    ImFont* result = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(family.data()), (int)family.size(), pixels, &cfg);

    data.push_back({&family, size, result});
    return result;
}
} // namespace solace
