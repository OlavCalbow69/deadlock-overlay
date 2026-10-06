#include "ui/effects/glass_cursor.h"
#include "graphics/dx11_helpers.h"
#include "graphics/snapshot.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/theme.h"

#include <cstring>
#include <math.h>
#include <vector>

namespace solace::glass
{
namespace
{
constexpr int k_max_points = 64;

constexpr char k_pixel_shader[] = R"HLSL(
cbuffer glass_cb : register(b0)
{
    float4 u_res;
    float4 u_shape;
    float4 u_glass;
    float4 u_glass2;
    float4 u_warp;
    float4 u_bg;
    float4 u_points[64];
};

Texture2D    tBackdrop : register(t1);
SamplerState sBackdrop : register(s1);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float vnoise(float2 p)
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

float fbm2(float2 p)
{
    return vnoise(p) * 0.65 + vnoise(p * 2.03 + 11.7) * 0.35;
}

float2 warp_point(float2 p)
{
    float2 q = p * u_warp.y;
    float t = u_warp.z;
    float2 n = float2(fbm2(q + float2(0.0, t)), fbm2(q + float2(37.4, -t)));
    return p + (n - 0.5) * u_warp.x;
}

float link_sdf(float2 p, float4 a, float4 b)
{
    float2 ba = b.xy - a.xy;
    float  l2 = dot(ba, ba);

    if (l2 < 1e-5)
        return length(p - a.xy) - max(a.z, b.z);

    float  rr = a.z - b.z;
    float  a2 = l2 - rr * rr;
    float  il2 = 1.0 / l2;

    float2 pa = p - a.xy;
    float  y = dot(pa, ba);
    float  z = y - l2;

    float2 w = pa * l2 - ba * y;
    float  x2 = dot(w, w);
    float  y2 = y * y * l2;
    float  z2 = z * z * l2;

    float  k = sign(rr) * rr * rr * x2;

    if (sign(z) * a2 * z2 > k) return sqrt(x2 + z2) * il2 - b.z;
    if (sign(y) * a2 * y2 < k) return sqrt(x2 + y2) * il2 - a.z;
    return (sqrt(max(x2 * a2 * il2, 0.0)) + y * rr) * il2 - a.z;
}

float trail_sdf(float2 p)
{
    float2 w = warp_point(p);
    int   n = (int)u_shape.w;

    float d = 1e9;
    [loop] for (int i = 0; i < n - 1; i++)
        d = min(d, link_sdf(w, u_points[i], u_points[i + 1]));

    return d + u_shape.y;
}

float erf_approx(float x)
{
    return tanh(1.7724538509 * x);
}

float dome_height(float inside, float blob, float zR)
{
    if (inside <= 0.0) return 0.0;

    float s = max(blob - min(inside, blob), 0.0);
    float R = (blob * blob + zR * zR) / (2.0 * zR);
    return sqrt(max(R * R - s * s, 0.0)) - (R - zR);
}

float4 sample_bg(float2 uv)
{
    return tBackdrop.SampleLevel(sBackdrop, uv, 0);
}

static const float2 k_ring[12] =
{
    float2( 1.000,  0.000), float2( 0.500,  0.866), float2(-0.500,  0.866),
    float2(-1.000,  0.000), float2(-0.500, -0.866), float2( 0.500, -0.866),
    float2( 0.383,  0.321), float2(-0.117,  0.484), float2(-0.484,  0.117),
    float2(-0.383, -0.321), float2( 0.117, -0.484), float2( 0.484, -0.117),
};

float4 sample_blur(float2 uv, float radius_px)
{
    if (radius_px < 0.5) return sample_bg(uv);

    float2 s = radius_px * u_res.zw;
    float4 acc = sample_bg(uv) * 0.16;

    [unroll] for (int i = 0; i < 6; i++)
        acc += sample_bg(uv + k_ring[i] * s) * 0.05;
    [unroll] for (int j = 6; j < 12; j++)
        acc += sample_bg(uv + k_ring[j] * s) * 0.09;

    return acc;
}

float4 main(PS_INPUT input) : SV_Target
{
    float2 px  = input.pos.xy;
    float  sdf = trail_sdf(px);

    float mask = 1.0 - smoothstep(-2.5, 1.0, sdf);
    if (mask <= 0.002)
        discard;

    float blob   = u_shape.x;
    float inside = -sdf;
    float edge   = smoothstep(blob * 0.35, 0.0, inside);

    float zR = blob * 0.75;
    float e  = 2.0;
    float dC = inside;
    float dR = -trail_sdf(px + float2(e, 0.0));
    float dL = -trail_sdf(px - float2(e, 0.0));
    float dU = -trail_sdf(px + float2(0.0, e));
    float dD = -trail_sdf(px - float2(0.0, e));

    float hC = dome_height(dC, blob, zR);
    float hR = dome_height(dR, blob, zR);
    float hL = dome_height(dL, blob, zR);
    float hU = dome_height(dU, blob, zR);
    float hD = dome_height(dD, blob, zR);

    float2 hGrad = float2(hR - hL, hU - hD) / (2.0 * e);
    float3 N = normalize(float3(-hGrad, 1.0));

    float depth = smoothstep(0.0, zR, inside);

    float2 pxToUV = u_res.zw;
    float  ior = 1.5;
    float  refrPow = 1.0 - 1.0 / ior;
    float  thickness = hC * 2.0;
    float  thickNorm = thickness / max(zR * 2.0, 1.0);

    float2 exitRefr = hGrad * refrPow;
    float2 entryRefr = hGrad * refrPow;
    float2 throughRefr = entryRefr * thickNorm * 0.5;
    float2 refrPx = (exitRefr + entryRefr + throughRefr) * u_glass.x * 30.0;

    float falloff = erf_approx(inside / max(blob * 0.5 * 1.41421356, 1e-3));
    float rimLip = pow(edge, 10.0);

    refrPx *= falloff * (1.0 - 0.35 * depth * depth);
    refrPx += hGrad * rimLip * u_glass.x * 42.0;

    float2 gradSdf = float2(dL - dR, dD - dU) / (2.0 * e);
    float2 centerDir = (length(gradSdf) > 1e-5) ? -normalize(gradSdf) : float2(0.0, 0.0);
    refrPx += centerDir * (1.0 - depth) * u_glass.x * 4.0 * depth;

    float2 refr = refrPx * pxToUV;

    float  caS = u_glass2.x * 18.0 * (edge * 0.7 + 0.3) * 2.0;
    float2 caD = N.xy * caS * pxToUV;

    float2 flat_uv = px * pxToUV;
    float2 base = flat_uv + refr;
    float  oob = max(max(-base.x, base.x - 1.0), max(-base.y, base.y - 1.0));
    base = lerp(base, flat_uv, smoothstep(0.0, 0.012, oob));

    float4 sG = sample_bg(base);
    float3 sharp = float3(sample_bg(base + caD).r, sG.g, sample_bg(base - caD).b);

    float4 bG = sample_blur(base, u_glass.y);

    float frost = saturate(u_glass2.x * 0.0 + u_shape.z);
    float edgeMix = (0.20 + 0.80 * edge) * frost;
    float3 col = lerp(sharp, bG.rgb, edgeMix);

    float bga = lerp(sG.a, bG.a, edgeMix);
    col = lerp(u_bg.rgb, col, saturate(bga));

    col *= 1.0 + u_glass2.w;

    float lum = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(float3(lum, lum, lum), col, 1.0 + u_glass2.z);

    col = lerp(col, col * float3(0.92, 0.95, 1.05), u_glass2.y);
    col *= 1.0 + 0.06 * depth;

    float fres = pow(1.0 - abs(N.z), 4.0);

    float3 V = float3(0.0, 0.0, 1.0);
    float3 L1 = normalize(float3(0.4, 0.7, 1.0));
    float3 H1 = normalize(L1 + V);
    float sp1 = pow(max(dot(N, H1), 0.0), 90.0);
    float3 L2 = normalize(float3(-0.3, -0.5, 1.0));
    float3 H2 = normalize(L2 + V);
    float sp2 = pow(max(dot(N, H2), 0.0), 50.0) * 0.3;
    float3 L3 = normalize(float3(0.1, 0.3, 1.0));
    float spB = pow(max(dot(N, L3), 0.0), 6.0) * 0.1;
    float3 L4 = normalize(float3(0.0, 0.9, 0.4));
    float3 H4 = normalize(L4 + V);
    float sp4 = pow(max(dot(N, H4), 0.0), 120.0) * 0.6;
    float totalSpec = (sp1 + sp2 + spB + sp4) * u_glass.w;

    float borderWidth = 1.5;
    float innerStroke = smoothstep(-borderWidth - 1.0, -borderWidth, sdf)
                      * (1.0 - smoothstep(-1.0, 0.0, sdf));
    float topBias = 0.5 + 0.5 * centerDir.y;
    innerStroke *= (0.4 + 0.6 * topBias);

    float rim = edge * u_glass.z * 0.22;
    float innerGlow = smoothstep(5.0, 0.0, -sdf) * u_glass.z * 0.15;
    float envRefl = (N.y * 0.5 + 0.5) * fres * 0.08;

    float3 fin = col;
    fin += totalSpec;
    fin += rim + innerGlow;
    fin += innerStroke * u_glass.z * 0.55;
    fin += envRefl;

    fin = lerp(fin, float3(1.0, 1.0, 1.0), fres * 0.2 * saturate(u_glass.z * 4.0));

    float a = mask * u_warp.w * input.col.a;
    if (a <= 0.002)
        discard;

    return float4(fin, a);
}
)HLSL";

struct constants
{
    float res[4];
    float shape[4];
    float glass[4];
    float glass2[4];
    float warp[4];
    float bg[4];
    float points[k_max_points][4];
};

struct render_command
{
    constants values{};
    ID3D11ShaderResourceView* backdrop = nullptr;
};

dx11::pixel_shader_pass g_shader;

struct trail
{
    std::vector<ImVec2> points;
    ImVec2 smoothed{0.f, 0.f};
    float time = 0.f;
    float clock = 0.f;
    float carry = 0.f;
    float presence = 0.f;
    bool seeded = false;
    int frame = -1;
};

trail& state()
{
    static trail t;
    return t;
}

void callback(const ImDrawList*, const ImDrawCmd* cmd)
{
    if (!cmd || !cmd->UserCallbackData ||
        cmd->UserCallbackDataSize != static_cast<int>(sizeof(render_command)))
        return;

    render_command command{};
    std::memcpy(&command, cmd->UserCallbackData, sizeof(command));
    if (!g_shader.upload_constants(&command.values, sizeof(command.values)))
        return;

    ID3D11ShaderResourceView* view = command.backdrop;
    g_shader.bind(&view, 1);
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

cursor_options& settings()
{
    static cursor_options o;
    return o;
}

bool& enabled()
{
    static bool on = true;
    return on;
}

bool cursor_live()
{
    return g_shader.ready() && state().seeded && state().presence > 0.05f;
}

bool cursor_init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    return g_shader.initialize(device, context, k_pixel_shader, sizeof(k_pixel_shader) - 1,
                               "glass cursor shader", sizeof(constants), true);
}

void cursor_shutdown()
{
    g_shader.reset();
    state() = trail();
}

void cursor(ImDrawList* dl, const ImRect& viewport, const cursor_options& o)
{
    if (!g_shader.ready() || !dl || !enabled() || o.opacity <= 0.001f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    trail& s = state();

    const int frame = ImGui::GetFrameCount();
    if (s.frame != frame)
    {
        s.frame = frame;

        const float dt = ImClamp(io.DeltaTime, 0.f, 0.1f);
        s.time += dt * ImMax(o.warp_speed, 0.f);
        s.clock += dt;

        const int want = ImClamp(o.trail_length, 2, k_max_points);

        const ImVec2 pointer = o.follow_pointer ? io.MousePos : o.pointer;
        const bool present = (pointer.x > -FLT_MAX * 0.5f) && viewport.Contains(pointer);

        const float toward = present ? 1.f : 0.f;
        const float rate = present ? 6.f : 4.5f;
        s.presence += (toward - s.presence) * ImMin(1.f, dt * rate);
        if (!present && s.presence < 0.01f)
        {
            s.presence = 0.f;
            s.seeded = false;
            s.points.clear();
            return;
        }

        if (present)
        {

            if (s.seeded)
            {
                const ImVec2 d(pointer.x - s.smoothed.x, pointer.y - s.smoothed.y);
                if (d.x * d.x + d.y * d.y > 0.25f * (viewport.GetWidth() * viewport.GetWidth() +
                                                     viewport.GetHeight() * viewport.GetHeight()))
                    s.seeded = false;
            }

            if (!s.seeded)
            {
                s.smoothed = pointer;
                s.seeded = true;
                s.carry = 0.f;
                s.points.assign((size_t)want, s.smoothed);
            }

            const float damp = ImClamp(o.dampening, 0.f, 0.999f);
            const float a = (damp <= 0.f) ? 1.f : 1.f - powf(damp, dt * 60.f);
            s.smoothed.x += (pointer.x - s.smoothed.x) * a;
            s.smoothed.y += (pointer.y - s.smoothed.y) * a;
        }

        if (!s.seeded)
            return;

        const float step = ImMax(o.trail_seconds, 0.008f) / (float)ImMax(want - 1, 1);

        s.carry += dt;
        int steps = (int)(s.carry / step);
        if (steps > 0)
        {
            s.carry -= (float)steps * step;
            steps = ImMin(steps, want);

            const ImVec2 from = s.points.empty() ? s.smoothed : s.points.front();
            for (int i = 1; i <= steps; i++)
            {
                const float t = (float)i / (float)steps;
                s.points.insert(s.points.begin(), ImVec2(from.x + (s.smoothed.x - from.x) * t,
                                                         from.y + (s.smoothed.y - from.y) * t));
            }
        }

        if ((int)s.points.size() > want)
            s.points.resize((size_t)want);
        while ((int)s.points.size() < want)
            s.points.push_back(s.points.back());
    }

    if (!s.seeded || s.points.size() < 2 || s.presence <= 0.004f)
        return;

    const float blob_px = ImMax(solace::px(o.blob_radius), 1.f);

    const float warp_px = ImMax(o.warp_amount, 0.f) * 0.15f;
    const int count = (int)s.points.size();

    ImRect box(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (int i = 0; i < count; i++)
    {
        box.Min.x = ImMin(box.Min.x, s.points[i].x);
        box.Min.y = ImMin(box.Min.y, s.points[i].y);
        box.Max.x = ImMax(box.Max.x, s.points[i].x);
        box.Max.y = ImMax(box.Max.y, s.points[i].y);
    }

    const float pad = blob_px * 1.6f + warp_px + 8.f;
    box.Expand(pad);
    box.ClipWith(viewport);
    if (box.GetWidth() < 1.f || box.GetHeight() < 1.f)
        return;

    snapshot::capture_backdrop(dl);
    if (!snapshot::backdrop_ready())
        return;

    render_command command{};
    constants& c = command.values;

    c.res[0] = viewport.GetWidth();
    c.res[1] = viewport.GetHeight();
    c.res[2] = 1.f / ImMax(c.res[0], 1.f);
    c.res[3] = 1.f / ImMax(c.res[1], 1.f);

    c.shape[0] = blob_px;
    c.shape[1] = (ImClamp(o.threshold, 0.f, 1.f) - 0.5f) * 2.f * blob_px;
    c.shape[2] = ImClamp(o.blur_spread * 2.f, 0.f, 1.f);
    c.shape[3] = (float)count;

    c.glass[0] = o.refraction;
    c.glass[1] = ImMax(o.blur_spread, 0.f) * 24.f;
    c.glass[2] = o.border_glow;
    c.glass[3] = o.specular_gain;

    c.glass2[0] = o.chromatic;
    c.glass2[1] = o.tint;
    c.glass2[2] = o.saturation;
    c.glass2[3] = o.brightness;

    c.warp[0] = warp_px;
    c.warp[1] = ImMax(o.warp_scale, 0.f) / ImMax(blob_px * 9.f, 1.f);
    c.warp[2] = s.time;
    c.warp[3] = ImClamp(o.opacity, 0.f, 1.f) * ImClamp(s.presence, 0.f, 1.f);

    set_colour(c.bg, o.background);

    const float fade = ImClamp(o.tail_fade, 0.f, 1.f);
    const float last = (float)(count - 1);
    const float flow = ImClamp(o.flow_amount, 0.f, 0.9f);

    for (int i = 0; i < count; i++)
    {
        const float u = (float)i / ImMax(last, 1.f);

        float r = blob_px * (1.f - fade * u);
        r *= 1.f + flow * sinf(u * 7.4f - s.clock * o.flow_speed);

        c.points[i][0] = s.points[i].x;
        c.points[i][1] = s.points[i].y;
        c.points[i][2] = ImMax(r, 1.f);
        c.points[i][3] = 0.f;
    }

    command.backdrop =
        reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<intptr_t>(snapshot::backdrop()));

    dl->AddCallback(callback, &command, sizeof(command));
    dl->AddRectFilled(box.Min, box.Max, IM_COL32_WHITE);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
} // namespace solace::glass
