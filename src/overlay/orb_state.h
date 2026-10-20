#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace overlay {
enum class OrbPhase { Unknown, Protected, Attackable, Expired, ClaimPending, Paused };
inline const char* orb_phase_name(OrbPhase phase) {
    switch(phase) {
    case OrbPhase::Protected:return "Orb not attackable yet";
    case OrbPhase::Attackable:return "Claimable soul orb";
    case OrbPhase::Expired:return "Orb attackable window ended";
    case OrbPhase::ClaimPending:return "Orb hit already pending";
    case OrbPhase::Paused:return "Game paused";
    default:return "Orb state unavailable";
    }
}
struct OrbState {
    float launch{},opens{},closes{},clock{};int launch_number{};
    uint64_t sampled{};bool valid{},paused{},claim_pending{};
    OrbPhase phase(uint64_t now)const {
        if(!valid||!sampled||now<sampled||now-sampled>25)return OrbPhase::Unknown;
        if(paused)return OrbPhase::Paused;
        if(claim_pending)return OrbPhase::ClaimPending;
        const double current=double(clock)+double(now-sampled)*.001;
        if(current<double(opens))return OrbPhase::Protected;
        if(current>=double(closes))return OrbPhase::Expired;
        return OrbPhase::Attackable;
    }
    uint64_t deadline()const {
        if(!valid||paused||closes<=clock)return 0;
        const auto remaining=uint64_t(std::floor((double(closes)-double(clock))*1000.));
        return remaining?std::min(sampled+25,sampled+remaining-1):0;
    }
};
}
