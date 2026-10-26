#pragma once
#include "adaptive_focus.h"

namespace overlay {
// Use the normal precise region/line solution in front of the camera. When a
// volume cannot be projected, sample points belonging to that same geometry in
// world space, so offscreen and rear targets can still authorize a turn.
inline AdaptivePoint x2_lock_point(const Snapshot& s,const FocusTarget& target,const CameraFocusSettings& settings,
    Vec3 lead={},uint64_t previous_id=0,bool inset=true) {
    CameraBasis basis;if(!camera_basis(s.matrix,basis)||!finite(lead))return {};
    auto allowed=[&](Vec3 point) {
        return finite(point)&&dot(sub(point,basis.origin),sub(point,basis.origin))>1e-10f
            &&visibility_allowed(settings.visible_only,s.visibility,s.camera_position,s.camera_valid,sub(point,lead))
            &&(dot(lead,lead)<1e-10f||visibility_allowed(settings.visible_only,s.visibility,s.camera_position,s.camera_valid,point));
    };
    if(adaptive_mode(settings)&&target.kind!=TargetKind::SoulOrb) {
        auto point=adaptive_closest(s,target,settings.adaptive,settings.focus_bone,lead,settings.visible_only,previous_id,inset);
        if(point.valid)return point;
    }else {
        Vec3 world;float line{};bool inside{};
        if(focus_point(s.matrix,target,settings.focus_bone,settings.free_focus,2,2,focus_reference(2,2),lead,world,line,settings.free_focus_mode,nullptr,&inside)&&allowed(world)) {
            const auto error=direction_error(basis,sub(world,basis.origin));
            return {world,error,0,AnatomicalRegion::Unknown,-1,true,inside,true,error.distance};
        }
    }
    AdaptivePoint best,previous,preferred;
    auto consider=[&](Vec3 world,uint64_t id,AnatomicalRegion region,int index) {
        if(!allowed(world))return;
        const auto error=direction_error(basis,sub(world,basis.origin));if(!std::isfinite(error.distance))return;
        AdaptivePoint point{world,error,id,region,index,true,false,true,error.distance};
        if(!best.valid||point.distance<best.distance)best=point;
        if(id==previous_id&&(!previous.valid||point.distance<previous.distance))previous=point;
        if(region==preferred_region(settings.focus_bone)&&(!preferred.valid||point.distance<preferred.distance))preferred=point;
    };
    if(target.kind==TargetKind::SoulOrb) {
        if(target.dot_valid[1])consider(add(target.dots[1],lead),0,AnatomicalRegion::Unknown,-1);
        return best;
    }
    if(adaptive_mode(settings)) {
        for(size_t i=0;i<target.hitboxes.size();++i) {
            const auto& box=target.hitboxes[i];float low{},high{};
            if(!adaptive_permits(target,box,settings.adaptive,settings.focus_bone)||!target_zone::bounds(box,low,high))continue;
            const auto center=add(hitbox_center(box),lead);
            auto sample=[&](Vec3 point){point=add(point,lead);if(inset)point=add(point,mul(sub(center,point),.05f));consider(point,box.source_id,box.region,int(i));};
            sample(hitbox_center(box));
            if(box.shape==HitboxShape::Box)for(auto corner:box.corners)sample(corner);
            else {sample(box.a);if(box.shape==HitboxShape::Capsule)sample(box.b);}
        }
    }else if(settings.free_focus&&target.dot_valid[0]&&target.dot_valid[1]&&target.dot_valid[2]) {
        // The fallback stays on the real head/body/pelvis polyline.
        for(int i=0;i<5;++i)consider(add(focus_line(target.dots,float(i)*.5f),lead),0,AnatomicalRegion::Unknown,-1);
    }
    if(best.valid) {
        const float nearest=best.distance;
        if(previous.valid&&previous.distance<=nearest+.1f)best=previous;
        else if(preferred.valid&&preferred.distance<=nearest+.1f)best=preferred;
        best.distance=nearest;
    }
    return best;
}

inline AdaptiveFocusSolution build_x2_lock_solution(const Snapshot& s,const FocusTarget& target,const CameraFocusSettings& settings,
    bool sniper,double now,double queue_ms,uint64_t previous_id=0) {
    AdaptiveFocusSolution result;result.target=target.handle;result.kind=target.kind;result.motion_source=target.motion.source;
    const auto base=x2_lock_point(s,target,settings,{},previous_id,false);
    if(!base.valid)return result;
    result.acquisition_angle=base.distance;
    auto point=x2_lock_point(s,target,settings,{},previous_id);Vec3 lead{};
    const bool adaptive=adaptive_mode(settings),prediction=sniper||(adaptive?settings.adaptive.prediction:settings.prediction);
    result.prediction_status=prediction?"Prediction waiting for motion":"Prediction off";
    const auto& motion=target.motion;const double age=now-motion.measured_ms;
    if(prediction&&motion.velocity_valid&&finite(motion.velocity)&&age>=0&&age<=50) {
        const bool automatic=adaptive||settings.auto_speed;
        const float speed=automatic?s.weapon.speed:settings.projectile_speed;
        const float inheritance=(sniper||(settings.tracking.enabled&&settings.tracking.fixed_lead))?0:automatic?s.weapon.inheritance:settings.inherit_velocity?1.f:0.f;
        const bool projectile_valid=sniper||!automatic||s.weapon.valid;
        const bool local_valid=inheritance==0||s.local_motion.velocity_valid||s.local_velocity_valid;
        if(projectile_valid&&local_valid) {
            const auto local=s.local_motion.velocity_valid?s.local_motion.velocity:s.local_velocity;
            const float data_age=float(age+(std::isfinite(queue_ms)?std::clamp(queue_ms,0.,20.):0.));
            const auto age_lead=mul(motion.velocity,data_age/1000.f),relative=sub(motion.velocity,mul(local,inheritance));
            lead=age_lead;CameraBasis basis;camera_basis(s.matrix,basis);bool solved=true,blocked=false;
            float flight{};
            for(int iteration=0;iteration<3;++iteration) {
                auto next=x2_lock_point(s,target,settings,lead,previous_id);
                if(!next.valid){solved=false;blocked=true;break;}point=next;flight=0;
                if(!sniper){if(settings.tracking.enabled&&settings.tracking.fixed_lead)flight=settings.tracking.lead;else if(!intercept(basis.origin,add(sub(point.world,lead),age_lead),relative,speed,flight)){solved=false;break;}}
                const auto next_lead=add(age_lead,mul(relative,flight));
                if(dot(sub(next_lead,lead),sub(next_lead,lead))<.001f){lead=next_lead;break;}lead=next_lead;
            }
            if(solved) {
                point=x2_lock_point(s,target,settings,lead,previous_id);
                if(point.valid){result.prediction_active=true;result.flight_time=flight;result.data_age_ms=data_age;
                    result.prediction_status=sniper?"Hitscan / data-age compensation":"Automatic prediction active";}
            }else if(blocked){point={};result.prediction_status="Predicted point blocked";}
            else {lead={};point=x2_lock_point(s,target,settings,{},previous_id);result.prediction_status="No valid intercept / current geometry";}
        }else result.prediction_status=projectile_valid?"Local motion unavailable / current geometry":"Projectile data unavailable / current geometry";
    }
    result.valid=point.valid;result.inside=point.inside;result.current_point=sub(point.world,lead);result.aim_point=point.world;
    result.lead=lead;result.error=point.error;result.source_id=point.source_id;result.region=point.region;result.retained=point.valid;
    if(result.valid&&adaptive)for(const auto& box:target.hitboxes)
        if(adaptive_permits(target,box,settings.adaptive,settings.focus_bone)&&result.box_count<result.boxes.size())result.boxes[result.box_count++]=box;
    return result;
}

// The latch survives controller/profile resets. Only a physical X2 release
// rearms acquisition; a missing target never permits selecting a replacement.
class X2LockSession {
public:
    enum class State { Idle,Acquire,Locked,Spent };
private:
    State state_{State::Idle};bool down_{},active_{};uint32_t target_{},pid_{};TargetKind kind_{};
public:
    std::string status{"Press X2 to lock a target"};
    bool active()const{return active_;}
    State state()const{return state_;}
    uint32_t target()const{return state_==State::Locked?target_:0;}
    void buttons(bool held,bool enabled,bool owned=false) {
        if(owned) {
            // Suppression makes Windows input ambiguous. Consume this hold until
            // an unowned release, rather than treating it as a new physical edge.
            down_=true;active_=enabled||active_;
            lose("Program input / release X2 to lock");return;
        }
        const bool pressed=held&&!down_;down_=held;
        // A scope/profile change cannot hand a consumed X2 hold to another controller.
        active_=held&&(enabled||active_);
        if(!active_){state_=State::Idle;target_=pid_=0;kind_=TargetKind::None;status="Press X2 to lock a target";return;}
        if(pressed){state_=State::Acquire;target_=pid_=0;kind_=TargetKind::None;status="Selecting X2 lock target";}
        else if(state_==State::Idle)lose("Release X2, then press it to lock");
    }
    void lose(const char* reason) {
        target_=pid_=0;kind_=TargetKind::None;
        if(active_){state_=State::Spent;status=reason;}
        else {state_=State::Idle;status="Press X2 to lock a target";}
    }
    const FocusTarget* select(const Snapshot& s,const CameraFocusSettings& settings,AdaptivePoint& point,uint64_t previous_id=0) {
        if(!active_||state_==State::Spent)return nullptr;
        const FocusOptions options{settings.focus_players,settings.focus_minions,settings.focus_orbs,settings.strict_enemy_players};
        if(state_==State::Locked) {
            const FocusTarget* selected=nullptr;
            if(s.pid==pid_) {
                for(const auto& p:s.players)if(p.handle==target_&&p.kind==kind_){selected=&p;break;}
                if(!selected)for(const auto& p:s.focus_targets)if(p.handle==target_&&p.kind==kind_){selected=&p;break;}
            }
            if(!selected||!focus_candidate(*selected,s,options,settings.exclude_teammates)){lose("Lock target lost / release X2 to select again");return nullptr;}
            point=x2_lock_point(s,*selected,settings,{},previous_id,false);
            if(!point.valid){lose("Lock point unavailable / release X2 to select again");return nullptr;}
            return selected;
        }
        const FocusTarget* selected=nullptr;double best=std::numeric_limits<double>::max();int lowest=INT_MAX;unsigned ties{};
        auto consider=[&](const FocusTarget& candidate) {
            if(!focus_candidate(candidate,s,options,settings.exclude_teammates))return;
            auto current=x2_lock_point(s,candidate,settings,{},0,false);if(!current.valid)return;
            const double distance=std::round(double(current.distance)*1000.);
            const int health=candidate.maximum>0&&candidate.health>=0?candidate.health:INT_MAX;
            if(distance<best||(distance==best&&health<lowest)){selected=&candidate;point=current;best=distance;lowest=health;ties=1;}
            else if(distance==best&&health==lowest&&std::uniform_int_distribution<unsigned>(1,++ties)(focus_random())==1){selected=&candidate;point=current;}
        };
        for(const auto& p:s.players)consider(p);for(const auto& p:s.focus_targets)consider(p);
        if(!selected){lose("No eligible lock target / release X2 to try again");return nullptr;}
        state_=State::Locked;target_=selected->handle;pid_=s.pid;kind_=selected->kind;status="X2 target locked / 360 degrees";return selected;
    }
};

class X2TargetLock {
    X2LockSession session_;AdaptiveMouseResponse response_;
    double last_sample_{-1},last_motion_{};float remainder_x_{},remainder_y_{},response_speed_{};
    bool response_adaptive_{},response_sniper_{};int response_bone_{-1};AdaptiveRegion response_region_{};
    void clear_motion(){response_.clear();last_motion_=0;remainder_x_=remainder_y_=0;}
public:
    AdaptiveFocusSolution solution{};std::string status{"Press X2 to lock a target"};uint64_t moves{},input_failures{};
    bool active()const{return session_.active();}
    uint32_t target()const{return session_.target();}
    bool spent()const{return session_.state()==X2LockSession::State::Spent;}
    void invalidate(const char* reason){session_.lose(reason);solution={};clear_motion();last_sample_=-1;status=session_.status;}
    void update(MakcuInput& input,const Snapshot& s,HWND game,const CameraFocusSettings& settings,bool menu,bool sniper,
        bool enabled,double now,const FocusActivation& activation,bool x2_down) {
        const bool was_active=active(),owned_x2=key_program_owned(VK_XBUTTON2);
        session_.buttons(x2_down,settings.x2_target_lock,owned_x2);
        if(!active()) {
            if(was_active){input.cancel_motion();solution={};clear_motion();last_sample_=-1;}
            status=session_.status;return;
        }
        const char* gate=owned_x2?"Program input / release X2 to lock":!enabled?"Focus disabled / release X2":menu?"Menu open / release X2":!input.ready()?"Input unavailable / release X2"
            :!game||IsIconic(game)||GetForegroundWindow()!=game?"Game not focused / release X2"
            :!s.pid||!s.time||GetTickCount64()<s.time||GetTickCount64()-s.time>50||!s.camera_valid||!s.projection.valid
                ||now<s.projection_ms||now-s.projection_ms>50?"Stale camera data / release X2":nullptr;
        if(gate){input.cancel_motion();invalidate(gate);return;}
        if(spent()){input.cancel_motion();status=session_.status;return;}
        RECT viewport{};if(!GetClientRect(game,&viewport)||viewport.right<=0||viewport.bottom<=0){input.cancel_motion();invalidate("Viewport unavailable / release X2");return;}
        const bool adaptive=adaptive_mode(settings);const float speed=adaptive?activation.adaptive_speed:activation.speed;
        if(response_adaptive_!=adaptive||response_sniper_!=sniper||response_speed_!=speed||response_bone_!=settings.focus_bone||response_region_!=settings.adaptive.region) {
            clear_motion();response_adaptive_=adaptive;response_sniper_=sniper;response_speed_=speed;response_bone_=settings.focus_bone;response_region_=settings.adaptive.region;
        }
        if(last_sample_==s.projection_ms&&session_.state()!=X2LockSession::State::Acquire)return;
        last_sample_=s.projection_ms;input.cancel_motion();
        AdaptivePoint base;const auto* target=session_.select(s,settings,base,solution.source_id);
        if(!target){solution={};clear_motion();status=session_.status;return;}
        auto next=build_x2_lock_solution(s,*target,settings,sniper,now,input.queue_delay_ms(),solution.source_id);
        if(!next.valid){invalidate("Lock point lost or blocked / release X2 to select again");return;}
        solution=next;
        if(next.inside){clear_motion();status="X2 locked / inside permitted region";return;}
        if(!last_motion_){last_motion_=now;status="X2 target locked / 360 degrees";return;}
        const float dt=float(std::clamp((now-last_motion_)/1000.,0.,.025));last_motion_=now;if(dt<=0)return;
        long x{},y{};
        if(adaptive) {
            const auto correction=response_.step(s.matrix,next.aim_point,float(viewport.right),float(viewport.bottom),dt,settings.adaptive,activation.adaptive_speed,true);
            if(!correction.valid){invalidate("Lock direction invalid / release X2");return;}
            x=correction.x;y=correction.y;solution.error=correction.error;solution.pixel_error=correction.remaining;solution.requested_pixels=correction.requested;
        }else {
            if(!spherical_screen_error(s.matrix,next.aim_point,float(viewport.right),float(viewport.bottom),solution.error,solution.pixel_error)){invalidate("Lock direction invalid / release X2");return;}
            x=ReplayCamera::motion(solution.pixel_error.x,dt,remainder_x_,activation.speed);
            y=ReplayCamera::motion(solution.pixel_error.y,dt,remainder_y_,activation.speed);
            solution.requested_pixels={float(x),float(y)};
        }
        solution.requested_x=x;solution.requested_y=y;status="X2 target locked / 360 degrees";
        if(!x&&!y)return;
        tracking_motion.error=std::hypot(solution.pixel_error.x,solution.pixel_error.y);
        if(input.move(game,x,y,FocusHoldButton::X2))++moves;
        else {++input_failures;invalidate("Input failed / release X2 to select again");}
    }
};
}
