#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace overlay {
struct HeroProfile {
    bool valid{};
    int id{};
    std::string name{"Waiting for hero"},token;
    uint32_t sniper_handle{};
    bool sniper_present{},sniper_valid{},sniper_scoped{};
    float scope_start{};
    std::string ability_status{"Waiting for ability data"};
};
inline bool vindicta(const HeroProfile& hero) {
    return hero.valid&&hero.id==3&&hero.token=="hero_hornet";
}
inline bool sniper_scope(float started) {return std::isfinite(started)&&started>0;}
inline bool sniper_speed_active(const HeroProfile& hero,bool enabled) {
    return enabled&&vindicta(hero)&&hero.sniper_present&&hero.sniper_valid&&hero.sniper_scoped;
}
inline float hero_focus_speed(const HeroProfile& hero,bool enabled,float normal,float sniper) {
    auto speed=sniper_speed_active(hero,enabled)?sniper:normal;
    return std::isfinite(speed)?std::clamp(speed,1.f,120.f):12.f;
}
inline std::string hero_token(std::string_view name) {
    if(name.starts_with('#'))name.remove_prefix(1);
    if(name.ends_with("_sort"))name.remove_suffix(5);
    else if(name.ends_with("_search"))name.remove_suffix(7);
    if(!name.starts_with("hero_")||name.size()>96)return {};
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return {};
    return std::string(name);
}
}
