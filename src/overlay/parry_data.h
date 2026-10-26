#pragma once
#include "hitboxes.h"
#include <string>
#include <vector>

namespace overlay {
enum class ParryThreatKind { None, LightMelee, HeavyMelee, SpellProjectile, PuddlePunch };
inline const char* parry_threat_name(ParryThreatKind kind) {
    switch(kind){case ParryThreatKind::LightMelee:return "Light melee";case ParryThreatKind::HeavyMelee:return "Heavy melee";
    case ParryThreatKind::PuddlePunch:return "Puddle Punch";case ParryThreatKind::SpellProjectile:return "Spell projectile";default:return "None";}
}
struct MeleeThreatSample {
    uint32_t pawn{},ability{};uint8_t team{};
    int state{},attack_type{};float state_start{},trigger_time{},activated{},simulation_time{};
    bool using_melee{},hit{},parryable{},piercing{},valid{},replicated{};
    float range{},half_angle{},attack_duration{};
    Vec3 origin{},direction{},velocity{};
    std::string name,status;
};
struct SpellThreatSample {
    uint32_t handle{},owner{};uint8_t team{};
    Vec3 position{},velocity{};float radius{};
    std::string name;
};
struct ParryReadState {
    bool valid{},ready{},active{},counterspell_owned{},counterspell_ready{},counterspell_active{},paused{};
    uint32_t local_handle{},ability{},counterspell{};
    uint64_t sampled{};
    float game_time{},interval{},cooldown_remaining{},spell_cooldown_remaining{};
    Vec3 local_velocity{};
    std::vector<Hitbox> local_hitboxes;
    std::vector<MeleeThreatSample> melee;
    std::vector<SpellThreatSample> projectiles;
    unsigned unsupported_projectiles{},unavailable_melee{},replicated_readers{};
    bool melee_definition_valid{};
    std::string melee_definition_status{"Waiting for base melee definition"};
    std::string status{"Waiting for parry data"};
};
}
