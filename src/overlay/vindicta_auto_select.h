#pragma once
#include "assassinate_data.h"
#include "adaptive_response.h"
#include "ray_click.h"
#include <random>

namespace overlay {
struct ActualCrosshairReference {
    decltype(focus_reference_offset) saved{focus_reference_offset};
    ActualCrosshairReference(){focus_reference_offset={};}
    ~ActualCrosshairReference(){focus_reference_offset=saved;}
};
inline bool auto_select_region(const Hitbox& box) {
    return box.region==AnatomicalRegion::Head||box.region==AnatomicalRegion::Torso||box.region==AnatomicalRegion::Pelvis;
}
inline bool auto_select_shot_lethal(const Snapshot& s,const Player& p,const CrosshairHit& hit,uint64_t now,float* damage_out=nullptr) {
    if(!s.assassinate.valid||!s.assassinate.cast_allowed||!s.helpers.sniper_ready||!s.hero.sniper_scoped||s.helpers.paused
        ||!assassinate_health_eligible(p.health,p.maximum)||!enemy_teams(s.local_team,p.team)
        ||hit.state!=CrosshairState::Enemy||hit.target!=p.handle||hit.hitbox<0||size_t(hit.hitbox)>=p.hitboxes.size()
        ||!auto_select_region(p.hitboxes[size_t(hit.hitbox)])||hit.distance>s.assassinate.range)return false;
    for(auto sampled:{s.time,s.assassinate.sampled,p.assassinate_defense.sampled})if(!sampled||now<sampled||now-sampled>25)return false;
    if(now>hit.valid_until)return false;
    const float charge=assassinate_ring(s.assassinate,s.helpers.game_time,s.hero.scope_start,s.helpers.sniper_minimum);
    const float damage=assassinate_damage(s.assassinate,p.assassinate_defense,p.health,p.maximum,charge,assassinate_head_hit(p.hitboxes[size_t(hit.hitbox)].group));
    if(damage_out)*damage_out=damage;return damage>=float(p.health);
}
struct AutoSelectSolution {
    uint32_t target{};uint64_t source_id{};int hitbox{-1},health{},maximum{};
    Vec3 point{};float angle{},required{},charge{},damage{},full_damage{};
    bool valid{},head{},inside{},estimated{},retained{};
    std::string hero,omissions;
};
inline bool auto_select_better(const AutoSelectSolution& a,const AutoSelectSolution& b) {
    if(!b.valid)return a.valid;
    if(std::abs(a.required-b.required)>.0001f)return a.required<b.required;
    if(std::abs(a.angle-b.angle)>.0001f)return a.angle<b.angle;
    return a.health<b.health;
}
inline AutoSelectSolution auto_select_solution(const Snapshot& s,const Player& player,float angle,bool acquire,uint64_t previous=0) {
    ActualCrosshairReference actual;AutoSelectSolution best;
    if(!s.assassinate.valid||!assassinate_health_eligible(player.health,player.maximum)||!enemy_teams(s.local_team,player.team)
        ||!s.camera_valid||!s.visibility||!player.assassinate_defense.sampled||player.assassinate_defense.protected_state)return best;
    CameraBasis basis;if(!camera_basis(s.matrix,basis))return best;
    Matrix angular=s.matrix;
    for(int row:{0,4,12}){const float length=std::sqrt(angular[row]*angular[row]+angular[row+1]*angular[row+1]+angular[row+2]*angular[row+2]);for(int k=0;k<4;++k)angular[row+k]/=length;}
    hitbox_focus::Projection projection(angular,2,2,{1,1});
    AutoSelectSolution prior;
    for(size_t i=0;i<player.hitboxes.size();++i) {
        const auto& box=player.hitboxes[i];if(!auto_select_region(box))continue;
        const bool head=assassinate_head_hit(box.group);
        const float required=assassinate_required_charge(s.assassinate,player.assassinate_defense,player.health,player.maximum,head);
        if(!std::isfinite(required)||(acquire&&!assassinate_acquirable(required)))continue;
        double time{};const bool inside=hitbox_focus::intersect(box,{},basis.origin,basis.forward,time);
        Vec3 world{};
        if(inside)world=add(basis.origin,mul(basis.forward,float(time)));
        else {hitbox_focus::Point nearest;if(!hitbox_focus::closest(projection,box,{},nearest,1e-6))continue;world=nearest.world;}
        const auto center=hitbox_center(box);bool found=false;float best_angle=angle;Vec3 point{};
        auto consider=[&](Vec3 value,bool inset) {
            if(inset)value=add(value,mul(sub(center,value),.05f));
            const auto error=direction_error(basis,sub(value,basis.origin));const auto distance=sub(value,basis.origin);
            if(!std::isfinite(error.distance)||error.distance>best_angle||dot(distance,distance)>s.assassinate.range*s.assassinate.range
                ||!s.visibility->clear(basis.origin,value))return;
            if(!found||error.distance<best_angle){found=true;best_angle=error.distance;point=value;}
        };
        consider(world,!inside);
        if(!found) {
            consider(center,false);
            constexpr std::array<std::array<double,2>,8> directions={{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}};
            for(const auto& direction:directions){hitbox_focus::Point support;if(projection.support(box,{},direction[0],direction[1],support))consider(support.world,true);}
        }
        if(!found)continue;
        AutoSelectSolution candidate;candidate.target=player.handle;candidate.source_id=box.source_id;candidate.hitbox=int(i);
        candidate.point=point;candidate.angle=best_angle;candidate.required=required;candidate.head=head;candidate.inside=inside&&best_angle<.0001f;
        candidate.health=player.health;candidate.maximum=player.maximum;candidate.hero=player.hero_name.empty()?player.model_name:player.hero_name;
        candidate.charge=assassinate_ring(s.assassinate,s.helpers.game_time,s.hero.scope_start,s.helpers.sniper_minimum);
        candidate.damage=assassinate_damage(s.assassinate,player.assassinate_defense,player.health,player.maximum,candidate.charge,head);
        candidate.full_damage=assassinate_damage(s.assassinate,player.assassinate_defense,player.health,player.maximum,1,head);
        candidate.omissions=s.assassinate.omissions;if(!player.assassinate_defense.omissions.empty()){if(!candidate.omissions.empty())candidate.omissions+=", ";candidate.omissions+=player.assassinate_defense.omissions;}
        candidate.estimated=s.assassinate.estimated||!candidate.omissions.empty();candidate.valid=true;
        if(auto_select_better(candidate,best))best=candidate;
        if(box.source_id==previous&&auto_select_better(candidate,prior))prior=candidate;
    }
    if(prior.valid&&best.valid&&std::abs(prior.required-best.required)<=.0001f&&prior.angle<=best.angle+.1f)return prior;
    return best;
}
enum class AutoSelectPhase { Monitoring,ScopeRequested,Charging,ShotPending,ExitScope };
inline const char* auto_select_phase_name(AutoSelectPhase phase) {
    switch(phase){case AutoSelectPhase::ScopeRequested:return "Scoping";case AutoSelectPhase::Charging:return "Aiming / charging";
    case AutoSelectPhase::ShotPending:return "Confirming shot";case AutoSelectPhase::ExitScope:return "Exiting scope";default:return "Monitoring";}
}
class VindictaAutoSelect {
    uint64_t session_{},next_bindings_{},release_at_{},deadline_{},reacquire_at_{},last_snapshot_{},data_pause_at_{},projection_pause_at_{};
    uint32_t target_{},ability_{},pid_{};float scope_start_{},blocked_scope_{-1},shot_cast_{};int shot_charges_{};
    bool owns_scope_{};CalibrationBinding pending_release_;std::string bindings_hero_;
    AssassinateScopeContinuity scope_,blocked_;
    AdaptiveMouseResponse response_;std::mt19937 random_{std::random_device{}()};
    void block_scope(float started){blocked_scope_=started;blocked_.reset(started);}
    bool pulse(MakcuInput& input,const CalibrationBinding& binding,uint64_t now) {
        if(!session_||pending_release_||!binding||physical_key_down(binding.vk))return false;
        if(!input.sequence_key(session_,binding,true))return false;
        pending_release_=binding;release_at_=now+40;return true;
    }
    void finish(MakcuInput& input,const char* reason,uint64_t now) {
        if(session_)input.end_sequence();session_=0;target_=ability_=0;owns_scope_=false;pending_release_={};
        phase=AutoSelectPhase::Monitoring;response_.clear();scope_.reset();data_pause_at_=projection_pause_at_=0;reacquire_at_=now+100;status=reason;solution={};
    }
    void end_target(MakcuInput& input,const Snapshot& s,const char* reason,uint64_t now) {
        input.cancel_click();input.cancel_motion();response_.clear();target_=0;solution={};status=reason;
        if(owns_scope_&&(!s.hero.sniper_scoped||scope_start_==0||std::abs(s.hero.scope_start-scope_start_)<=.25f||phase==AutoSelectPhase::ScopeRequested)) {
            phase=AutoSelectPhase::ExitScope;deadline_=0;
            if(s.hero.sniper_scoped)scope_start_=s.hero.scope_start;
            scope_.reset(scope_start_);
        }
        else finish(input,reason,now);
    }
public:
    CalibrationBindings bindings;AutoSelectSolution solution;
    AutoSelectPhase phase{AutoSelectPhase::Monitoring};std::string status{"Disabled"};
    uint64_t shots{},moves{},scope_exits{};int candidates{};
    bool active()const{return session_!=0||phase!=AutoSelectPhase::Monitoring;}
    bool owns_scope()const{return owns_scope_;}
    bool owns_key(int key)const{return pending_release_&&pending_release_.vk==key;}
    void reset_response(){response_.clear();}
    void stop(MakcuInput& input,const char* reason="Stopped") {finish(input,reason,GetTickCount64());last_snapshot_=0;}
    void maintain(MakcuInput& input) {
        if(pending_release_&&GetTickCount64()>=release_at_) {
            if(session_&&!input.sequence_key(session_,pending_release_,false)){stop(input,"Input release failed");return;}
            pending_release_={};
        }
    }
    void tick(MakcuInput& input,const Snapshot& s,const VindictaAutoSelectSettings& settings,HWND game,bool allowed,bool parry) {
        const auto now=GetTickCount64();maintain(input);
        if(!settings.enabled){if(active())stop(input,"Disabled");status="Disabled";solution={};return;}
        if(!allowed||!game||!IsWindow(game)||IsIconic(game)||GetForegroundWindow()!=game||!gameplay_cursor_hidden()||!input.ready()||s.replay
            ||!s.pid||(GetAsyncKeyState(VK_ESCAPE)&0x8000)||parry||input.parry_pulse_active()) {
            if(active())stop(input,parry?"Yielding to Auto Parry":"Paused / input cancelled");
            status=parry?"Yielding to Auto Parry":"Paused / game, input or data unavailable";solution={};return;
        }
        if(s.pid==pid_&&s.status=="Waiting for a stable render view") {
            if(session_)input.end_sequence();session_=0;pending_release_={};response_.clear();
            if(!projection_pause_at_)projection_pause_at_=now;
            if(now-projection_pause_at_>=500){stop(input,"Render view unavailable / inputs cancelled");return;}
            status="Paused / waiting for a coherent render view";return;
        }
        projection_pause_at_=0;
        if(!s.local_alive||!s.time||now<s.time||now-s.time>50) {
            if(active())stop(input,"Stale data / inputs cancelled");status="Waiting for fresh local-player data";return;
        }
        if(!vindicta(s.hero)){if(active())stop(input,"Vindicta required");status="Vindicta required";solution={};return;}
        if(pid_!=s.pid||bindings_hero_!=s.hero.token) {if(active())stop(input,"Hero / game changed");pid_=s.pid;next_bindings_=0;blocked_scope_=-1;}
        if(now>=next_bindings_){bindings=find_calibration_bindings(s.hero.token,game);bindings_hero_=s.hero.token;next_bindings_=now+5000;}
        if(!bindings.ultimate||!bindings.attack){if(active())stop(input,"Ultimate or attack binding unavailable");status="Ultimate or attack binding unavailable";return;}
        if(session_){if(!input.calibration_live(session_)||input.calibration_error()){stop(input,"Input cancelled / backend changed");return;}input.keep_sequence(session_);}
        else if(phase!=AutoSelectPhase::Monitoring) {
            session_=input.begin_sequence(game);if(!session_){status="Waiting for input cleanup";return;}
        }
        if(s.time==last_snapshot_)return;
        const float dt=last_snapshot_&&s.time>last_snapshot_?float(s.time-last_snapshot_)/1000:.004f;last_snapshot_=s.time;
        RECT rect{};if(!GetClientRect(game,&rect)||rect.right<=0||rect.bottom<=0){stop(input,"Game view unavailable");return;}
        ActualCrosshairReference actual;
        if(phase==AutoSelectPhase::ExitScope) {
            if(!s.hero.sniper_valid){input.cancel_motion();input.cancel_click();status="Waiting for scope state";
                if(!data_pause_at_)data_pause_at_=now;if(now-data_pause_at_>=500)finish(input,"Scope state unavailable / inputs cancelled",now);return;}
            data_pause_at_=0;
            if(scope_start_==0) {
                if(s.hero.sniper_scoped)scope_start_=s.hero.scope_start;
                else {if(!deadline_)deadline_=now+750;if(now>=deadline_)finish(input,"Scope request cancelled",now);return;}
                deadline_=0;
            }
            const auto observation=scope_.observe(s.hero.sniper_scoped,s.hero.scope_start,now);
            if(observation==AssassinateScopeObservation::Waiting){status="Confirming scope exit";return;}
            if(observation==AssassinateScopeObservation::Closed||observation==AssassinateScopeObservation::Changed) {
                if(observation==AssassinateScopeObservation::Closed)++scope_exits;finish(input,"Monitoring",now);return;
            }
            scope_start_=scope_.started;
            if(deadline_&&now>deadline_){block_scope(s.hero.scope_start);finish(input,"Scope exit not confirmed / leave scope to retry",now);return;}
            if(!deadline_&&!pending_release_&&pulse(input,bindings.ultimate,now))deadline_=now+1000;
            return;
        }
        if(phase==AutoSelectPhase::ShotPending) {
            if((s.helpers.sniper_valid&&s.helpers.charges<shot_charges_)
                ||(s.assassinate.ability==ability_&&std::isfinite(s.assassinate.cast_completed)&&s.assassinate.cast_completed>shot_cast_+.0001f)) {
                ++shots;end_target(input,s,"Shot confirmed",now);return;
            }
            if(now>deadline_){block_scope(s.hero.scope_start);end_target(input,s,"Shot not confirmed / leave scope to retry",now);return;}
        }
        if(!s.helpers.time_valid||s.helpers.paused||!s.helpers.sniper_valid||!s.assassinate.valid||now<s.assassinate.sampled||now-s.assassinate.sampled>50) {
            input.cancel_motion();input.cancel_click();response_.clear();
            // A transient cache miss pauses correction; it must not turn a
            // scope we opened into an apparently manual scope on the next tick.
            if(active()) {
                if(!data_pause_at_)data_pause_at_=now;
                if(now-data_pause_at_>=500){end_target(input,s,"Damage data unavailable / ending engagement",now);return;}
            }
            status=s.assassinate.status;return;
        }
        data_pause_at_=0;
        if(blocked_scope_>=0) {
            const auto state=blocked_.observe(s.hero.sniper_scoped,s.hero.scope_start,now);
            if(state==AssassinateScopeObservation::Same||state==AssassinateScopeObservation::Waiting){status="Leave scope to retry the unconfirmed command";return;}
            blocked_scope_=-1;
        }
        if(ability_&&ability_!=s.assassinate.ability){stop(input,"Assassinate ability changed");return;}
        candidates=0;
        AutoSelectSolution best;
        if(target_) {
            const auto it=std::find_if(s.players.begin(),s.players.end(),[&](const auto& p){return p.handle==target_;});
            if(it!=s.players.end())best=auto_select_solution(s,*it,settings.angle,false,solution.source_id);
            if(!best.valid){end_target(input,s,"Target no longer eligible",now);return;}best.retained=true;
        }else {
            unsigned ties=0;
            for(const auto& player:s.players) {
                if(assassinate_health_eligible(player.health,player.maximum)&&enemy_teams(s.local_team,player.team))++candidates;
                auto candidate=auto_select_solution(s,player,settings.angle,true);
                if(!candidate.valid)continue;
                if(auto_select_better(candidate,best)){best=std::move(candidate);ties=1;}
                else if(!auto_select_better(best,candidate)&&std::uniform_int_distribution<unsigned>(1,++ties)(random_)==1)best=std::move(candidate);
            }
        }
        solution=std::move(best);
        if(!solution.valid){status="Monitoring / no visible lethal target below 49%";return;}
        if(phase==AutoSelectPhase::ShotPending) {
            auto hit=crosshair_hit(s,float(rect.right),float(rect.bottom),{rect.right*.5f,rect.bottom*.5f});
            auto player=std::find_if(s.players.begin(),s.players.end(),[&](const auto& p){return p.handle==target_;});
            const bool valid=player!=s.players.end()&&auto_select_shot_lethal(s,*player,hit,now);
            if(!valid){input.cancel_click();end_target(input,s,"Shot conditions changed",now);}return;
        }
        if(!s.assassinate.cast_allowed){if(active())end_target(input,s,"Ability interrupted",now);else status="Ability interrupted / cannot cast";return;}
        if(!target_) {
            if(now<reacquire_at_){status="Reacquisition delay";return;}
            if(s.helpers.charges<=0||(!s.hero.sniper_scoped&&!s.helpers.sniper_can_scope)){status=s.helpers.sniper_status;return;}
            if(!session_)session_=input.begin_sequence(game);
            if(!session_){status="Waiting for input ownership";return;}
            target_=solution.target;ability_=s.assassinate.ability;response_.clear();
            if(!s.hero.sniper_scoped) {
                if(!pulse(input,bindings.ultimate,now)){finish(input,"Scope binding busy / retrying",now);return;}
                owns_scope_=true;scope_start_=0;phase=AutoSelectPhase::ScopeRequested;deadline_=now+1000;status="Scope requested";return;
            }
            scope_start_=s.hero.scope_start;scope_.reset(scope_start_);phase=AutoSelectPhase::Charging;
        }
        if(phase==AutoSelectPhase::ScopeRequested) {
            if(s.hero.sniper_scoped){scope_start_=s.hero.scope_start;scope_.reset(scope_start_);phase=AutoSelectPhase::Charging;response_.clear();}
            else {if(now>deadline_)finish(input,"Scope not confirmed",now);else status="Waiting for scope";return;}
        }
        const auto observation=scope_.observe(s.hero.sniper_scoped,s.hero.scope_start,now);
        if(observation==AssassinateScopeObservation::Waiting){input.cancel_motion();input.cancel_click();response_.clear();status="Confirming scope state";return;}
        if(observation!=AssassinateScopeObservation::Same){finish(input,"Scope changed",now);return;}
        scope_start_=scope_.started;
        const auto player=std::find_if(s.players.begin(),s.players.end(),[&](const auto& p){return p.handle==target_;});
        if(player==s.players.end()){end_target(input,s,"Target lost",now);return;}
        const auto hit=crosshair_hit(s,float(rect.right),float(rect.bottom),{rect.right*.5f,rect.bottom*.5f});
        if(hit.state==CrosshairState::Enemy&&hit.target==target_&&hit.hitbox>=0&&size_t(hit.hitbox)<player->hitboxes.size()
            &&auto_select_region(player->hitboxes[size_t(hit.hitbox)])&&s.helpers.sniper_ready&&!pending_release_) {
            const auto until=std::min({hit.valid_until,s.assassinate.sampled+25,player->assassinate_defense.sampled+25});
            if(auto_select_shot_lethal(s,*player,hit,now)&&!physical_key_down(bindings.attack.vk)) {
                input.cancel_motion();response_.clear();
                if(input.sequence_shot(game,bindings.attack,until,session_)) {
                    shot_charges_=s.helpers.charges;shot_cast_=s.assassinate.cast_completed;phase=AutoSelectPhase::ShotPending;deadline_=now+750;status="Lethal estimate / shot requested";
                }else status="Waiting for shot input / interval";
                return;
            }
        }
        if(pending_release_){status="Waiting for scope key release";return;}
        if(solution.inside){input.cancel_motion();response_.clear();status="Aligned / waiting for lethal charge";return;}
        AdaptiveSettings response;response.speed=settings.speed;
        const auto step=response_.step(s.matrix,solution.point,float(rect.right),float(rect.bottom),dt,response,settings.speed);
        if(step.valid&&(step.x||step.y)) {if(input.sequence_move(game,step.x,step.y,session_))++moves;else {stop(input,"Movement input rejected");return;}}
        status=solution.charge<solution.required?"Aiming / charging toward lethal damage":"Aiming at lethal head / torso";
    }
};
}
