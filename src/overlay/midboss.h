#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace overlay {
enum class MidBossPhase {Unavailable,Alive,Countdown,Due};
struct MidBossState {
    MidBossPhase phase{MidBossPhase::Unavailable};bool valid{},paused{};
    float game_clock{},next_spawn{},remaining{};int kills{};uint64_t sampled{};
    std::string status{"Waiting for match data"};
};
inline const char* midboss_phase_name(MidBossPhase phase) {
    switch(phase){case MidBossPhase::Alive:return "alive";case MidBossPhase::Countdown:return "countdown";case MidBossPhase::Due:return "due";default:return "unavailable";}
}
inline std::string midboss_countdown(float seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>86400)return "--:--";
    const int value=int(std::ceil(seconds));char label[24]{};snprintf(label,sizeof(label),"%02d:%02d",value/60,value%60);return label;
}
inline bool midboss_clock(float anchor,int anchor_tick,int tick,float interval,bool paused,float& clock) {
    if(!std::isfinite(anchor)||!std::isfinite(interval)||interval<.001f||interval>.1f||anchor_tick<0||tick<anchor_tick
        ||tick-anchor_tick>10000000)return false;
    clock=anchor+(paused?0.f:float(tick-anchor_tick)*interval);
    return std::isfinite(clock)&&clock>=-180&&clock<=86400;
}
inline MidBossState midboss_state(float game_clock,float next_spawn,int kills,bool alive,bool paused,uint64_t sampled) {
    MidBossState result;result.sampled=sampled;
    if(!std::isfinite(game_clock)||game_clock< -180||game_clock>86400||!std::isfinite(next_spawn)||next_spawn<0||next_spawn>90000||kills<0||kills>100) {
        result.status="Mid Boss timing data unavailable";return result;
    }
    result.game_clock=game_clock;result.next_spawn=next_spawn;result.kills=kills;result.paused=paused;
    if(alive){result.valid=true;result.phase=MidBossPhase::Alive;result.status="Alive";}
    else if(next_spawn>0&&next_spawn-game_clock>0&&next_spawn-game_clock<=3600) {
        result.valid=true;result.phase=MidBossPhase::Countdown;result.remaining=next_spawn-game_clock;result.status="Spawns in "+midboss_countdown(result.remaining);
    }else if(next_spawn>0&&next_spawn<=game_clock) {
        // A due timestamp is not evidence that a boss entity has spawned.
        result.valid=true;result.phase=MidBossPhase::Due;result.status="Spawn due / awaiting boss";
    }else result.status="No Mid Boss spawn data on this map";
    return result;
}
}
