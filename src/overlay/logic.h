#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace overlay {
struct Vec3 { float x{}, y{}, z{}; };
using Matrix = std::array<float, 16>;
struct ScreenPoint { float x{}, y{}; };
struct BarRect {float left{},top{},right{},bottom{};};
inline BarRect bar_rect(ScreenPoint p,float length,float thickness,float gap,float scale) {
    length*=scale;thickness*=scale;gap*=scale;
    return {p.x-length/2,p.y-gap-thickness,p.x+length/2,p.y-gap};
}
inline BarRect bar_fill(BarRect rect,float fill) {
    fill=std::clamp(fill,0.f,1.f);
    rect.right=rect.left+(rect.right-rect.left)*fill;
    return rect;
}
inline bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline bool project(const Matrix& m, Vec3 p, float width, float height, ScreenPoint& out) {
    if (!finite(p) || width <= 0 || height <= 0) return false;
    const float w = m[12]*p.x + m[13]*p.y + m[14]*p.z + m[15];
    if (!std::isfinite(w) || w < 0.001f) return false;
    const float x = (m[0]*p.x + m[1]*p.y + m[2]*p.z + m[3])/w;
    const float y = (m[4]*p.x + m[5]*p.y + m[6]*p.z + m[7])/w;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1 || std::abs(y)>1) return false;
    out = {(x+1)*width*0.5f, (1-y)*height*0.5f};
    return true;
}
inline float health_fraction(int health, int maximum) {
    return maximum > 0 ? std::clamp(float(health)/float(maximum), 0.0f, 1.0f) : 0.0f;
}
inline bool valid_handle(uint32_t requested, uint32_t identity, uint32_t index) {
    return requested != UINT32_MAX && index <= 0x7ffe && (requested & 0x7fff)==index
        && requested == identity;
}
inline bool fresh(uint64_t now, uint64_t sampled) {
    return sampled != 0 && now >= sampled && now-sampled <= 250;
}
inline int select_anchor(int head_end, int head) { return head_end >= 0 ? head_end : head; }
inline bool include_player(bool exclude_teammates,uint8_t local_team,uint8_t player_team) {
    return !exclude_teammates||local_team<2||player_team<2||local_team!=player_team;
}
inline bool render_active(bool game_exists, bool minimized, bool game_foreground, bool menu_open, bool menu_foreground) {
    return game_exists && !minimized && (game_foreground || (menu_open && menu_foreground));
}
}
