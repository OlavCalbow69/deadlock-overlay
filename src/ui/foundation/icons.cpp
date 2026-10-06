#include "ui/foundation/icons.h"
#include "ui/foundation/svg.h"

namespace solace::icons
{
namespace
{
constexpr float k_view_box = 24.f;
constexpr float k_stroke = 2.f;

float unit(float box)
{
    return box / k_view_box;
}
} // namespace

void stroke_path(ImDrawList* dl, const char* d, const ImVec2& top_left, float box, ImU32 col,
                 float stroke_width, float fraction)
{
    svg::stroke(dl, svg::path(d), top_left, unit(box), col, stroke_width, fraction);
}

void user(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl, svg::path("M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2"), tl, s, col, k_stroke);
    svg::stroke(dl, svg::circle(12, 7, 4), tl, s, col, k_stroke);
}

void mail(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl, svg::rounded_rect(2, 4, 20, 16, 2), tl, s, col, k_stroke);
    svg::stroke(dl, svg::path("m22 7-8.991 5.727a2 2 0 0 1-2.009 0L2 7"), tl, s, col, k_stroke);
}

void lock(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl, svg::rounded_rect(3, 11, 18, 11, 2), tl, s, col, k_stroke);
    svg::stroke(dl, svg::path("M7 11V7a5 5 0 0 1 10 0v4"), tl, s, col, k_stroke);
}

void eye(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl,
                svg::path("M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 "
                          ".696 10.75 10.75 0 0 1-19.876 0"),
                tl, s, col, k_stroke);
    svg::stroke(dl, svg::circle(12, 12, 3), tl, s, col, k_stroke);
}

void eye_off(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl,
                svg::path("M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 "
                          "10.747 0 0 1-1.444 2.49"),
                tl, s, col, k_stroke);
    svg::stroke(dl, svg::path("M14.084 14.158a3 3 0 0 1-4.242-4.242"), tl, s, col, k_stroke);
    svg::stroke(dl,
                svg::path("M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 "
                          "10.75 0 0 1 4.446-5.143"),
                tl, s, col, k_stroke);
    svg::stroke(dl, svg::path("m2 2 20 20"), tl, s, col, k_stroke);
}

void check(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    svg::stroke(dl, svg::path("M20 6 9 17l-5-5"), tl, unit(box), col, k_stroke);
}

void cross(ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    svg::stroke(dl, svg::path("M18 6 6 18"), tl, s, col, k_stroke);
    svg::stroke(dl, svg::path("m6 6 12 12"), tl, s, col, k_stroke);
}

void loader(ImDrawList* dl, const ImVec2& center, float box, ImU32 col, float angle)
{

    const float s = unit(box);
    const float radius = 9.f * s;
    const float thickness = k_stroke * s;

    constexpr int k_segments = 48;
    constexpr float k_sweep = 288.f * (IM_PI / 180.f);

    ImVec2 points[k_segments + 1];
    for (int i = 0; i <= k_segments; i++)
    {
        const float a = angle + k_sweep * ((float)i / (float)k_segments);
        points[i] = ImVec2(center.x + ImCos(a) * radius, center.y + ImSin(a) * radius);
    }

    dl->AddPolyline(points, k_segments + 1, col, thickness, ImDrawFlags_None);
    dl->AddCircleFilled(points[0], thickness * 0.5f, col, 12);
    dl->AddCircleFilled(points[k_segments], thickness * 0.5f, col, 12);
}

void draw(id which, ImDrawList* dl, const ImVec2& tl, float box, ImU32 col)
{
    const float s = unit(box);
    auto path = [&](const char* d) { svg::stroke(dl, svg::path(d), tl, s, col, k_stroke); };
    auto circle = [&](float cx, float cy, float r)
    { svg::stroke(dl, svg::circle(cx, cy, r), tl, s, col, k_stroke); };
    auto rect = [&](float x, float y, float w, float h, float rx)
    { svg::stroke(dl, svg::rounded_rect(x, y, w, h, rx), tl, s, col, k_stroke); };

    switch (which)
    {
    case id::user:
        user(dl, tl, box, col);
        break;
    case id::mail:
        mail(dl, tl, box, col);
        break;
    case id::lock:
        lock(dl, tl, box, col);
        break;
    case id::eye:
        eye(dl, tl, box, col);
        break;
    case id::eye_off:
        eye_off(dl, tl, box, col);
        break;
    case id::check:
        check(dl, tl, box, col);
        break;
    case id::cross:
        cross(dl, tl, box, col);
        break;

    case id::search:
        path("m21 21-4.34-4.34");
        circle(11, 11, 8);
        break;

    case id::sparkles:
        path("M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 "
             "0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 "
             "0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 "
             "0 0 0 1.594-1.594z");
        path("M20 2v4");
        path("M22 4h-4");
        circle(4, 20, 2);
        break;

    case id::inbox:
        path("M22 12L16 12L14 15L10 15L8 12L2 12");
        path("M5.45 5.11 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 "
             "4H7.24a2 2 0 0 0-1.79 1.11z");
        break;

    case id::circle_user_round:
        path("M17.925 20.056a6 6 0 0 0-11.851.001");
        circle(12, 11, 4);
        circle(12, 12, 10);
        break;

    case id::building_2:
        path("M10 12h4");
        path("M10 8h4");
        path("M14 21v-3a2 2 0 0 0-4 0v3");
        path("M6 10H4a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-2");
        path("M6 21V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v16");
        break;

    case id::target:
        circle(12, 12, 10);
        circle(12, 12, 6);
        circle(12, 12, 2);
        break;

    case id::crosshair_simple:
    {
        // Phosphor regular crosshair-simple, rendered as native vector geometry.
        // Original 256-unit SVG and MIT notice: assets/icons/phosphor/.
        const float scale = box / 256.f;
        const ImVec2 center = tl + ImVec2(128.f, 128.f) * scale;
        dl->AddCircle(center, 96.f * scale, col, 48, 16.f * scale);
        auto arm = [&](ImVec2 a, ImVec2 b)
        {
            a = tl + a * scale;
            b = tl + b * scale;
            dl->AddLine(a, b, col, 16.f * scale);
            dl->AddCircleFilled(a, 8.f * scale, col, 12);
            dl->AddCircleFilled(b, 8.f * scale, col, 12);
        };
        arm({128.f, 32.f}, {128.f, 72.f});
        arm({128.f, 184.f}, {128.f, 224.f});
        arm({32.f, 128.f}, {72.f, 128.f});
        arm({184.f, 128.f}, {224.f, 128.f});
        break;
    }

    case id::list_todo:
        path("M13 5h8");
        path("M13 12h8");
        path("M13 19h8");
        path("m3 17 2 2 4-4");
        rect(3, 4, 6, 6, 1);
        break;

    case id::notebook_tabs:
        path("M2 6h4");
        path("M2 10h4");
        path("M2 14h4");
        path("M2 18h4");
        rect(4, 2, 16, 20, 2);
        path("M15 2v20");
        path("M15 7h5");
        path("M15 12h5");
        path("M15 17h5");
        break;

    case id::workflow:
        rect(3, 3, 8, 8, 2);
        path("M7 11v4a2 2 0 0 0 2 2h4");
        rect(13, 13, 8, 8, 2);
        break;

    case id::layout_grid:
        rect(3, 3, 7, 7, 1);
        rect(14, 3, 7, 7, 1);
        rect(14, 14, 7, 7, 1);
        rect(3, 14, 7, 7, 1);
        break;

    case id::chevron_right:
        path("m9 18 6-6-6-6");
        break;

    case id::chevron_down:
        path("m6 9 6 6 6-6");
        break;

    case id::chevrons_up_down:
        path("m7 15 5 5 5-5");
        path("m7 9 5-5 5 5");
        break;

    case id::panel_left:
        rect(3, 3, 18, 18, 2);
        path("M9 3v18");
        break;

    case id::sun:
        circle(12, 12, 4);
        path("M12 2v2");
        path("M12 20v2");
        path("m4.93 4.93 1.41 1.41");
        path("m17.66 17.66 1.41 1.41");
        path("M2 12h2");
        path("M20 12h2");
        path("m6.34 17.66-1.41 1.41");
        path("m19.07 4.93-1.41 1.41");
        break;

    case id::moon:
        path("M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 "
             "8.268c.344-.215.825-.004.803.401");
        break;

    case id::bell:
        path("M10.268 21a2 2 0 0 0 3.464 0");
        path("M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 18 8A6 6 "
             "0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326");
        break;

    case id::info:
        circle(12, 12, 10);
        path("M12 16v-4");
        path("M12 8h.01");
        break;

    case id::circle_alert:
        circle(12, 12, 10);
        path("M12 8v4");
        path("M12 16h.01");
        break;

    case id::clock:
        circle(12, 12, 10);
        path("M12 6v6l4 2");
        break;

    case id::star:
        path("M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 "
             "1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 "
             "5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 "
             "0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 "
             ".294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z");
        break;

    case id::settings:
        path("M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 "
             "2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 "
             "1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 "
             "2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915");
        circle(12, 12, 3);
        break;

    case id::user_plus:
        path("M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2");
        circle(9, 7, 4);
        path("M19 8v6");
        path("M22 11h-6");
        break;

    case id::log_out:
        path("m16 17 5-5-5-5");
        path("M21 12H9");
        path("M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4");
        break;

    case id::plus:
        path("M5 12h14");
        path("M12 5v14");
        break;

    case id::command:
        path("M15 6v12a3 3 0 1 0 3-3H6a3 3 0 1 0 3 3V6a3 3 0 1 0-3 3h12a3 3 0 1 0-3-3");
        break;

    default:
        break;
    }
}
} // namespace solace::icons
