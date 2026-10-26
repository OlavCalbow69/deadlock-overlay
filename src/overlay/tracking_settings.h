#pragma once
#include "external_settings.h"
#include "focus_input.h"
#include <random>
namespace overlay {
struct TrackingSettings {
    bool enabled{},toggle{},ignore_x{},ignore_y{},lock_subject{true},auto_switch{},fixed_lead{},rgb{},target_line{};
    int activation_key{},second_key{},cycle_key{VK_NEXT},joint{-1},strategy{},groups{15};
    float interval{.75f},extra_x{},extra_y{},smoothness{},deadzone{4},max_counts{900},ease{},offset_x{},offset_y{},range{},lead{.1f};
    ImVec4 guide_color{.35f,.65f,1,1};
};
#define VANTA_TRACK_PROPERTIES(X) X(enabled) X(toggle) X(ignore_x) X(ignore_y) X(lock_subject) X(auto_switch) X(fixed_lead) X(rgb) X(target_line) X(activation_key) X(second_key) X(cycle_key) X(joint) X(strategy) X(groups) X(interval) X(extra_x) X(extra_y) X(smoothness) X(deadzone) X(max_counts) X(ease) X(offset_x) X(offset_y) X(range) X(lead) X(guide_color)
inline bool read_tracking_setting(TrackingSettings& s,std::string_view key,std::istream& in) {
#define X(name) if(key=="tracking_" #name){read_preference(in,s.name);return true;}
    VANTA_TRACK_PROPERTIES(X)
#undef X
    return false;
}
inline void write_tracking_settings(std::ostream& out,const TrackingSettings& s,std::string_view prefix) {
#define X(name) out<<prefix<<"tracking_" #name "=";write_preference(out,s.name);out<<'\n';
    VANTA_TRACK_PROPERTIES(X)
#undef X
}
inline void sanitize_tracking(TrackingSettings& s) {
    s.joint=std::clamp(s.joint,-1,15);s.strategy=std::clamp(s.strategy,0,2);s.groups=std::clamp(s.groups,1,15);
    for(int* key:{&s.activation_key,&s.second_key,&s.cycle_key})if(*key<0||*key>254||*key==VK_INSERT||*key==VK_ESCAPE)*key=0;
    auto clamp=[](float& v,float lo,float hi){v=std::clamp(std::isfinite(v)?v:lo,lo,hi);};
    clamp(s.interval,.1f,5);clamp(s.extra_x,0,30);clamp(s.extra_y,0,30);clamp(s.smoothness,0,30);clamp(s.deadzone,0,20);clamp(s.max_counts,100,2400);
    clamp(s.ease,0,1);clamp(s.offset_x,-200,200);clamp(s.offset_y,-200,200);clamp(s.range,0,20000);clamp(s.lead,0,.5f);
}
struct TrackingMotion {
    TrackingSettings settings;bool enabled{};uint64_t sample{},last_sample{};double last{},engagement{};float vx{},vy{},rx{},ry{},error{};
    void clear(){last_sample=0;last=engagement=0;vx=vy=rx=ry=0;}
    bool apply(long& x,long& y) {
        if(!enabled)return true;
        if(!sample||sample==last_sample)return false;
        const double now=double(GetTickCount64());float dt=last>0?float((now-last)/1000):1.f/240;last=now;last_sample=sample;
        if(dt<=0||dt>.1f){clear();return false;}dt=std::min(dt,.05f);engagement+=dt;
        if(error<=settings.deadzone){vx=vy=rx=ry=0;return false;}
        float desired_x=settings.ignore_x?0:float(x)/dt,desired_y=settings.ignore_y?0:float(y)/dt;
        auto gain=[&](float extra){float time=(settings.smoothness+extra)/60;return time<=0?1.f:1-std::exp(-dt/time);};
        vx+=(desired_x-vx)*gain(settings.extra_x);vy+=(desired_y-vy)*gain(settings.extra_y);
        if(settings.ignore_x)vx=rx=0;if(settings.ignore_y)vy=ry=0;
        float length=std::hypot(vx,vy);if(length>settings.max_counts){vx*=settings.max_counts/length;vy*=settings.max_counts/length;}
        float ramp=settings.ease>0?std::min(1.f,float(engagement)/settings.ease):1;
        float dx=vx*dt*ramp+rx,dy=vy*dt*ramp+ry;
        // Added smoothing never reverses or exceeds the controller's request.
        auto bounded=[](float value,long bound){return value*bound<=0?0.f:std::copysign(std::min(std::abs(value),float(std::abs(bound))),value);};
        dx=bounded(dx,x);dy=bounded(dy,y);x=long(dx);y=long(dy);rx=dx-float(x);ry=dy-float(y);return x||y;
    }
};
inline TrackingMotion tracking_motion;
}
