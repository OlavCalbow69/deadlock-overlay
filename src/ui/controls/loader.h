#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace solace
{

void metaballs(ImDrawList* dl, const ImVec2& center, float size, float speed, float elapsed,
               ImU32 col);
}
