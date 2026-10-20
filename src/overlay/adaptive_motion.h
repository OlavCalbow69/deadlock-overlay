#pragma once
#include "focus_math.h"
#include <array>
#include <chrono>

namespace overlay {
inline double motion_clock_ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
enum class MotionSource : uint8_t { Unavailable, RootEstimate, EngineVerified };
inline const char* motion_source_name(MotionSource source) {
    return source==MotionSource::EngineVerified?"engine_verified":source==MotionSource::RootEstimate?"root_estimate":"unavailable";
}
struct MotionSample {
    Vec3 root{},velocity{};
    Vec3 engine_velocity{},root_velocity{};
    double measured_ms{};
    MotionSource source{};
    bool position_valid{},velocity_valid{},engine_candidate_valid{};
};
class RootMotionTracker {
    struct Measurement {Vec3 position{},engine{};double time{};bool engine_valid{};};
    std::array<Measurement,32> history_{};
    size_t count_{};
    Vec3 filtered_{};bool filtered_valid_{},engine_verified_{};
    unsigned agreements_{},disagreements_{};double agreement_start_{};
public:
    void reset(){*this={};}
    MotionSample update(Vec3 root,Vec3 engine,bool engine_valid,double now) {
        MotionSample result;result.root=root;result.measured_ms=now;result.position_valid=finite(root)&&std::isfinite(now)&&now>0;
        if(!result.position_valid){reset();return {};}
        engine_valid=engine_valid&&finite(engine)&&dot(engine,engine)<=10000.f*10000.f;
        result.engine_candidate_valid=engine_valid;result.engine_velocity=engine_valid?engine:Vec3{};
        if(count_) {
            const auto& last=history_[count_-1];const double gap=now-last.time;
            if(gap<=0)return result;
            if(gap>32||dot(sub(root,last.position),sub(root,last.position))>256.f*256.f)reset();
        }
        if(count_==history_.size()){std::move(history_.begin()+1,history_.end(),history_.begin());--count_;}
        history_[count_++]={root,engine,now,engine_valid};
        int baseline=-1;
        for(int i=int(count_)-2;i>=0;--i){const double age=now-history_[i].time;if(age>32)break;if(age>=16)baseline=i;}
        if(baseline<0)return result;
        const auto& first=history_[baseline];const double span=now-first.time;
        Vec3 displacement=sub(root,first.position),raw=mul(displacement,float(1000/span));
        if(!finite(raw)||dot(raw,raw)>10000.f*10000.f){reset();return result;}
        const double dt=count_>1?now-history_[count_-2].time:span;
        if(!filtered_valid_||dot(raw,filtered_)<0)filtered_=raw;
        else filtered_=add(filtered_,mul(sub(raw,filtered_),float(1-std::exp(-dt/15.))));
        filtered_valid_=true;
        result.root_velocity=filtered_;
        // Trust an engine field only after its integrated motion agrees with an
        // independent root-position read while the entity actually moves.
        Vec3 integrated{};bool all_engine=true;
        for(size_t i=size_t(baseline)+1;i<count_;++i) {
            const auto& a=history_[i-1];const auto& b=history_[i];all_engine&=a.engine_valid&&b.engine_valid;
            integrated=add(integrated,mul(add(a.engine,b.engine),float((b.time-a.time)/2000.)));
        }
        const float traveled=std::sqrt(dot(displacement,displacement));
        if(all_engine&&(traveled>=.5f||dot(integrated,integrated)>=.25f)) {
            const Vec3 error=sub(integrated,displacement);const float tolerance=1.5f+.25f*traveled;
            if(traveled>=.5f&&dot(error,error)<=tolerance*tolerance&&dot(integrated,displacement)>0
                &&dot(integrated,integrated)>=.25f*traveled*traveled) {
                if(!agreements_)agreement_start_=now;
                ++agreements_;disagreements_=0;
                if(agreements_>=3&&now-agreement_start_>=50)engine_verified_=true;
            }else if(dot(error,error)>tolerance*tolerance||dot(integrated,integrated)<.25f*traveled*traveled
                ||(traveled>=.5f&&dot(integrated,displacement)<=0)) {
                agreements_=0;++disagreements_;engine_verified_=false;
            }
        }else if(!all_engine){engine_verified_=false;agreements_=0;}
        result.velocity=engine_verified_&&engine_valid?engine:filtered_;
        result.velocity_valid=true;result.source=engine_verified_&&engine_valid?MotionSource::EngineVerified:MotionSource::RootEstimate;
        return result;
    }
};
}
