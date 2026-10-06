#include "ui/controls/morph_slider.h"
#include "assets/images.h"
#include "graphics/dx11_helpers.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/theme.h"

#include <cstring>

namespace solace::slides
{
namespace
{

constexpr char k_pixel_shader[] = R"HLSL(
cbuffer Params : register(b0)
{
    float4 u_rect;
    float4 u_card;
    float4 u_progress;
    float4 u_look;
    float4 u_sizes;
    float4 u_pointer;
    float4 u_overlay;
    float4 u_fade;
};

Texture2D    tCurrent : register(t1);
Texture2D    tNext    : register(t2);
SamplerState sSlide   : register(s1);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

static const float PI = 3.14159265359;

float hash11(float p)
{
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float hash21(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p)
{
    float v = 0.0;
    float a = 0.5;
    [unroll] for (int i = 0; i < 5; i++)
    {
        v += a * noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

float2 rot_mul(float a, float2 v)
{
    float s = sin(a), c = cos(a);
    return float2(c * v.x + s * v.y, -s * v.x + c * v.y);
}

float2 coverUV(float2 uv, float2 res, float2 img)
{
    float rA = res.x / max(res.y, 1.0);
    float iA = img.x / max(img.y, 1.0);
    float2 s = float2(1.0, 1.0);
    float ratio = rA / max(iA, 0.0001);
    if (ratio > 1.0) s.y = 1.0 / ratio;
    else             s.x = ratio;
    return (uv - 0.5) * s + 0.5;
}

float smootherstep(float t)
{
    t = saturate(t);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float rounded_box(float2 p, float2 half_size, float r)
{
    float2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float2 animated_uv(PS_INPUT input)
{
    float2 uResolution = u_rect.zw;
    float2 local = input.pos.xy - u_rect.xy;
    float2 vUv = float2(local.x / uResolution.x, 1.0 - local.y / uResolution.y);
    float2 uv = vUv;
    uv += float2(sin(u_look.w * 0.25 + uv.y * 4.0),
        cos(u_look.w * 0.22 + uv.x * 4.0)) * u_look.z * 0.008;
    return (uv - 0.5) * (1.0 - u_look.z * 0.02 * sin(u_look.w * 0.4)) + 0.5;
}

float4 finish_pixel(PS_INPUT input, float2 uv, float3 col)
{
    float vig = smoothstep(1.25, 0.25, length(uv - 0.5));
    col = lerp(col, u_overlay.rgb, (1.0 - vig) * 0.28);

    float2 half_size = u_card.zw * 0.5;
    float2 centre = u_card.xy + half_size;
    float d = rounded_box(input.pos.xy - centre, half_size, u_pointer.z);
    float inside = 1.0 - smoothstep(-0.75, 0.75, d);

    float cover = 0.0;
    if (u_fade.x > 0.5) cover = max(cover, 1.0 - smootherstep((input.pos.x - u_rect.x) / u_fade.x));
    if (u_fade.y > 0.5) cover = max(cover, 1.0 - smootherstep((input.pos.y - u_rect.y) / u_fade.y));
    if (u_fade.z > 0.5) cover = max(cover, 1.0 - smootherstep(((u_rect.y + u_rect.w) - input.pos.y) / u_fade.z));

    float a = inside * (1.0 - cover) * u_pointer.w * input.col.a;
    if (a <= 0.002)
        discard;

    return float4(col, a);
}

float4 main(PS_INPUT input) : SV_Target
{
    float  uProgress   = u_progress.x;
    float  uDir        = u_progress.y;
    int    uMode       = (int)u_progress.z;
    float  uIntensity  = u_progress.w;
    float  uScale      = u_look.x;
    float  uAberration = u_look.y;
    float  uTime       = u_look.w;
    float2 uResolution = u_rect.zw;
    float2 uPointer    = u_pointer.xy;
    float2 uv = animated_uv(input);

    float p = clamp(uProgress, 0.0, 1.0);
    float env = sin(p * PI);
    float2 uvC = uv;
    float2 uvN = uv;
    float m = smoothstep(0.0, 1.0, p);

    if (uMode == 3)
    {
        float2 c = uv - 0.5;
        float r = length(c);
        float ang = env * uIntensity * 3.5 * (1.0 - r);
        uvC = rot_mul(ang, c) + 0.5;
        uvN = rot_mul(-ang, c) + 0.5;
        m = smoothstep(0.0, 1.0, p);
    }
    else if (uMode == 1)
    {
        float d = distance(uv, uPointer);
        float ring = p * 1.6;
        float wave = sin((d - ring) * 30.0) * env;
        float2 dir = normalize(uv - uPointer + 1e-4);
        float2 disp = dir * wave * uIntensity * 0.25;
        uvC = uv + disp;
        uvN = uv + disp * 0.6;
        m = 1.0 - smoothstep(ring - 0.03, ring + 0.03, d);
    }
    else if (uMode == 2)
    {
        float slices = 14.0;
        float row = floor(uv.y * slices);
        float rnd = hash11(row);
        float2 disp = float2((rnd - 0.5) * env * uIntensity * 0.6, 0.0);
        uvC = uv + disp;
        uvN = uv + disp;
        float localX = uDir > 0.0 ? uv.x : 1.0 - uv.x;
        float th = p * 1.5 - 0.25 + (rnd - 0.5) * 0.25;
        m = 1.0 - smoothstep(th - 0.06, th + 0.06, localX);
    }
    else
    {
        float nn = fbm(uv * uScale + uTime * 0.03);
        float warp = fbm(uv * uScale * 1.7 - uTime * 0.02);
        float2 g = float2(nn, warp) - 0.5;
        uvC = uv + g * uIntensity * 0.5 * p;
        uvN = uv - g * uIntensity * 0.5 * (1.0 - p);
        m = smoothstep(nn - 0.15, nn + 0.15, p);
    }

    float2 sC = coverUV(uvC, uResolution, u_sizes.xy);
    float2 sN = coverUV(uvN, uResolution, u_sizes.zw);
    float ca = uAberration * env * 0.03;
    float3 colC = float3(
        tCurrent.SampleLevel(sSlide, sC + float2(ca, 0.0), 0).r,
        tCurrent.SampleLevel(sSlide, sC, 0).g,
        tCurrent.SampleLevel(sSlide, sC - float2(ca, 0.0), 0).b);
    float3 colN = float3(
        tNext.SampleLevel(sSlide, sN + float2(ca, 0.0), 0).r,
        tNext.SampleLevel(sSlide, sN, 0).g,
        tNext.SampleLevel(sSlide, sN - float2(ca, 0.0), 0).b);

    return finish_pixel(input, uv, lerp(colC, colN, m));
}

float4 main_settled(PS_INPUT input) : SV_Target
{
    // Completion guarantees progress == 1 and current == next. The aberration
    // envelope is therefore zero, so two RGB samples preserve the settled melt.
    float2 uv = animated_uv(input);
    float nn = fbm(uv * u_look.x + u_look.w * 0.03);
    float warp = fbm(uv * u_look.x * 1.7 - u_look.w * 0.02);
    float2 g = float2(nn, warp) - 0.5;
    float2 uvC = uv + g * u_progress.w * 0.5;
    float m = smoothstep(nn - 0.15, nn + 0.15, 1.0);
    float2 sC = coverUV(uvC, u_rect.zw, u_sizes.xy);
    float2 sN = coverUV(uv, u_rect.zw, u_sizes.xy);
    float3 colC = tCurrent.SampleLevel(sSlide, sC, 0).rgb;
    float3 colN = tCurrent.SampleLevel(sSlide, sN, 0).rgb;

    return finish_pixel(input, uv, lerp(colC, colN, m));
}
)HLSL";

struct constants
{
    float rect[4];
    float card[4];
    float progress[4];
    float look[4];
    float sizes[4];
    float pointer[4];
    float overlay[4];
    float fade[4];
};

struct render_command
{
    constants values{};
    ID3D11ShaderResourceView* current = nullptr;
    ID3D11ShaderResourceView* next = nullptr;
    bool settled_melt = false;
};

dx11::pixel_shader_pass g_transition_shader;
dx11::pixel_shader_pass g_settled_shader;

struct slider_state
{
    int current = 0;
    int next = 0;
    int dir = 1;
    float progress = 1.f;
    bool animating = false;
    float since_change = 0.f;
    ImVec2 pointer{0.5f, 0.5f};

    float caption_t = 1.f;

    bool hovered = false;
};

slider_state& state()
{
    static slider_state s;
    return s;
}

morph_slider_status& status()
{
    static morph_slider_status st;
    return st;
}

float power2_in_out(float t)
{
    t = ImClamp(t, 0.f, 1.f);
    return (t < 0.5f) ? 4.f * t * t * t : 1.f - powf(-2.f * t + 2.f, 3.f) * 0.5f;
}

void callback(const ImDrawList*, const ImDrawCmd* cmd)
{
    if (!cmd || !cmd->UserCallbackData ||
        cmd->UserCallbackDataSize != static_cast<int>(sizeof(render_command)))
        return;

    render_command command{};
    std::memcpy(&command, cmd->UserCallbackData, sizeof(command));
    const dx11::pixel_shader_pass& shader =
        command.settled_melt ? g_settled_shader : g_transition_shader;
    if (!shader.upload_constants(&command.values, sizeof(command.values)))
        return;

    ID3D11ShaderResourceView* views[2] = {command.current, command.next};
    shader.bind(views, command.settled_melt ? 1u : 2u);
}

void set_colour(float out[4], ImU32 col)
{
    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
    out[3] = c.w;
}
} // namespace

bool morph_slider_init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (g_transition_shader.ready() && g_settled_shader.ready())
        return true;

    g_transition_shader.reset();
    g_settled_shader.reset();
    if (!g_transition_shader.initialize(device, context, k_pixel_shader, sizeof(k_pixel_shader) - 1,
                                        "morph slider transition shader", sizeof(constants),
                                        true) ||
        !g_settled_shader.initialize(device, context, k_pixel_shader, sizeof(k_pixel_shader) - 1,
                                     "morph slider settled shader", sizeof(constants), true,
                                     "main_settled"))
    {
        g_transition_shader.reset();
        g_settled_shader.reset();
        return false;
    }
    return true;
}

const morph_slider_status& morph_slider_state()
{
    return status();
}

static void morph_slider_progress(ImDrawList* dl, const ImRect& rect, float right_inset, float t,
                                  ImU32 track, ImU32 fill)
{
    using namespace solace;

    const float h = px(2.f);
    const float x0 = rect.Min.x;
    const float x1 = rect.Max.x - right_inset;
    const float y = rect.Max.y - h;

    if (x1 <= x0)
        return;

    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), track);
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * ImClamp(t, 0.f, 1.f), y + h), fill);
}

void morph_slider_shutdown()
{
    g_settled_shader.reset();
    g_transition_shader.reset();
    state() = slider_state{};
    status() = morph_slider_status{};
}

void morph_slider(ImDrawList* dl, const ImRect& rect, const ImRect& card, float card_rounding,
                  const morph_slider_options& o)
{
    using namespace solace;

    const std::vector<images::texture>& slides = images::ready();
    const int count = (int)slides.size();
    if (!g_transition_shader.ready() || !g_settled_shader.ready() || !dl || count == 0)
        return;

    const ImVec2 size = rect.GetSize();
    if (size.x <= 1.f || size.y <= 1.f)
        return;

    slider_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    s.current = ImClamp(s.current, 0, count - 1);
    s.next = ImClamp(s.next, 0, count - 1);

    auto go = [&](int direction)
    {
        if (s.animating || count < 2)
            return;

        int target = s.next + direction;
        if (!o.loop && (target < 0 || target >= count))
            return;
        target = ((target % count) + count) % count;

        s.current = s.next;
        s.next = target;
        s.dir = direction;
        s.progress = 0.f;
        s.animating = true;
        s.since_change = 0.f;
        s.caption_t = 0.f;
    };

    if (s.animating)
    {
        s.progress += dt / ImMax(o.duration, 0.05f);
        if (s.progress >= 1.f)
        {
            s.progress = 1.f;
            s.animating = false;
            s.current = s.next;
        }
    }
    else if (!(o.pause_on_hover && s.hovered))
    {
        s.since_change += dt;
        if (o.autoplay && count > 1 && s.since_change >= ImMax(o.autoplay_delay, 1.f))
            go(1);
    }

    s.caption_t = ImMin(s.caption_t + dt / ImMax(o.duration * 0.66f, 0.05f), 1.f);

    ImGui::PushID("morph-slider");
    const ImGuiID stage_id = window->GetID("stage");
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(rect, stage_id);

    bool hovered = false, held = false;
    ImGui::ButtonBehavior(rect, stage_id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
    s.hovered = hovered;

    if (o.keyboard && hovered && count > 1)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            go(1);
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            go(-1);
    }

    if (hovered)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        s.pointer = ImVec2((mouse.x - rect.Min.x) / size.x, 1.f - (mouse.y - rect.Min.y) / size.y);
    }

    if (held && ImFabs(ImGui::GetIO().MouseDelta.x) > 0.f)
    {
        const float drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
        if (ImFabs(drag) > px(48.f))
        {
            go(drag < 0.f ? 1 : -1);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
    }

    const images::texture& tc = slides[ImClamp(s.current, 0, count - 1)];
    const images::texture& tn = slides[ImClamp(s.next, 0, count - 1)];

    render_command command{};
    command.current = reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<intptr_t>(tc.id));
    command.next = reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<intptr_t>(tn.id));
    constants& values = command.values;

    values.rect[0] = rect.Min.x;
    values.rect[1] = rect.Min.y;
    values.rect[2] = size.x;
    values.rect[3] = size.y;

    values.card[0] = card.Min.x;
    values.card[1] = card.Min.y;
    values.card[2] = card.GetWidth();
    values.card[3] = card.GetHeight();

    values.progress[0] = power2_in_out(s.progress);
    values.progress[1] = (float)s.dir;
    values.progress[2] = (float)o.transition;
    values.progress[3] = o.intensity;

    values.look[0] = o.scale;
    values.look[1] = o.aberration;
    values.look[2] = o.drift;
    values.look[3] = (float)ImGui::GetTime();

    values.sizes[0] = (float)ImMax(tc.width, 1);
    values.sizes[1] = (float)ImMax(tc.height, 1);
    values.sizes[2] = (float)ImMax(tn.width, 1);
    values.sizes[3] = (float)ImMax(tn.height, 1);

    values.pointer[0] = s.pointer.x;
    values.pointer[1] = s.pointer.y;
    values.pointer[2] = card_rounding;
    values.pointer[3] = 1.f;

    set_colour(values.overlay, o.overlay);
    values.fade[0] = o.fade_left;
    values.fade[1] = o.fade_top;
    values.fade[2] = o.fade_bottom;
    const bool settled_melt =
        !s.animating && s.progress >= 1.f && s.current == s.next && o.transition == morph_melt;
    values.fade[3] = 0.f;
    command.settled_melt = settled_melt;

    dl->AddCallback(callback, &command, sizeof(command));
    dl->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    const int shown = s.animating ? s.next : s.current;

    {
        morph_slider_status& st = status();
        st.shown = shown;
        st.previous = s.animating ? s.current : shown;
        st.count = count;
        st.animating = s.animating;
        st.progress = power2_in_out(s.progress);
        st.swap = s.caption_t;
        st.until_next = (o.autoplay && count > 1)
                            ? ImClamp(s.since_change / ImMax(o.autoplay_delay, 1.f), 0.f, 1.f)
                            : 0.f;
    }

    if (o.show_progress && count > 1)
        morph_slider_progress(dl, rect, card_rounding, status().until_next,
                              mo::with_alpha(IM_COL32_WHITE, 0.12f),
                              mo::with_alpha(IM_COL32_WHITE, 0.55f));

    if (o.show_captions && !slides[shown].name.empty())
    {

        const float t = mo::EASE_OUT(s.caption_t);
        const float dy = px(12.f) * (1.f - t);
        const float blur = px(6.f) * (1.f - t);

        ImFont* f = font_semibold(15.f);
        const float tw = text_width(f, slides[shown].name.c_str());
        const ImVec2 at(rect.Min.x + px(22.f), rect.Max.y - px(22.f) - px(37.f) + dy);
        const ImRect pill(at, ImVec2(at.x + tw + px(28.f), at.y + px(37.f)));

        dl->AddRectFilled(pill.Min, pill.Max,
                          mo::with_alpha(IM_COL32(0x0A, 0x0A, 0x0C, 0xFF), 0.42f * t), px(10.f));
        draw_text_blur(dl, f, ImVec2(at.x + px(14.f), pill.GetCenter().y - f->LegacySize * 0.5f),
                       mo::with_alpha(IM_COL32_WHITE, t), slides[shown].name.c_str(), blur);
    }

    if (o.show_controls && count > 1)
    {

        const float button = px(40.f);
        const float cy = rect.GetCenter().y;

        struct arrow
        {
            const char* id;
            float x;
            int direction;
            const char* path;
        };
        const arrow arrows[2] = {
            {"prev", rect.Min.x + px(16.f), -1, "M15 5l-7 7 7 7"},
            {"next", rect.Max.x - px(16.f) - button, 1, "M9 5l7 7-7 7"},
        };

        for (const arrow& a : arrows)
        {
            const ImRect bb(ImVec2(a.x, cy - button * 0.5f),
                            ImVec2(a.x + button, cy + button * 0.5f));

            ImGui::PushID(a.id);
            const ImGuiID id = window->GetID("b");
            ImGui::SetCursorScreenPos(bb.Min);
            ImGui::ItemSize(ImVec2(0, 0));
            ImGui::ItemAdd(bb, id);
            bool bh = false, bheld = false;
            const bool pressed = ImGui::ButtonBehavior(bb, id, &bh, &bheld);
            ImGui::PopID();

            if (pressed)
                go(a.direction);

            const float scale = bheld ? 0.96f : (bh ? 1.06f : 1.f);
            const ImVec2 c = bb.GetCenter();
            const float r = button * 0.5f * scale;

            dl->AddCircleFilled(
                c, r, mo::with_alpha(IM_COL32(0x0C, 0x0C, 0x0E, 0xFF), bh ? 0.6f : 0.4f), 32);
            dl->AddCircle(c, r - px(0.5f), mo::with_alpha(IM_COL32_WHITE, bh ? 0.5f : 0.22f), 32,
                          px(1.f));

            const float glyph = px(18.f);
            icons::stroke_path(dl, a.path, ImVec2(c.x - glyph * 0.5f, c.y - glyph * 0.5f), glyph,
                               IM_COL32_WHITE, 2.f);
        }
    }

    if (o.show_indicators && count > 1)
    {

        const float dot = px(8.f);
        const float gap = px(8.f);

        float total = 0.f;
        for (int i = 0; i < count; i++)
            total += (i == shown ? px(22.f) : dot) + (i + 1 < count ? gap : 0.f);

        float x = o.indicators_right ? rect.Max.x - px(o.indicator_pad_x) - total
                                     : rect.GetCenter().x - total * 0.5f;
        const float y = rect.Max.y - px(o.indicator_pad_y) - dot;

        for (int i = 0; i < count; i++)
        {
            const bool active = (i == shown);
            const float w = active ? px(22.f) : dot;
            const ImRect bb(ImVec2(x, y), ImVec2(x + w, y + dot));

            ImGui::PushID(1000 + i);
            const ImGuiID id = window->GetID("dot");
            ImGui::SetCursorScreenPos(bb.Min);
            ImGui::ItemSize(ImVec2(0, 0));
            ImGui::ItemAdd(bb, id);
            bool dh = false, dheld = false;
            if (ImGui::ButtonBehavior(bb, id, &dh, &dheld) && i != shown && !s.animating)
            {
                s.current = s.next;
                s.next = i;
                s.dir = (i > s.current) ? 1 : -1;
                s.progress = 0.f;
                s.animating = true;
                s.since_change = 0.f;
                s.caption_t = 0.f;
            }
            ImGui::PopID();

            dl->AddRectFilled(bb.Min, bb.Max,
                              mo::with_alpha(IM_COL32_WHITE, active ? 0.95f : (dh ? 0.6f : 0.35f)),
                              dot * 0.5f);

            x += w + gap;
        }
    }

    ImGui::PopID();
}
} // namespace solace::slides
