#include "overlay/rift.h"
#include <iostream>

int main() {
    using namespace overlay;
    int failures{};
    auto check=[&](bool valid,const char* name){if(!valid){std::cerr<<name<<'\n';++failures;}};
    RiftHistory history;
    auto step=[&](uint64_t now,float clock,uint32_t warning=0,int lane=6,bool active=false,float giveup=0,bool paused=false){
        return history.update(now,clock,paused,active,giveup,{1,2,3},warning,lane,60);
    };
    auto state=step(1000,100);
    check(state.phase==RiftPhase::Waiting&&!state.timer_valid,"initial waiting");
    state=step(1100,100.1f,42);
    check(state.phase==RiftPhase::Countdown&&state.estimated&&std::abs(state.remaining-60)<.001f&&state.lane==6,"observed warning start");
    state=step(1200,100.2f,42);
    check(std::abs(state.remaining-59.9f)<.001f,"warning does not restart");
    state=step(1300,100.2f,42,6,false,0,true);
    check(state.paused&&std::abs(state.remaining-59.9f)<.001f,"pause uses game clock");
    state=step(1400,100.3f,0,0,true,220);
    check(state.phase==RiftPhase::Active&&state.timer_valid&&!state.estimated&&state.lane==6&&std::abs(state.remaining-119.7f)<.001f,"authoritative active timer and retained lane");
    state=step(1500,100.4f);
    check(state.phase==RiftPhase::Waiting&&state.lane==0&&!state.timer_valid,"completed event clears lane and timer");
    history.reset();state=step(2000,200,42);
    check(state.phase==RiftPhase::Incoming&&!state.timer_valid,"join mid-warning has no fabricated countdown");
    state=step(2100,200.1f,42);
    check(state.phase==RiftPhase::Incoming&&!state.timer_valid,"mid-warning stays unanchored");
    history.reset();step(3000,300);step(3100,300.1f,42);state=step(3900,300.9f,42);
    check(state.phase==RiftPhase::Incoming&&!state.timer_valid,"sample gap invalidates estimate");
    history.reset();step(4000,400);step(4100,400.1f,42);state=step(4200,100,42);
    check(!state.timer_valid,"clock rewind invalidates estimate");
    history.reset();step(5000,500);step(5100,500.1f,42);state=step(5200,500.2f,43);
    check(!state.timer_valid,"replacement warning cannot fabricate a new start");
    history.reset();state=step(6000,600,0,0,true,599);
    check(state.phase==RiftPhase::Active&&!state.timer_valid,"expired active deadline is not a countdown");
    check(rift_lane_valid(1)&&rift_lane_valid(3)&&rift_lane_valid(4)&&rift_lane_valid(6)&&!rift_lane_valid(2),"network lane values");
    if(!failures)std::cout<<"Rift timing checks passed (13 cases)\n";
    return failures?1:0;
}
