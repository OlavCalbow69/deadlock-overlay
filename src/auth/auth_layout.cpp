#include "auth/auth.h"

#include "application/brand.h"
#include "assets/avatars.h"
#include "ui/controls/morph_slider.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"

#include <cmath>
#include <vector>

namespace solace
{
namespace
{
struct testimonial
{
    const char* quote;
    const char* name;
    const char* role;
    int face;
};

const testimonial k_testimonials[] = {
    {"Moved a whole settings profile across in an afternoon. What I did not "
     "expect was how quiet the support channel got afterwards.",
     "Ravi Menon", "Support, Vermillion", 0},

    {"Two weeks in and nobody has asked me which setting ate the frame time. "
     "That has never happened on a patch day before.",
     "Ines Duarte", "Build lead, Kestrel", 1},

    {"The settings handoff finally feels predictable. The team can see what "
     "changed without digging through a separate report.",
     "Mira Chen", "Release, Solace Lab", 2},

    {"I handed it to the beta testers on a Friday without training anyone. "
     "There were no questions on Monday.",
     "Amara Okafor", "Community, Halcyon", 3},

    {"It is the first launcher in years that got faster as the library grew "
     "rather than slower.",
     "Julian Reyes", "Packaging, Orrin", 4},
};

constexpr int k_testimonial_count = (int)(sizeof(k_testimonials) / sizeof(k_testimonials[0]));

float smootherstep(float t)
{
    t = ImClamp(t, 0.f, 1.f);
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

void ramp(ImDrawList* dl, const ImRect& r, ImU32 col, float a0, float a1, bool horizontal,
          int bands = 16)
{
    const float span = horizontal ? r.GetWidth() : r.GetHeight();
    if (span <= 0.f || r.GetWidth() <= 0.f || r.GetHeight() <= 0.f)
        return;

    for (int i = 0; i < bands; i++)
    {
        const float t0 = (float)i / (float)bands;
        const float t1 = (float)(i + 1) / (float)bands;

        const ImU32 c0 = mo::with_alpha(col, a0 + (a1 - a0) * smootherstep(t0));
        const ImU32 c1 = mo::with_alpha(col, a0 + (a1 - a0) * smootherstep(t1));

        if (horizontal)
            dl->AddRectFilledMultiColor(ImVec2(r.Min.x + span * t0, r.Min.y),
                                        ImVec2(r.Min.x + span * t1, r.Max.y), c0, c1, c1, c0);
        else
            dl->AddRectFilledMultiColor(ImVec2(r.Min.x, r.Min.y + span * t0),
                                        ImVec2(r.Max.x, r.Min.y + span * t1), c0, c0, c1, c1);
    }
}

ImU32 grain_hash(ImU32 value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

void testimonial_grain(ImDrawList* dl, const ImRect& stage, float band_bottom)
{
    struct grain_dot
    {
        ImVec2 at;
        ImU32 col;
    };

    // Keep the grain in screen pixels so it remains genuinely fine at every DPI.
    constexpr float cell = 4.f;
    constexpr float dot = 1.f;
    constexpr ImU32 keep = 180u; // ~4.4% total pixel coverage after cell sampling.

    // Leave more breathing room above the quote and feather the texture over a longer span.
    const float start = stage.Min.y + stage.GetHeight() * 0.56f;
    const float full = stage.Min.y + stage.GetHeight() * 0.76f;
    const float bottom_fade = band_bottom - px(14.f);
    const float left_full = stage.Min.x + px(24.f);
    const float right_fade = stage.Max.x - px(16.f);

    if (band_bottom <= start || stage.GetWidth() <= 0.f)
        return;

    const int columns = ImMax(1, (int)ceilf(stage.GetWidth() / cell));
    const int rows = ImMax(1, (int)ceilf((band_bottom - start) / cell));
    const bool dark = is_dark();

    static std::vector<grain_dot> dots;
    dots.clear();
    dots.reserve((size_t)(columns * rows * 3 / 4));

    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
            const ImU32 seed = (ImU32)column * 0x9E3779B9u ^ (ImU32)row * 0x85EBCA6Bu ^ 0xD1B54A35u;
            const ImU32 h = grain_hash(seed);
            if ((h & 0xFFu) >= keep)
                continue;

            const float jitter_x = (float)((h >> 8) & 0xFFu) / 255.f * (cell - dot);
            const float jitter_y = (float)((h >> 16) & 0xFFu) / 255.f * (cell - dot);
            const float x = ImFloor(stage.Min.x + (float)column * cell + jitter_x);
            const float y = ImFloor(start + (float)row * cell + jitter_y);

            const float top_envelope = smootherstep((y - start) / ImMax(full - start, 1.f));
            const float bottom_envelope =
                1.f - smootherstep((y - bottom_fade) / ImMax(band_bottom - bottom_fade, 1.f));
            const float left_envelope =
                smootherstep((x - stage.Min.x) / ImMax(left_full - stage.Min.x, 1.f));
            const float right_envelope =
                1.f - smootherstep((x - right_fade) / ImMax(stage.Max.x - right_fade, 1.f));
            const float envelope = top_envelope * bottom_envelope * left_envelope * right_envelope;

            const bool bright = (h & 0x01000000u) != 0;
            const float variation = 0.78f + 0.22f * (float)((h >> 25) & 0x7Fu) / 127.f;
            const float alpha = envelope * variation *
                                (dark ? (bright ? 0.024f : 0.015f) : (bright ? 0.012f : 0.019f));
            const ImU32 col = mo::with_alpha(bright ? IM_COL32_WHITE : IM_COL32_BLACK, alpha);
            if ((col & IM_COL32_A_MASK) != 0)
                dots.push_back({ImVec2(x, y), col});
        }
    }

    dl->PushClipRect(ImVec2(stage.Min.x, start), ImVec2(stage.Max.x, band_bottom), true);
    constexpr int batch_size = 4096;
    for (size_t first = 0; first < dots.size(); first += batch_size)
    {
        const int count = (int)ImMin((size_t)batch_size, dots.size() - first);
        dl->PrimReserve(count * 6, count * 4);
        for (int i = 0; i < count; i++)
        {
            const grain_dot& grain = dots[first + (size_t)i];
            dl->PrimRect(grain.at, grain.at + ImVec2(dot, dot), grain.col);
        }
    }
    dl->PopClipRect();
}

void panel_chip(ImDrawList* dl, const ImRect& box, float alpha)
{
    using namespace solace;
    dl->AddRectFilled(box.Min, box.Max,
                      mo::with_alpha(IM_COL32(0x0A, 0x0A, 0x0C, 0xFF), 0.42f * alpha), px(10.f));
    dl->AddRect(ImVec2(box.Min.x + px(0.5f), box.Min.y + px(0.5f)),
                ImVec2(box.Max.x - px(0.5f), box.Max.y - px(0.5f)),
                mo::with_alpha(IM_COL32_WHITE, 0.14f * alpha), px(10.f), px(1.f), ImDrawFlags_None);
}
} // namespace

void auth_stage(ImDrawList* dl, const ImRect& stage, const ImRect& card, float rounding)
{
    using namespace solace;

    slides::morph_slider_options slider;
    slider.transition = slides::morph_melt;
    slider.overlay = IM_COL32(0x05, 0x06, 0x0A, 0xFF);
    slider.duration = 1.1f;
    slider.intensity = 0.55f;
    slider.scale = 2.4f;
    slider.aberration = 0.35f;
    slider.drift = 0.4f;
    slider.autoplay = true;
    slider.autoplay_delay = 4.f;
    slider.radius = px(16.f);

    slider.fade_left = px(240.f);
    slider.fade_top = px(96.f);
    slider.fade_bottom = px(150.f);

    slider.show_controls = false;
    slider.show_captions = false;
    slider.show_indicators = false;
    slider.show_progress = false;
    slider.pause_on_hover = true;
    slider.keyboard = true;

    const ImRect mask(ImVec2(card.Min.x + px(1.f), card.Min.y + px(1.f)),
                      ImVec2(card.Max.x - px(1.f), card.Max.y - px(1.f)));

    slides::morph_slider(dl, stage, mask, rounding - px(1.f), slider);

    const slides::morph_slider_status& st = slides::morph_slider_state();

    const float pad = px(32.f);
    const float scrim_top = stage.Min.y + stage.GetHeight() * 0.45f;
    const float corner = rounding;

    const float k_floor = 0.46f;
    const float band_bottom = stage.Max.y - corner;

    ramp(dl, ImRect(ImVec2(stage.Min.x, scrim_top), ImVec2(stage.Max.x, band_bottom)), c_background,
         0.f, k_floor, false, 24);

    dl->PushClipRect(ImVec2(stage.Min.x, band_bottom), stage.Max, true);
    dl->AddRectFilled(ImVec2(stage.Min.x, band_bottom - corner), stage.Max,
                      mo::with_alpha(c_background, k_floor), corner,
                      ImDrawFlags_RoundCornersBottomRight);
    dl->PopClipRect();

    testimonial_grain(dl, stage, band_bottom);

    {
        ImFont* f = font_semibold(text_sm);
        const char* name = brand::product;
        const float w = px(20.f) + px(8.f) + text_width(f, name) + px(28.f);
        const ImRect chip(ImVec2(stage.Min.x + pad, stage.Min.y + pad),
                          ImVec2(stage.Min.x + pad + w, stage.Min.y + pad + px(38.f)));

        panel_chip(dl, chip, 1.f);
        icons::draw(icons::id::command, dl,
                    ImVec2(chip.Min.x + px(14.f), chip.GetCenter().y - px(10.f)), px(20.f),
                    IM_COL32(0xFF, 0xFF, 0xFF, 0xFF));
        draw_text(dl, f, ImVec2(chip.Min.x + px(42.f), chip.GetCenter().y - f->LegacySize * 0.5f),
                  IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), name);
    }

    {
        const testimonial& q = k_testimonials[st.shown % k_testimonial_count];

        const float t = mo::EASE_OUT(st.swap);
        const float dy = px(12.f) * (1.f - t);
        const float blur = px(6.f) * (1.f - t);

        const float content_w = stage.GetWidth() - pad * 2.f;

        ImFont* qf = font_medium(20.f);
        const float quote_h = px(28.f) * (float)wrapped_line_count(qf, q.quote, content_w);

        const float block_bottom = stage.Max.y - pad - px(44.f);
        const float quote_y = block_bottom - px(20.f) - quote_h + dy;

        const ImU32 shade = IM_COL32(0x05, 0x06, 0x0A, 0xFF);
        const float drop = px(2.f);

        draw_text_wrapped_blur(dl, qf, ImVec2(stage.Min.x + pad, quote_y + drop),
                               mo::with_alpha(shade, 0.55f * t), q.quote, content_w, px(28.f),
                               blur + px(7.f));

        draw_text_wrapped_blur(dl, qf, ImVec2(stage.Min.x + pad, quote_y),
                               mo::with_alpha(c_foreground, t), q.quote, content_w, px(28.f), blur);

        const float avatar = px(36.f);
        const ImVec2 at(stage.Min.x + pad, block_bottom + px(4.f) + dy);

        if (!avatars::draw(dl, avatars::other(q.face), at, avatar, t))
            dl->AddRectFilled(at, ImVec2(at.x + avatar, at.y + avatar),
                              mo::with_alpha(c_border_strong, t), avatar * 0.5f);

        ImFont* nf = font_medium(text_sm);
        const ImVec2 name_at(at.x + avatar + px(12.f),
                             at.y + px(1.f) + line_top(nf, px(leading_sm)));
        draw_text_blur(dl, nf, ImVec2(name_at.x, name_at.y + drop), mo::with_alpha(shade, 0.5f * t),
                       q.name, blur + px(6.f));
        draw_text_blur(dl, nf, name_at, mo::with_alpha(c_foreground, t), q.name, blur);

        ImFont* rf = font_regular(text_xs);
        const ImVec2 role_at(at.x + avatar + px(12.f),
                             at.y + px(20.f) + line_top(rf, px(leading_xs)));
        draw_text_blur(dl, rf, ImVec2(role_at.x, role_at.y + drop),
                       mo::with_alpha(shade, 0.45f * t), q.role, blur + px(5.f));
        draw_text_blur(dl, rf, role_at, mo::with_alpha(c_muted_foreground, t), q.role, blur);
    }
}

auth_layout::frame auth_layout::begin(const char* name, const ImVec2& card_size, bool interactive)
{
    ui_runtime::host_size = card_size;

    ImGui::SetNextWindowSize(card_size);
    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;
    if (!interactive)
        flags |= ImGuiWindowFlags_NoInputs;

    ImGui::Begin(name, nullptr, flags);
    ui_runtime::apply_style();

    frame result;
    result.window = ImGui::GetCurrentWindow();
    result.draw_list = result.window->DrawList;
    result.card = ImRect(result.window->Pos, result.window->Pos + card_size);
    const ImRect stage(ImVec2(result.card.Max.x - px(stage_width), result.card.Min.y + px(1.f)),
                       ImVec2(result.card.Max.x - px(1.f), result.card.Max.y - px(1.f)));

    rounded_panel::draw(result.draw_list, result.card.Min, result.card.Max, c_background,
                        px(rounded_3xl));
    auth_stage(result.draw_list, stage, result.card, px(rounded_3xl));
    return result;
}

void auth_layout::end()
{
    ImGui::End();
}
} // namespace solace
