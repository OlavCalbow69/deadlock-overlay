#pragma once
#include "imgui.h"
#include <windows.h>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <string>
#include <type_traits>

namespace overlay {
// Optional HUD and hero helpers share Vanta's settings file.
struct ExternalSettings {
    bool spectators{},crosshair{},crosshair_dot{true},crosshair_outline{true};
    float crosshair_size{8},crosshair_gap{4},crosshair_width{1.5f},render_cap{};
    ImVec4 crosshair_color{0,.85f,.91f,1};
    bool auto_save{true};
    bool player_distance{},hero_icons{};float hero_icon_size{28};
    bool world{},world_lane{true},world_jungle{true},world_breakables{true},world_pickups{true},world_spawners{true};
    bool world_allies{true},world_distance{},world_timers{true},world_destroyed{true};
    float world_range{100},world_icon_size{22};
    bool minimap{},minimap_players{true},minimap_allies{true},minimap_world{true},minimap_lanes{true};
    int minimap_corner{3};float minimap_size{320},minimap_opacity{.85f},minimap_icon_size{20};
    bool visual_preview{true},preview_occluded{};float preview_health{.74f},preview_yaw{-90};
    bool rift_hud{true},preview_hud_live{};int preview_page{};
};
#define VANTA_EXTERNAL_PROPERTIES(X) \
 X(spectators) X(crosshair) X(crosshair_dot) X(crosshair_outline) \
 X(crosshair_size) X(crosshair_gap) X(crosshair_width) X(crosshair_color) X(render_cap) \
 X(auto_save) \
 X(player_distance) X(hero_icons) X(hero_icon_size) \
 X(world) X(world_lane) X(world_jungle) X(world_breakables) X(world_pickups) X(world_spawners) \
 X(world_allies) X(world_distance) X(world_timers) X(world_destroyed) X(world_range) X(world_icon_size) \
 X(minimap) X(minimap_players) X(minimap_allies) X(minimap_world) X(minimap_lanes) \
 X(minimap_corner) X(minimap_size) X(minimap_opacity) X(minimap_icon_size) \
 X(visual_preview) X(preview_occluded) X(preview_health) X(preview_yaw) X(rift_hud) X(preview_page) X(preview_hud_live)
template<class T> inline void write_preference(std::ostream& out,const T& value) {
    if constexpr(std::is_same_v<T,ImVec4>)out<<value.x<<' '<<value.y<<' '<<value.z<<' '<<value.w;
    else if constexpr(std::is_same_v<T,std::string>)out<<std::quoted(value);
    else out<<value;
}
template<class T> inline void read_preference(std::istream& in,T& value) {
    T candidate{};
    if constexpr(std::is_same_v<T,ImVec4>) {
        if(in>>candidate.x>>candidate.y>>candidate.z>>candidate.w) {
            for(float v:{candidate.x,candidate.y,candidate.z,candidate.w})if(!std::isfinite(v))return;
            value={std::clamp(candidate.x,0.f,1.f),std::clamp(candidate.y,0.f,1.f),std::clamp(candidate.z,0.f,1.f),std::clamp(candidate.w,0.f,1.f)};
        }
    } else if constexpr(std::is_same_v<T,std::string>){if(in>>std::quoted(candidate)&&candidate.size()<=128)value=candidate;}
    else if constexpr(std::is_same_v<T,bool>){int n;if(in>>n)value=n!=0;}
    else if(in>>candidate){if constexpr(std::is_floating_point_v<T>){if(!std::isfinite(candidate))return;}value=candidate;}
}
inline void write_external_settings(std::ostream& out,const ExternalSettings& s) {
#define X(name) out<<"extra_" #name "=";write_preference(out,s.name);out<<'\n';
    VANTA_EXTERNAL_PROPERTIES(X)
#undef X
}
inline bool read_external_setting(ExternalSettings& s,std::string_view key,std::istream& in) {
#define X(name) if(key=="extra_" #name){read_preference(in,s.name);return true;}
    VANTA_EXTERNAL_PROPERTIES(X)
#undef X
    return false;
}
inline void sanitize_external(ExternalSettings& s) {
    auto clamp=[](float& v,float low,float high){v=std::clamp(std::isfinite(v)?v:low,low,high);};
    if(s.render_cap!=0)clamp(s.render_cap,30,240);
    clamp(s.crosshair_size,2,24);clamp(s.crosshair_gap,0,16);clamp(s.crosshair_width,1,4);
    clamp(s.hero_icon_size,16,56);clamp(s.world_range,10,1000);clamp(s.world_icon_size,12,40);
    clamp(s.minimap_size,180,600);clamp(s.minimap_opacity,.1f,1);clamp(s.minimap_icon_size,12,32);s.minimap_corner=std::clamp(s.minimap_corner,0,3);
    clamp(s.preview_health,.01f,1);clamp(s.preview_yaw,-180,180);
    s.preview_page=std::clamp(s.preview_page,0,1);
}
}
