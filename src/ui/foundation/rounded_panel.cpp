#include "ui/foundation/rounded_panel.h"
#include "graphics/dx11_helpers.h"

#include <algorithm>

namespace solace::rounded_panel
{
namespace
{
struct float4
{
    float x;
    float y;
    float z;
    float w;
};

struct alignas(16) shader_constants
{
    float4 bounds;
    float4 fill_color;
    float4 shape;
    float4 viewport_transform;
};

static_assert(sizeof(float4) == 16);
static_assert(sizeof(shader_constants) == 64);

dx11::pixel_shader_pass g_renderer;

constexpr char k_pixel_shader[] = R"HLSL(
cbuffer RoundedPanelParams : register(b0)
{
    float4 u_bounds;
    float4 u_fill;
    float4 u_shape;
    float4 u_viewport_transform;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
};

float rounded_box(float2 sample_position, float2 half_size, float radius)
{
    float2 q = abs(sample_position) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 main(PS_INPUT input) : SV_Target
{
    float2 inv_framebuffer_scale = u_viewport_transform.zw;
    float2 pixel = input.pos.xy * inv_framebuffer_scale + u_viewport_transform.xy;
    float2 center = (u_bounds.xy + u_bounds.zw) * 0.5;
    float2 half_size = max((u_bounds.zw - u_bounds.xy) * 0.5, 0.0);
    float radius = min(max(u_shape.x, 0.0), min(half_size.x, half_size.y));
    float center_distance = rounded_box(pixel - center, half_size, radius);

    // Most pixels are nowhere near the one-pixel silhouette. Keep those fast
    // and supersample only the narrow outer band.
    if (center_distance > 1.0)
        return 0.0;
    if (center_distance < -1.0)
        return u_fill;

    float coverage_samples = 0.0;
    float sample_feather = 0.5 * max(inv_framebuffer_scale.x, inv_framebuffer_scale.y);
    [unroll] for (int y = 0; y < 8; ++y)
    {
        [unroll] for (int x = 0; x < 8; ++x)
        {
            float2 offset = (float2(x, y) + 0.5) * (1.0 / 8.0) - 0.5;
            float distance = rounded_box(pixel + offset * inv_framebuffer_scale - center,
                half_size, radius);
            coverage_samples += saturate(0.5 - distance / sample_feather);
        }
    }

    return float4(u_fill.rgb, u_fill.a * coverage_samples * (1.0 / 64.0));
}
)HLSL";

float4 to_float4(ImU32 color)
{
    const ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    return {value.x, value.y, value.z, value.w};
}

void bind_shader_callback(const ImDrawList*, const ImDrawCmd* command)
{
    if (!command || !command->UserCallbackData ||
        command->UserCallbackDataSize != static_cast<int>(sizeof(shader_constants)))
        return;

    if (g_renderer.upload_constants(command->UserCallbackData, sizeof(shader_constants)))
        g_renderer.bind();
}

void draw_fallback(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, ImU32 fill,
                   float radius)
{
    draw_list->AddRectFilled(min, max, fill, radius);
}
} // namespace

bool init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    return g_renderer.initialize(device, context, k_pixel_shader, sizeof(k_pixel_shader) - 1,
                                 "rounded panel shader", sizeof(shader_constants), false);
}

void shutdown()
{
    g_renderer.reset();
}

void draw(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, ImU32 fill, float radius)
{
    if (!draw_list || max.x <= min.x || max.y <= min.y)
        return;

    radius = (std::max)(radius, 0.f);
    if (!g_renderer.ready())
    {
        draw_fallback(draw_list, min, max, fill, radius);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float framebuffer_x =
        io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
    const float framebuffer_y =
        io.DisplayFramebufferScale.y > 0.f ? io.DisplayFramebufferScale.y : 1.f;
    shader_constants values{};
    values.bounds = {min.x, min.y, max.x, max.y};
    values.fill_color = to_float4(fill);
    values.shape = {radius, 0.f, 0.f, 0.f};
    values.viewport_transform = {viewport ? viewport->Pos.x : 0.f, viewport ? viewport->Pos.y : 0.f,
                                 1.f / framebuffer_x, 1.f / framebuffer_y};

    draw_list->AddCallback(bind_shader_callback, &values, sizeof(values));
    draw_list->AddRectFilled(min, max, IM_COL32_WHITE);
    draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
} // namespace solace::rounded_panel
