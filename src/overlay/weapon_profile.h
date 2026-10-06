#pragma once
#include <cmath>
#include <cstdint>
#include <string>
namespace overlay {
struct WeaponProfile {
    bool valid{};
    uint32_t handle{};
    float base_speed{},speed{},bonus_percent{},inheritance{},random_factor{};
    std::string status{"Waiting for local weapon"};
};
inline bool valid_weapon_numbers(float base,float bonus,float inheritance,float random){
    float speed=base*(1.f+bonus/100.f);
    return std::isfinite(base)&&base>0&&base<=200000&&std::isfinite(bonus)&&bonus>=-100&&bonus<=10000
        &&std::isfinite(speed)&&speed>0&&speed<=200000&&std::isfinite(inheritance)&&inheritance>=0&&inheritance<=2
        &&std::isfinite(random)&&random==0;
}
inline bool valid_modifier_cache(uint32_t sequence,uint32_t cache_version,uint32_t before,uint32_t after,
    uint8_t policy,uint32_t cached_tick,uint32_t current_tick,uint16_t dirty,uint32_t result_type,float value,uint32_t key){
    return !(sequence&1)&&before==after&&cache_version==before&&before!=0&&!dirty
        &&(policy==3||policy==4)&&(policy!=4||cached_tick==current_tick)
        &&(result_type==0||result_type==2)&&std::isfinite(value)&&key==1664691683u;
}
}
