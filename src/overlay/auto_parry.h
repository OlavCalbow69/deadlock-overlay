#pragma once
#include "game_reader.h"
#include "focus_hitboxes.h"
#include "makcu_input.h"
#include "parry_settings.h"
#include <bit>
#include <optional>
#include <unordered_map>

namespace overlay {
namespace parry_geometry {
inline Vec3 nearest(const Hitbox& box,Vec3 p) {
    if(box.shape==HitboxShape::Box) {
        Vec3 answer=box.corners[0],offset=sub(p,answer);
        for(int bit:{1,2,4}) {
            auto edge=sub(box.corners[bit],box.corners[0]);float length=dot(edge,edge);
            if(length>1e-6f)answer=add(answer,mul(edge,std::clamp(dot(offset,edge)/length,0.f,1.f)));
        }
        return answer;
    }
    Vec3 center=box.a;
    if(box.shape==HitboxShape::Capsule) {
        auto axis=sub(box.b,box.a);float length=dot(axis,axis);
        if(length>1e-6f)center=add(center,mul(axis,std::clamp(dot(sub(p,center),axis)/length,0.f,1.f)));
    }
    auto delta=sub(p,center);float distance=std::sqrt(dot(delta,delta));
    return distance>box.radius&&distance>1e-6f?add(center,mul(delta,box.radius/distance)):p;
}
inline Hitbox expanded(Hitbox box,float radius) {
    if(box.shape!=HitboxShape::Box){box.radius+=radius;return box;}
    auto original=box.corners;
    for(int i=0;i<8;++i)for(int bit:{1,2,4}) {
        auto edge=sub(original[bit],original[0]);float length=std::sqrt(dot(edge,edge));
        if(length>1e-6f)box.corners[i]=add(box.corners[i],mul(edge,((i&bit)?radius:-radius)/length));
    }
    box.a=box.corners[0];box.b=box.corners[7];return box;
}
inline bool contact(const Hitbox& box,Vec3 shift,Vec3 origin,Vec3 forward,float range,float half_angle,Vec3& point) {
    if(!finite(origin)||!finite(forward)||!finite(shift)||!std::isfinite(range)||range<=0)return false;
    auto center=add(hitbox_center(box),shift);float distance=std::sqrt(dot(sub(center,origin),sub(center,origin)));
    // Select a point on the actual body primitive closest to the attack axis.
    // Padding used by camera focus never participates in defensive eligibility.
    auto axis_point=add(origin,mul(forward,std::clamp(distance,0.f,range)));
    auto candidate=add(nearest(box,sub(axis_point,shift)),shift),delta=sub(candidate,origin);
    float length=std::sqrt(dot(delta,delta));
    if(!finite(candidate)||length<.01f||length>range||dot(delta,forward)/length<std::cos(half_angle*.01745329252f))return false;
    point=candidate;return true;
}
}
struct ParryDecision {
    ParryThreatKind kind{};uint32_t source{},ability{};uint64_t key{};
    float impact_ms{};Vec3 origin{},point{};bool valid{};
    std::string name;
};
inline uint64_t parry_attack_key(const MeleeThreatSample& sample) {
    auto start=sample.activated>0?sample.activated:sample.state_start;
    return (uint64_t(sample.ability)<<32)^std::bit_cast<uint32_t>(start)^uint64_t(sample.pawn)*1099511628211ULL;
}
inline ParryDecision find_parry_threat(const Snapshot& snapshot,const AutoParrySettings& settings,uint64_t now) {
    ParryDecision best;best.impact_ms=100000;
    const auto& state=snapshot.parry;
    if(!state.valid||!snapshot.visibility||state.local_hitboxes.empty()||now<state.sampled||now-state.sampled>25)return best;
    const float age=float(now-state.sampled)*.001f,horizon=settings.lead_ms*.001f;
    const auto local_shift=mul(state.local_velocity,age);
    auto consider=[&](ParryDecision candidate) {
        if(candidate.valid&&candidate.impact_ms<best.impact_ms)best=std::move(candidate);
    };
    if(settings.melee)for(const auto& enemy:state.melee) {
        const bool heavy=enemy.attack_type>=2;
        if(!enemy.valid||!enemy.parryable||enemy.piercing||enemy.team==snapshot.local_team||enemy.team<2||enemy.team>3
            ||(heavy?!settings.heavy:!settings.light))continue;
        if(enemy.state!=2&&enemy.state!=3&&enemy.state!=4&&enemy.state!=5)continue; // Charging is cancellable.
        if(!heavy&&enemy.state!=4)continue;
        const float elapsed=state.game_time+age-enemy.trigger_time;
        if(enemy.state==4&&(enemy.hit||elapsed<-.10f||elapsed>std::clamp(enemy.attack_duration,.12f,.65f)))continue;
        bool found=false;
        for(float t=0;t<=horizon+.0001f&&!found;t+=.01f) {
            auto origin=add(enemy.origin,mul(enemy.velocity,age+t));
            auto shift=add(local_shift,mul(state.local_velocity,t));
            for(const auto& box:state.local_hitboxes) {
                Vec3 point;
                if(!parry_geometry::contact(box,shift,origin,enemy.direction,enemy.range,enemy.half_angle,point))continue;
                // Check the present and predicted path; a wall makes this ineligible.
                auto current_point=sub(point,shift);
                if(!snapshot.visibility->clear(enemy.origin,current_point)||!snapshot.visibility->clear(origin,point))continue;
                consider({heavy?ParryThreatKind::HeavyMelee:ParryThreatKind::LightMelee,enemy.pawn,enemy.ability,parry_attack_key(enemy),t*1000,origin,point,true,enemy.name});
                found=true;break;
            }
        }
    }
    if(settings.counterspell&&state.counterspell_owned&&(state.counterspell_ready||state.counterspell_active))for(const auto& projectile:state.projectiles) {
        if(projectile.team==snapshot.local_team||projectile.team<2||projectile.team>3)continue;
        auto velocity=sub(projectile.velocity,state.local_velocity);float speed=std::sqrt(dot(velocity,velocity));
        if(!finite(velocity)||speed<50||speed>50000)continue;
        auto origin=add(projectile.position,mul(projectile.velocity,age)),direction=mul(velocity,1/speed);
        for(const auto& shape:state.local_hitboxes) {
            // Box expansion is a conservative swept-box approximation; spheres
            // and capsules retain their analytical intersection geometry.
            auto box=parry_geometry::expanded(shape,projectile.radius);
            auto local_origin=sub(origin,local_shift),center=hitbox_center(box);
            if(dot(sub(center,local_origin),velocity)<=0||dot(sub(parry_geometry::nearest(box,local_origin),local_origin),sub(parry_geometry::nearest(box,local_origin),local_origin))<.01f)continue;
            double distance{};
            if(!hitbox_focus::intersect(box,{},local_origin,direction,distance))continue;
            const float t=float(distance)/speed;
            if(!std::isfinite(t)||t<0||t>horizon)continue;
            auto point=add(origin,mul(projectile.velocity,t));
            if(!snapshot.visibility->clear(projectile.position,point))continue;
            consider({ParryThreatKind::SpellProjectile,projectile.handle,0,(uint64_t(projectile.handle)<<32)^0x5350454c,t*1000,origin,point,true,projectile.name});
        }
    }
    return best;
}
class AutoParry {
    uint64_t last_sample_{},pending_{},pending_at_{},pending_key_{};
    uint32_t pid_{},local_{},pending_source_{};bool pending_melee_{};
    std::optional<AutoParrySettings> previous_settings_;
    std::unordered_map<uint64_t,uint64_t> submitted_;
    std::unordered_map<uint32_t,uint64_t> attacker_until_;
public:
    ParryDecision decision;
    std::string status{"Off"};uint64_t observed{},confirmed_inputs{};
    void cancel(MakcuInput& input,const char* reason="Off") {
        input.cancel_parry();pending_=pending_at_=pending_key_=0;pending_source_=0;pending_melee_=false;decision={};status=reason;
    }
    void update(MakcuInput& input,const Snapshot& snapshot,HWND game,const AutoParrySettings& settings,bool menu) {
        if(!settings.enabled){cancel(input);return;}
        if(!previous_settings_||*previous_settings_!=settings||pid_!=snapshot.pid||local_!=snapshot.parry.local_handle||snapshot.time<last_sample_) {
            cancel(input);submitted_.clear();attacker_until_.clear();last_sample_=0;previous_settings_=settings;pid_=snapshot.pid;local_=snapshot.parry.local_handle;
        }
        if(menu||!game||!IsWindow(game)||IsIconic(game)||GetForegroundWindow()!=game||GetAsyncKeyState(VK_ESCAPE)&0x8000){cancel(input,"Paused / game not focused");return;}
        if(snapshot.replay||!snapshot.local_alive){cancel(input,"Waiting for a living local player");return;}
        const auto now=GetTickCount64();
        if(!snapshot.pid||!snapshot.time||now<snapshot.time||now-snapshot.time>25||now<snapshot.parry.sampled||now-snapshot.parry.sampled>25){cancel(input,"Waiting for fresh parry data");return;}
        if(snapshot.parry.paused){cancel(input,"Paused / game clock stopped");return;}
        if(!snapshot.parry.valid){cancel(input,snapshot.parry.status.c_str());return;}
        if(!snapshot.visibility){cancel(input,"Waiting for map collision mesh");return;}
        if(!settings.observe_only&&!input.ready()){cancel(input,"Waiting for input");return;}
        auto binding=calibration_binding(settings.key);
        if(!binding||binding.mouse){cancel(input,"Choose a supported keyboard parry key");return;}
        if(pending_) {
            if(input.parry_sent_id()>=pending_){++confirmed_inputs;submitted_[pending_key_]=now;
                if(pending_melee_)attacker_until_[pending_source_]=now+500;
                pending_=pending_at_=pending_key_=0;pending_source_=0;pending_melee_=false;status="Parry sent";}
            else if(now-pending_at_<45){status="Parry queued";return;}
            else {input.cancel_parry();pending_=pending_at_=pending_key_=0;status="Input expired / rechecking";}
        }
        if(GetAsyncKeyState(binding.vk)&0x8000){input.cancel_parry();decision={};status=input.parry_pulse_active()?"Parry sent":"Parry key already held";return;}
        if(snapshot.time==last_sample_)return;
        last_sample_=snapshot.time;
        for(auto it=submitted_.begin();it!=submitted_.end();)it=now-it->second>5000?submitted_.erase(it):std::next(it);
        for(auto it=attacker_until_.begin();it!=attacker_until_.end();)it=now>=it->second?attacker_until_.erase(it):std::next(it);
        decision=find_parry_threat(snapshot,settings,now);
        if(!decision.valid){input.cancel_parry();status=settings.melee&&snapshot.parry.replicated_readers&&!snapshot.parry.melee_definition_valid?snapshot.parry.melee_definition_status
            :settings.counterspell&&!snapshot.parry.counterspell_owned?"No incoming melee / Counterspell not owned":snapshot.parry.unavailable_melee?"No incoming threat / some melee data unavailable":"Watching for an incoming attack";return;}
        if((decision.kind==ParryThreatKind::SpellProjectile?snapshot.parry.counterspell_active:snapshot.parry.active)) {
            input.cancel_parry();status="Current parry window already protects this threat";return;
        }
        if(!snapshot.parry.ready){input.cancel_parry();status=snapshot.parry.status;return;}
        if(decision.kind==ParryThreatKind::SpellProjectile&&!snapshot.parry.counterspell_ready){input.cancel_parry();status="Counterspell cooldown";return;}
        if(submitted_.contains(decision.key)||(decision.kind!=ParryThreatKind::SpellProjectile&&attacker_until_.contains(decision.source))) {
            input.cancel_parry();status="Already handled this attack";return;
        }
        if(settings.observe_only){status="Would parry / observation only";return;}
        if(now<input.last_parry()+250){input.cancel_parry();status="Parry repeat interval";return;}
        input.cancel_click();
        pending_=input.parry(game,binding,snapshot.parry.sampled+25,false);
        if(pending_){pending_at_=now;pending_key_=decision.key;pending_source_=decision.source;
            pending_melee_=decision.kind!=ParryThreatKind::SpellProjectile;++observed;status="Parry queued";
        }else status="Waiting for parry input";
    }
};
}
