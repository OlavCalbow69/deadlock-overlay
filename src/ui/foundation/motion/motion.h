#pragma once
#include "imgui.h"
#include "imgui_internal.h"

namespace solace::mo
{

struct ease
{
    float x1, y1, x2, y2;
    float operator()(float t) const;
};

inline constexpr ease EASE_OUT{0.16f, 1.f, 0.3f, 1.f};
inline constexpr ease EASE_IN_OUT{0.77f, 0.f, 0.175f, 1.f};

inline constexpr ease EASE_OUT_NAMED{0.f, 0.f, 0.58f, 1.f};
inline constexpr ease EASE_IN_OUT_NAMED{0.42f, 0.f, 0.58f, 1.f};

inline constexpr ease EASE_STANDARD{0.4f, 0.f, 0.2f, 1.f};

struct spring_cfg
{
    float stiffness, damping, mass;
};

inline constexpr spring_cfg SPRING_PRESS{500.f, 30.f, 0.6f};
inline constexpr spring_cfg SPRING_SWAP{460.f, 30.f, 0.55f};
inline constexpr spring_cfg SPRING_LAYOUT{360.f, 32.f, 0.6f};

spring_cfg from_bounce(float duration, float bounce, float mass = 1.f);

struct spring
{
    float value = 0.f;
    float velocity = 0.f;
    bool seeded = false;

    void snap(float v)
    {
        value = v;
        velocity = 0.f;
        seeded = true;
    }
    float to(float target, const spring_cfg& cfg, float dt);
};

struct clock
{
    float elapsed = 0.f;

    void reset()
    {
        elapsed = 0.f;
    }
    void advance(float dt)
    {
        elapsed += dt;
    }
    float progress(float duration, float delay = 0.f) const;
    float eased(const ease& e, float duration, float delay = 0.f) const
    {
        return e(progress(duration, delay));
    }
    bool done(float duration, float delay = 0.f) const
    {
        return elapsed >= duration + delay;
    }
};

float keyframes(const float* values, int count, float t, float duration, const ease& e);

struct presence
{
    bool mounted = false;
    bool exiting = false;
    float in = 0.f;
    float out = 0.f;
    bool skip_enter = true;

    bool update(bool want, float dt, float exit_duration);
};

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
ImU32 mix(ImU32 a, ImU32 b, float t);
ImU32 with_alpha(ImU32 col, float alpha);
} // namespace solace::mo
