#include "ui/foundation/draw.h"

#include <algorithm>
#include <cmath>

namespace solace::draw_utils
{
int rotation_start(const ImDrawList* draw_list)
{
    return draw_list ? draw_list->VtxBuffer.Size : 0;
}

void rotate_vertices(ImDrawList* draw_list, int first_vertex, float radians, const ImVec2& center)
{
    if (!draw_list || radians == 0.f)
        return;

    const int begin = (std::max)(first_vertex, 0);
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);

    for (int index = begin; index < draw_list->VtxBuffer.Size; ++index)
    {
        ImVec2& position = draw_list->VtxBuffer[index].pos;
        const float x = position.x - center.x;
        const float y = position.y - center.y;
        position.x = center.x + x * cosine - y * sine;
        position.y = center.y + x * sine + y * cosine;
    }
}
} // namespace solace::draw_utils
