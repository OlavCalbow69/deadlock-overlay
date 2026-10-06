#include "ui/foundation/motion/motion.h"
#include <math.h>

namespace solace::mo
{
spring_cfg from_bounce(float duration, float bounce, float mass)
{
    const float z = ImClamp(1.f - bounce, 0.05f, 1.f);
    const float d = ImMax(duration, 0.01f);

    const float zz = ImMin(z, 0.9999f);
    const float w = -logf(0.001f * sqrtf(1.f - zz * zz) / zz) / (zz * d);

    return spring_cfg{w * w * mass, 2.f * zz * w * mass, mass};
}

float ease::operator()(float t) const
{
    if (t <= 0.f)
        return 0.f;
    if (t >= 1.f)
        return 1.f;

    const float ax = 3.f * x1 - 3.f * x2 + 1.f;
    const float bx = 3.f * x2 - 6.f * x1;
    const float cx = 3.f * x1;

    const float ay = 3.f * y1 - 3.f * y2 + 1.f;
    const float by = 3.f * y2 - 6.f * y1;
    const float cy = 3.f * y1;

    auto sample_x = [&](float u) { return ((ax * u + bx) * u + cx) * u; };
    auto sample_dx = [&](float u) { return (3.f * ax * u + 2.f * bx) * u + cx; };

    float u = t;
    for (int i = 0; i < 8; i++)
    {
        const float x = sample_x(u) - t;
        if (ImFabs(x) < 1e-6f)
            break;

        const float d = sample_dx(u);
        if (ImFabs(d) < 1e-6f)
            break;

        u -= x / d;
    }

    if (u < 0.f || u > 1.f)
    {
        float lo = 0.f, hi = 1.f;
        u = t;
        for (int i = 0; i < 20; i++)
        {
            const float x = sample_x(u);
            if (ImFabs(x - t) < 1e-6f)
                break;
            if (x < t)
                lo = u;
            else
                hi = u;
            u = (lo + hi) * 0.5f;
        }
    }

    return ((ay * u + by) * u + cy) * u;
}

float spring::to(float target, const spring_cfg& cfg, float dt)
{
    if (!seeded)
    {
        snap(target);
        return value;
    }

    if (dt <= 0.f)
        return value;

    dt = ImMin(dt, 0.1f);

    const int steps = ImMax(1, (int)ceilf(dt / 0.001f));
    const float h = dt / (float)steps;

    for (int i = 0; i < steps; i++)
    {
        const float accel = (-cfg.stiffness * (value - target) - cfg.damping * velocity) / cfg.mass;
        velocity += accel * h;
        value += velocity * h;
    }

    const float scale = ImMax(1.f, ImFabs(target));
    if (ImFabs(value - target) < 0.0005f * scale && ImFabs(velocity) < 0.005f * scale)
        snap(target);

    return value;
}

float clock::progress(float duration, float delay) const
{
    if (duration <= 0.f)
        return elapsed >= delay ? 1.f : 0.f;

    return ImClamp((elapsed - delay) / duration, 0.f, 1.f);
}

float keyframes(const float* values, int count, float t, float duration, const ease& e)
{
    if (count <= 0)
        return 0.f;
    if (count == 1)
        return values[0];

    const float progress = duration > 0.f ? ImClamp(t / duration, 0.f, 1.f) : 1.f;
    const float scaled = progress * (float)(count - 1);
    const int index = ImMin((int)scaled, count - 2);
    const float local = e(scaled - (float)index);

    return lerp(values[index], values[index + 1], local);
}

bool presence::update(bool want, float dt, float exit_duration)
{
    if (want)
    {
        if (!mounted)
        {
            mounted = true;
            in = skip_enter ? 1e6f : 0.f;
            exiting = false;
        }
        else if (exiting)
        {
            exiting = false;
            in = 0.f;
        }
    }
    else if (mounted && !exiting)
    {
        exiting = true;
        out = 0.f;
    }

    skip_enter = false;

    if (!mounted)
        return false;

    if (exiting)
    {
        out += dt;
        if (out >= exit_duration)
        {
            mounted = false;
            return false;
        }
    }
    else
    {
        in += dt;
    }

    return true;
}

ImU32 mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ca, cb, ImClamp(t, 0.f, 1.f)));
}

ImU32 with_alpha(ImU32 col, float alpha)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    c.w *= ImClamp(alpha, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(c);
}
} // namespace solace::mo
