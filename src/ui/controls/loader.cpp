#include "ui/controls/loader.h"
#include "ui/foundation/motion/motion.h"

#include <vector>

namespace solace
{
namespace
{

constexpr float k_view_box = 100.f;
constexpr float k_radius = 15.f;
constexpr float k_cy = 50.f;
constexpr float k_std_dev = 5.f;
constexpr float k_alpha_gain = 20.f;
constexpr float k_alpha_bias = -8.f;

constexpr float k_cx_a[] = {30.f, 70.f, 30.f};
constexpr float k_cx_b[] = {70.f, 30.f, 70.f};
constexpr float k_cycle = 1.6f;

constexpr int k_max_size = 160;

struct field
{
    ImFontAtlasRectId rect = ImFontAtlasRectId_Invalid;
    int size = 0;
    std::vector<float> a, b;
};

field& store()
{
    static field f;
    return f;
}

void box_pass(const float* src, float* dst, int w, int h, int lo, int ro, bool horizontal)
{
    const int count = lo + ro + 1;
    const float inv = 1.f / (float)count;
    const int line_len = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    const int step = horizontal ? 1 : w;

    for (int line = 0; line < lines; line++)
    {
        const float* s = horizontal ? (src + (size_t)line * w) : (src + line);
        float* d = horizontal ? (dst + (size_t)line * w) : (dst + line);

        float sum = 0.f;
        for (int i = -lo; i <= ro; i++)
            if (i >= 0 && i < line_len)
                sum += s[(size_t)i * step];

        for (int i = 0; i < line_len; i++)
        {
            d[(size_t)i * step] = sum * inv;

            const int add = i + ro + 1;
            const int sub = i - lo;
            if (add < line_len)
                sum += s[(size_t)add * step];
            if (sub >= 0)
                sum -= s[(size_t)sub * step];
        }
    }
}

void gaussian(std::vector<float>& a, std::vector<float>& b, int n, float sigma)
{
    if (sigma <= 0.f)
        return;

    const int d = (int)floorf(sigma * 3.f * ImSqrt(2.f * IM_PI) / 4.f + 0.5f);
    if (d < 1)
        return;

    float* src = a.data();
    float* dst = b.data();

    auto run = [&](int lo, int ro)
    {
        box_pass(src, dst, n, n, lo, ro, true);
        box_pass(dst, src, n, n, lo, ro, false);
    };

    if (d & 1)
    {
        const int r = (d - 1) / 2;
        run(r, r);
        run(r, r);
        run(r, r);
    }
    else
    {
        run(d / 2, d / 2 - 1);
        run(d / 2 - 1, d / 2);
        run(d / 2, d / 2);
    }
}

void rasterise(std::vector<float>& out, int n, float cx_a, float cx_b)
{
    const float scale = (float)n / k_view_box;
    const float r = k_radius * scale;
    const float cy = k_cy * scale;
    const float ax = cx_a * scale;
    const float bx = cx_b * scale;

    for (int y = 0; y < n; y++)
    {
        const float py = (float)y + 0.5f;
        for (int x = 0; x < n; x++)
        {
            const float px = (float)x + 0.5f;

            const float da = ImSqrt((px - ax) * (px - ax) + (py - cy) * (py - cy));
            const float db = ImSqrt((px - bx) * (px - bx) + (py - cy) * (py - cy));
            const float d = ImMin(da, db);

            out[(size_t)y * n + x] = ImClamp(r + 0.5f - d, 0.f, 1.f);
        }
    }
}
} // namespace

void metaballs(ImDrawList* dl, const ImVec2& center, float size, float speed, float elapsed,
               ImU32 col)
{
    const int n = ImClamp((int)(size + 0.5f), 8, k_max_size);
    field& f = store();

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFontAtlasRect r;

    if (f.size != n || f.rect == ImFontAtlasRectId_Invalid || !atlas->GetCustomRect(f.rect, &r))
    {
        f.rect = atlas->AddCustomRect(n, n, &r);
        if (f.rect == ImFontAtlasRectId_Invalid)
            return;

        f.size = n;
        f.a.assign((size_t)n * n, 0.f);
        f.b.assign((size_t)n * n, 0.f);
    }

    const float duration = ImMax(speed, 0.001f) * k_cycle;
    const float t = fmodf(elapsed, duration);
    const float cx_a = mo::keyframes(k_cx_a, IM_ARRAYSIZE(k_cx_a), t, duration, mo::EASE_IN_OUT);
    const float cx_b = mo::keyframes(k_cx_b, IM_ARRAYSIZE(k_cx_b), t, duration, mo::EASE_IN_OUT);

    rasterise(f.a, n, cx_a, cx_b);
    gaussian(f.a, f.b, n, k_std_dev * (float)n / k_view_box);

    ImTextureData* tex = atlas->TexData;
    if (tex == nullptr || tex->Pixels == nullptr)
        return;

    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(col);
    const unsigned char cr = (unsigned char)(base.x * 255.f + 0.5f);
    const unsigned char cg = (unsigned char)(base.y * 255.f + 0.5f);
    const unsigned char cb = (unsigned char)(base.z * 255.f + 0.5f);

    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
        {

            const float a =
                ImClamp(f.a[(size_t)y * n + x] * k_alpha_gain + k_alpha_bias, 0.f, 1.f) * base.w;
            unsigned char* px = (unsigned char*)tex->GetPixelsAt(r.x + x, r.y + y);

            if (tex->Format == ImTextureFormat_Alpha8)
            {
                px[0] = (unsigned char)(a * 255.f + 0.5f);
            }
            else
            {
                px[0] = cr;
                px[1] = cg;
                px[2] = cb;
                px[3] = (unsigned char)(a * 255.f + 0.5f);
            }
        }

    ImFontAtlasTextureBlockQueueUpload(atlas, tex, r.x, r.y, n, n);

    const ImVec2 half(size * 0.5f, size * 0.5f);
    dl->AddImage(atlas->TexRef, ImVec2(IM_ROUND(center.x - half.x), IM_ROUND(center.y - half.y)),
                 ImVec2(IM_ROUND(center.x - half.x) + size, IM_ROUND(center.y - half.y) + size),
                 r.uv0, r.uv1, IM_COL32_WHITE);
}
} // namespace solace
