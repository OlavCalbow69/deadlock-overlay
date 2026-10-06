#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/foundation/icons.h"
#include "ui/screens/navigation.h"

namespace solace
{
struct search_item
{
    const char* title;
    const char* description;
    const char* keywords;
    icons::id icon;
    route destination;
    int sub;
};

inline constexpr float search_trigger_w = 288.f;
inline constexpr float search_trigger_h = 48.f;

void morphing_search_trigger(const ImRect& anchor, float alpha, const char* placeholder = "Search",
                             const char* shortcut = "F");

int morphing_search_overlay(const ImRect& viewport, const search_item* items, int count,
                            float alpha);

bool morphing_search_open();
} // namespace solace
