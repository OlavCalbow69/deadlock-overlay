#pragma once

#include "ui/screens/navigation.h"

struct ImRect;

namespace solace
{
void draw_page(route destination, const char* title, const char* const* subs, int sub_count,
               int* sub, const ImRect& area, float alpha);
}
