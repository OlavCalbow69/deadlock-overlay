#pragma once
#include "logic.h"
#include <string>

namespace overlay {
enum class RiftPhase {Unavailable,Waiting,Incoming,Countdown,Active};
inline const char* rift_phase_name(RiftPhase phase) {
    switch(phase){case RiftPhase::Waiting:return "waiting";case RiftPhase::Incoming:return "incoming";case RiftPhase::Countdown:return "countdown";case RiftPhase::Active:return "active";default:return "unavailable";}
}
inline const char* rift_lane_name(int lane) {
    switch(lane){case 1:return "Yellow / York";case 3:case 6:return "Green / Greenwich";case 4:return "Blue / Broadway";default:return "Lane unannounced";}
}
inline bool rift_lane_valid(int lane){return lane==1||lane==3||lane==4||lane==6;}
struct RiftState {
    bool valid{},paused{},estimated{},timer_valid{};RiftPhase phase{RiftPhase::Unavailable};
    float remaining{},deadline{},game_time{},warning_seconds{};int lane{};uint32_t warning_handle{};uint64_t sampled{};
    Vec3 position{};std::string status{"Waiting for rift data"};
};
// A warning countdown is anchored only after observing its transition. Joining
// mid-warning cannot establish when it started; never fabricate a fresh timer.
struct RiftHistory {
    uint64_t sampled{};float clock{},deadline{};uint32_t warning{};int lane{};bool observed_clear{};
    void reset(){*this={};}
    RiftState update(uint64_t now,float game_time,bool paused,bool active,float giveup,Vec3 position,uint32_t incoming,int incoming_lane,float warning_seconds) {
        RiftState out;out.valid=true;out.paused=paused;out.sampled=now;out.game_time=game_time;out.position=position;out.warning_seconds=warning_seconds;out.warning_handle=incoming;
        const bool continuous=sampled&&now>=sampled&&now-sampled<=500&&game_time>=clock-.1f&&game_time-clock<1;
        if(!continuous){deadline=0;warning=0;lane=0;observed_clear=false;}
        if(active) {
            out.phase=RiftPhase::Active;out.status="Rift active";
            if(rift_lane_valid(incoming_lane))lane=incoming_lane;out.lane=lane;
            if(std::isfinite(giveup)&&giveup>game_time&&giveup-game_time<=3600){out.timer_valid=true;out.deadline=giveup;out.remaining=giveup-game_time;}
            observed_clear=false;deadline=0;
        }else if(incoming) {
            if(incoming!=warning) {
                deadline=continuous&&observed_clear&&std::isfinite(warning_seconds)&&warning_seconds>0&&warning_seconds<=600?game_time+warning_seconds:0;
                lane=rift_lane_valid(incoming_lane)?incoming_lane:0;
            }
            if(rift_lane_valid(incoming_lane))lane=incoming_lane;
            out.lane=lane;out.phase=RiftPhase::Incoming;out.status="Rift announced; warning start unavailable";
            if(deadline>game_time){out.phase=RiftPhase::Countdown;out.estimated=out.timer_valid=true;out.deadline=deadline;out.remaining=deadline-game_time;out.status="Observed warning countdown";}
            else if(deadline>0){out.status="Rift opening; awaiting active zone";}
            observed_clear=false;
        }else{out.phase=RiftPhase::Waiting;out.status="Waiting for rift announcement";deadline=0;lane=0;observed_clear=true;}
        sampled=now;clock=game_time;warning=incoming;return out;
    }
};
}
