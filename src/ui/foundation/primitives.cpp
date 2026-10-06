#include "ui/foundation/primitives.h"

#include "graphics/snapshot.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/theme.h"

#include "imgui_internal.h"

#include <cstring>
#include <math.h>

namespace solace
{
namespace
{
int decode(const char* p, const char* end, unsigned int* out)
{
    const unsigned int c = (unsigned char)*p;
    if (c < 0x80)
    {
        *out = c;
        return 1;
    }
    return ImTextCharFromUtf8(out, p, end);
}

template <typename F> float walk(ImFont* f, const char* s, const char* end, float tracking, F&& fn)
{
    if (!f || !s)
        return 0.f;
    if (!end)
        end = s + strlen(s);

    const float size = f->LegacySize;

    float x = 0.f;
    unsigned int prev = 0;

    for (const char* p = s; p < end && *p;)
    {
        unsigned int c = 0;
        const int consumed = decode(p, end, &c);
        if (consumed <= 0)
            break;

        if (prev)
            x += ImFontGetKerning(f, size, prev, c);

        fn(c, p, p + consumed, x);

        x += f->CalcTextSizeA(size, FLT_MAX, 0.f, p, p + consumed).x + tracking;
        prev = c;
        p += consumed;
    }

    return x > 0.f ? x - tracking : 0.f;
}

template <typename F>
void for_each_wrapped_line(ImFont* f, const char* s, float wrap_width, F&& callback)
{
    if (!f || !s || !*s)
        return;

    const char* const text_end = s + strlen(s);
    const float size = f->LegacySize;

    const char* line = s;
    while (line < text_end)
    {
        const char* last_break = nullptr;
        const char* fit_end = text_end;

        float x = 0.f;
        unsigned int prev = 0;

        for (const char* p = line; p < text_end;)
        {
            unsigned int c = 0;
            const int consumed = decode(p, text_end, &c);
            if (consumed <= 0)
                break;

            if (prev)
                x += ImFontGetKerning(f, size, prev, c);

            const float advance = f->CalcTextSizeA(size, FLT_MAX, 0.f, p, p + consumed).x;
            if (x + advance > wrap_width && p != line)
            {
                fit_end = last_break ? last_break : p;
                break;
            }

            if (c == ' ')
                last_break = p;

            x += advance;
            prev = c;
            p += consumed;
        }

        callback(line, fit_end);

        line = fit_end;
        while (line < text_end && *line == ' ')
            line++;
    }
}
} // namespace

float text_width(ImFont* f, const char* s)
{
    if (!f || !s || !*s)
        return 0.f;
    return f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0.f, s).x;
}

float text_width(ImFont* f, const char* s, const char* end)
{
    if (!f || !s || s == end)
        return 0.f;
    return f->CalcTextSizeA(f->LegacySize, FLT_MAX, 0.f, s, end).x;
}

void draw_text(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
               const char* end)
{
    if (!f || !s || (col & IM_COL32_A_MASK) == 0)
        return;
    dl->AddText(f, f->LegacySize, pos, col, s, end);
}

void draw_text_blur(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                    float blur)
{
    if (!f || !s || (col & IM_COL32_A_MASK) == 0)
        return;

    if (blur < 0.25f)
    {
        draw_text(dl, f, pos, col, s);
        return;
    }

    for_each_blur_tap(blur, col,
                      [&](const ImVec2& off, ImU32 tap_col)
                      {
                          dl->AddText(f, f->LegacySize, ImVec2(pos.x + off.x, pos.y + off.y),
                                      tap_col, s, nullptr);
                      });
}

void backdrop_blur(ImDrawList* dl, const ImRect& rect, float radius, float rounding, float alpha)
{
    snapshot::capture_backdrop(dl);
    if (!snapshot::backdrop_ready() || alpha <= 0.004f)
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 origin = vp->Pos;
    const ImVec2 size = vp->Size;
    if (size.x <= 0.f || size.y <= 0.f)
        return;

    constexpr int k_taps = 7;
    const float sigma = ImMax(radius, 0.01f);
    const float step = radius * (4.f / (float)(k_taps - 1));

    float weights[k_taps];
    for (int i = 0; i < k_taps; i++)
    {
        const float d = (float)(i - k_taps / 2) * step;
        weights[i] = expf(-(d * d) / (2.f * sigma * sigma));
    }

    float total = 0.f;
    for (int y = 0; y < k_taps; y++)
        for (int x = 0; x < k_taps; x++)
            total += weights[x] * weights[y];

    float accumulated = 0.f;

    dl->PushTexture(ImTextureRef(snapshot::backdrop()));
    for (int ty = 0; ty < k_taps; ty++)
        for (int tx = 0; tx < k_taps; tx++)
        {
            const float w = (weights[tx] * weights[ty]) / total;
            if (w < 0.0008f)
                continue;

            accumulated += w;
            const float a = ImClamp(w / accumulated, 0.f, 1.f) * alpha;
            const ImVec2 off((float)(tx - k_taps / 2) * step, (float)(ty - k_taps / 2) * step);

            dl->PathRect(rect.Min, rect.Max, rounding);
            const int n = dl->_Path.Size;
            if (n >= 3)
            {
                dl->PrimReserve((n - 2) * 3, n);
                const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
                const ImU32 col = mo::with_alpha(IM_COL32_WHITE, a);

                for (int i = 0; i < n; i++)
                {
                    const ImVec2& p = dl->_Path[i];

                    dl->PrimWriteVtx(p,
                                     ImVec2((p.x + off.x - origin.x) / size.x,
                                            (p.y + off.y - origin.y) / size.y),
                                     col);
                }
                for (int i = 2; i < n; i++)
                {
                    dl->PrimWriteIdx(base);
                    dl->PrimWriteIdx((ImDrawIdx)(base + i - 1));
                    dl->PrimWriteIdx((ImDrawIdx)(base + i));
                }
            }
            dl->_Path.Size = 0;
        }
    dl->PopTexture();
}

void draw_border(ImDrawList* dl, const ImRect& box, float rounding, float width, ImU32 border,
                 ImU32 inner)
{
    dl->AddRectFilled(box.Min, box.Max, border, rounding);

    if (width <= 0.f)
        return;

    dl->AddRectFilled(ImVec2(box.Min.x + width, box.Min.y + width),
                      ImVec2(box.Max.x - width, box.Max.y - width), inner,
                      ImMax(rounding - width, 0.f));
}

void draw_text_tracked(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                       float tracking)
{
    if (!f || !s || (col & IM_COL32_A_MASK) == 0)
        return;

    const bool asked_for_tracking = (tracking != 0.f);
    if (tracking < 0.f)
        tracking = 0.f;

    if (!asked_for_tracking)
    {
        draw_text(dl, f, pos, col, s);
        return;
    }

    walk(f, s, nullptr, tracking, [&](unsigned int, const char* b, const char* e, float x)
         { dl->AddText(f, f->LegacySize, ImVec2(IM_ROUND(pos.x + x), pos.y), col, b, e); });
}

int wrapped_line_count(ImFont* f, const char* s, float wrap_width)
{
    int line_count = 0;
    for_each_wrapped_line(f, s, wrap_width, [&](const char*, const char*) { ++line_count; });
    return ImMax(1, line_count);
}

void draw_text_wrapped(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                       float wrap_width, float line_height)
{
    if (!f || !s || (col & IM_COL32_A_MASK) == 0)
        return;

    float y = pos.y + line_top(f, line_height);
    for_each_wrapped_line(f, s, wrap_width,
                          [&](const char* begin, const char* end)
                          {
                              draw_text(dl, f, ImVec2(pos.x, y), col, begin, end);
                              y += line_height;
                          });
}

void draw_text_wrapped_blur(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                            float wrap_width, float line_height, float blur)
{
    if (!f || !s || (col & IM_COL32_A_MASK) == 0)
        return;

    float y = pos.y + line_top(f, line_height);
    for_each_wrapped_line(f, s, wrap_width,
                          [&](const char* begin, const char* end)
                          {
                              if (blur < 0.25f)
                              {
                                  draw_text(dl, f, ImVec2(pos.x, y), col, begin, end);
                              }
                              else
                              {
                                  for_each_blur_tap(blur, col,
                                                    [&](const ImVec2& off, ImU32 tap_col)
                                                    {
                                                        dl->AddText(
                                                            f, f->LegacySize,
                                                            ImVec2(pos.x + off.x, y + off.y),
                                                            tap_col, begin, end);
                                                    });
                              }
                              y += line_height;
                          });
}

float draw_text_ellipsis(ImDrawList* dl, ImFont* f, const ImVec2& pos, ImU32 col, const char* s,
                         float max_width)
{
    if (!f || !s || !*s || max_width <= 0.f)
        return 0.f;

    const float full = text_width(f, s);
    if (full <= max_width)
    {
        draw_text(dl, f, pos, col, s);
        return full;
    }

    static const char* k_ellipsis = "\xE2\x80\xA6";
    const float dots = text_width(f, k_ellipsis);
    const float room = ImMax(max_width - dots, 0.f);

    const char* end = s;
    const char* cut = s;
    while (*end)
    {
        const char* next = end + 1;
        while ((*next & 0xC0) == 0x80)
            next++;
        if (text_width(f, s, next) > room)
            break;
        cut = next;
        end = next;
    }

    if (cut > s)
        draw_text(dl, f, pos, col, s, cut);

    const float used = text_width(f, s, cut);
    draw_text(dl, f, ImVec2(pos.x + used, pos.y), col, k_ellipsis);
    return used + dots;
}

namespace
{
int g_claim_frame = -100;
}

void claim_pointer()
{
    g_claim_frame = ImGui::GetFrameCount();
}

bool pointer_claimed()
{
    return g_claim_frame >= ImGui::GetFrameCount() - 1;
}

ImU32 color_tween::update(ImU32 target, float dt, float duration)
{
    const ImVec4 want = ImGui::ColorConvertU32ToFloat4(target);

    if (!seeded)
    {
        current = from = to = want;
        seeded = true;
        t = duration;
        return target;
    }

    if (want.x != to.x || want.y != to.y || want.z != to.z || want.w != to.w)
    {
        from = current;
        to = want;
        t = 0.f;
    }

    t += dt;
    const float p = duration > 0.f ? ImClamp(t / duration, 0.f, 1.f) : 1.f;
    current = ImLerp(from, to, mo::EASE_STANDARD(p));

    return ImGui::ColorConvertFloat4ToU32(current);
}
} // namespace solace
