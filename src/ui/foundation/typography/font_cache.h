#pragma once

#include "imgui.h"

#include <vector>

namespace solace
{
class font_cache
{
  public:
    void update();

    ImFont* get(const std::vector<unsigned char>& family, float size);

    const std::vector<unsigned char>* source_of(ImFont* f) const;

    void install_kerning();
    void reset() { data.clear(); }

  private:
    struct font_entry
    {
        const std::vector<unsigned char>* source;
        float size;
        ImFont* font;
    };

    ImFont* add(const std::vector<unsigned char>& family, float size);

    std::vector<font_entry> data;
};

inline font_cache fonts;
} // namespace solace
