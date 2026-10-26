#pragma once
#include "game_reader.h"
#include "makcu_input.h"
#include "focus_area.h"
#include "focus_hitboxes.h"
#include "focus_view.h"
#include "adaptive_target_zone.h"
#include <random>
#include <climits>

namespace overlay {
inline bool camera_mode_allowed(bool /*replay*/,bool /*practice*/,bool /*debug_enabled*/){return true;}inline bool camera_allowed(bool enabled,bool replay,bool game_focused,bool menu_open,bool held,uint64_t now,uint64_t sampled) {
    return enabled&&replay&&game_focused&&!menu_open&&held&&sampled&&now>=sampled&&now-sampled<=50;
}
inline bool hitscan_sample_lead(const MotionSample& motion,double now,double queue_ms,Vec3& lead) {
    const double age=now-motion.measured_ms;
    if(!motion.velocity_valid||!finite(motion.velocity)||!std::isfinite(age)||age<0||age>50)return false;
    const double delay=std::isfinite(queue_ms)?std::clamp(queue_ms,0.,20.):0.;
    lead=mul(motion.velocity,float((age+delay)/1000.));return finite(lead);
}
struct FocusOptions {
    bool players{true},minions{},orbs{},strict_enemy_players{};
    int mask()const{return (players?1:0)|(minions?2:0)|(orbs?4:0)|(strict_enemy_players?8:0);}
    bool allows(TargetKind kind)const{return kind==TargetKind::Player?players:kind==TargetKind::Minion?minions:kind==TargetKind::SoulOrb?orbs:false;}
};
inline bool focus_candidate(const FocusTarget& target,const Snapshot& snapshot,FocusOptions options,bool enemies_only) {
    if(target.maximum>0&&target.health<=0)return false;
    if(options.strict_enemy_players)return options.players&&target.kind==TargetKind::Player&&enemy_teams(snapshot.local_team,target.team);
    return options.allows(target.kind)&&(target.kind==TargetKind::SoulOrb||snapshot.replay||include_player(enemies_only,snapshot.local_team,target.team));
}
inline bool focus_point(const Matrix& matrix,const FocusTarget& target,int bone,bool free_move,float width,float height,ScreenPoint center,Vec3 lead,Vec3& world,float& line_position,FreeMovementMode mode=FreeMovementMode::BoneLine,int* hitbox_index=nullptr,bool* inside_hitbox=nullptr) {
    if(hitbox_index)*hitbox_index=-1;if(inside_hitbox)*inside_hitbox=false;
    if(bone<0||bone>2)return false;
    if(free_move&&target.kind!=TargetKind::SoulOrb) {
        if(mode==FreeMovementMode::Hitboxes) {
            ClosestHitboxPoint nearest;
            if(!closest_focus_hitboxes(matrix,target.hitboxes,width,height,center,lead,nearest))return false;
            world=nearest.world;line_position=float(bone);
            if(hitbox_index)*hitbox_index=nearest.index;if(inside_hitbox)*inside_hitbox=nearest.inside;
            return true;
        }
        if(!target.dot_valid[0]||!target.dot_valid[1]||!target.dot_valid[2])return false;
        auto points=target.dots;for(auto& p:points)p=add(p,lead);
        ClosestLinePoint nearest;
        if(!closest_focus_line(matrix,points,width,height,center,nearest))return false;
        world=nearest.world;line_position=nearest.position;return true;
    }
    if(!target.dot_valid[bone])return false;
    world=add(target.dots[bone],lead);line_position=float(bone);return finite(world);
}
inline bool focus_target_geometry(const Snapshot& snapshot,const FocusTarget& target,int bone,bool free_move,const AdaptiveSettings& area,float width,float height,ScreenPoint center,Vec3& world,float& line,ScreenPoint& screen,FreeMovementMode mode=FreeMovementMode::BoneLine,const AdaptiveTargetZones* zones=nullptr,bool retained=false,float* angle=nullptr) {
    bool inside=false;
    if(bone<0||bone>=3
        ||!focus_point(snapshot.matrix,target,bone,free_move,width,height,center,{},world,line,mode,nullptr,&inside)
        ||!focus_project(snapshot.matrix,world,width,height,screen))return false;
    if(inside)screen=center; // All intersected volumes have exactly zero selection distance.
    CameraBasis basis;if(!camera_basis(snapshot.matrix,basis))return false;
    const float distance=inside?0.f:point_error(basis,world).distance;
    if(!std::isfinite(distance))return false;if(angle)*angle=distance;
    if(area.area==AdaptiveAreaMode::TargetZone) {
        const auto* zone=zones?zones->find(target.handle):nullptr;
        return zone&&zone->valid&&(retained?zone->retention_hit:zone->activation_hit);
    }
    return adaptive_area_accepts(snapshot.matrix,world,area,distance,retained);
}
inline std::mt19937& focus_random() {static thread_local std::mt19937 rng(std::random_device{}());return rng;}
inline const FocusTarget* select_focus_target(const Snapshot& snapshot,FocusOptions options,bool enemies_only,int bone,bool free_move,bool visible_only,float width,float height,ScreenPoint center,const AdaptiveSettings& area={},std::mt19937* random=nullptr,FreeMovementMode mode=FreeMovementMode::BoneLine,const AdaptiveTargetZones* zones=nullptr) {
    const FocusTarget* selected=nullptr;double best=std::numeric_limits<double>::max();int lowest=INT_MAX;unsigned ties{};
    auto consider=[&](const FocusTarget& target) {
        if(!focus_candidate(target,snapshot,options,enemies_only))return;
        Vec3 world{};float line{},angle{};ScreenPoint screen{};
        if(!focus_target_geometry(snapshot,target,bone,free_move,area,width,height,center,world,line,screen,mode,zones,false,&angle)
            ||!visibility_allowed(visible_only,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,world))return;
        // Angular ranking stays consistent across zoom and custom aspect ratios.
        double distance=std::round(double(angle)*1000.);
        int health=target.maximum>0&&target.health>=0?target.health:INT_MAX;
        if(distance<best||(distance==best&&health<lowest)) {best=distance;lowest=health;selected=&target;ties=1;}
        else if(distance==best&&health==lowest) {
            // Reservoir sampling is uniform, allocation-free and used only on acquisition.
            if(std::uniform_int_distribution<unsigned>(1,++ties)(random?*random:focus_random())==1)selected=&target;
        }
    };
    for(const auto& target:snapshot.players)consider(target);
    for(const auto& target:snapshot.focus_targets)consider(target);
    return selected;
}
struct ReplayCamera {
    uint32_t target{};bool engaged{},lost{};int bone{-1};double last{};
    float remainder_x{},remainder_y{};
    float line_position{1.f},flight_time{};bool free_mode{},prediction_active{},point_valid{},occluded{};Vec3 aim_point{};
    FreeMovementMode free_movement_mode{FreeMovementMode::BoneLine};int hitbox_index{-1};bool inside_hitbox{};
    FocusHoldButton hold_button{FocusHoldButton::SideButtons};
    TargetKind kind{TargetKind::None};int target_mask{};
    AdaptiveSettings focus_area{};uint64_t attempted_sample{};bool input_blocked{};
    double acquire_after{};const char* selection_status{"Hold and enter a valid focus area"};
    uint64_t moves{},input_failures{};
    void release(){target=0;kind=TargetKind::None;engaged=lost=input_blocked=false;bone=-1;last=acquire_after=0;attempted_sample=0;remainder_x=remainder_y=0;point_valid=prediction_active=occluded=inside_hitbox=false;hitbox_index=-1;flight_time=0;}
    const FocusTarget* choose(const Snapshot& snapshot,FocusOptions targets,bool enemies_only,int selected_bone,bool free_move,bool visible_only,AdaptiveSettings area,float width,float height,ScreenPoint center,double now,std::mt19937* random=nullptr,FreeMovementMode mode=FreeMovementMode::BoneLine,const AdaptiveTargetZones* zones=nullptr) {
        sanitize_adaptive(area);selected_bone=std::clamp(selected_bone,0,2);
        const bool area_changed=focus_area.area!=area.area||focus_area.radius_degrees!=area.radius_degrees||focus_area.padding_percent!=area.padding_percent||focus_area.region!=area.region;
        if(!engaged||bone!=selected_bone||free_mode!=free_move||free_movement_mode!=mode||target_mask!=targets.mask()||area_changed) {
            release();engaged=true;bone=selected_bone;free_mode=free_move;free_movement_mode=mode;line_position=float(bone);target_mask=targets.mask();focus_area=area;last=now;
        }
        if(input_blocked)return nullptr;
        const FocusTarget* selected=nullptr;
        if(target) {
            for(const auto& player:snapshot.players)if(player.handle==target){selected=&player;break;}
            if(!selected)for(const auto& object:snapshot.focus_targets)if(object.handle==target){selected=&object;break;}
            Vec3 world{};float line{};ScreenPoint screen{};
            if(!selected||!focus_candidate(*selected,snapshot,targets,enemies_only)||!focus_target_geometry(snapshot,*selected,bone,free_move,area,width,height,center,world,line,screen,mode,zones,true)) {
                selected=nullptr;target=0;kind=TargetKind::None;point_valid=prediction_active=occluded=inside_hitbox=false;hitbox_index=-1;flight_time=0;remainder_x=remainder_y=0;last=now;
                acquire_after=now+100;selection_status="Target lost / waiting to select next target";
            }
        }
        if(!target&&now>=acquire_after&&attempted_sample!=snapshot.time) {
            attempted_sample=snapshot.time;
            selected=select_focus_target(snapshot,targets,enemies_only,bone,free_move,visible_only,width,height,center,area,random,mode,zones);
            if(selected){target=selected->handle;kind=selected->kind;last=now;remainder_x=remainder_y=0;}
            else selection_status=area.area==AdaptiveAreaMode::TargetZone?"Hold and enter an eligible Target Zone":"Hold and enter the crosshair angle";
        }
        lost=!selected;
        return selected;
    }
    // Persistent fractional counts avoid losing small corrections to integer truncation.
    static LONG motion(float error,float dt,float& remainder,float speed=4.f) {
        if(!std::isfinite(error)||!std::isfinite(dt)||!std::isfinite(speed)||dt<=0){remainder=0;return 0;}
        speed=std::clamp(speed,1.f,240.f);
        if(std::abs(error)<2){remainder=0;return 0;}
        // Keep the old response at ordinary speeds, but bound high-speed corrections.
        float desired=error*std::min(speed*std::min(dt,.025f),.85f)+remainder;
        float limit=600.f*speed*std::min(dt,.025f);
        desired=std::clamp(desired,-limit,limit);
        auto count=static_cast<LONG>(desired);remainder=desired-float(count);return count;
    }
    void update(MakcuInput& input,const Snapshot& snapshot,HWND game,bool enabled,bool debug_enabled,bool enemies_only,bool menu,int selected_bone,float speed,bool free_move,bool prediction,float projectile_speed,bool inherit_velocity,bool auto_speed,bool visible_only,FocusOptions targets,const AdaptiveSettings& area_config,double now,FreeMovementMode mode=FreeMovementMode::BoneLine,FocusHoldButton selected_hold_button=FocusHoldButton::SideButtons,const AdaptiveTargetZones* zones=nullptr,bool hitscan=false,float fixed_lead=-1.f) {
        input.cancel_motion(); // No previous correction survives a changed gate/occlusion state.
        if(hold_button!=selected_hold_button){release();hold_button=selected_hold_button;}
        if(!input.ready()){release();return;}
        bool held=focus_button_held(hold_button);
        bool foreground=game&&GetForegroundWindow()==game&&!IsIconic(game);
        if(!camera_allowed(enabled,camera_mode_allowed(snapshot.replay,snapshot.practice,debug_enabled),foreground,menu,held,GetTickCount64(),snapshot.time)){release();return;}
        RECT area{};if(!GetClientRect(game,&area)||area.right<=0||area.bottom<=0){release();return;}
        selected_bone=std::clamp(selected_bone,0,2);ScreenPoint center=focus_reference(float(area.right),float(area.bottom));ScreenPoint point{};
        const auto* selected=choose(snapshot,targets,enemies_only,selected_bone,free_move,visible_only,area_config,float(area.right),float(area.bottom),center,now,nullptr,mode,zones);
        if(!selected)return;
        point_valid=false;
        if(!focus_point(snapshot.matrix,*selected,bone,free_move,float(area.right),float(area.bottom),center,{},aim_point,line_position,mode,&hitbox_index,&inside_hitbox)){last=now;remainder_x=remainder_y=0;return;}
        prediction_active=false;flight_time=0;Vec3 source{},lead{};
        occluded=!visibility_allowed(visible_only,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,aim_point);
        if(occluded){point_valid=false;last=now;remainder_x=remainder_y=0;return;}
        float inheritance=auto_speed?snapshot.weapon.inheritance:(inherit_velocity?1.f:0.f);
        if(auto_speed)projectile_speed=snapshot.weapon.speed;
        Vec3 sample_lead{};
        if(hitscan&&hitscan_sample_lead(selected->motion,now,input.queue_delay_ms(),sample_lead)) {
            Vec3 compensated{};float position=line_position;int compensated_hitbox=-1;bool compensated_inside=false;
            if(focus_point(snapshot.matrix,*selected,bone,free_move,float(area.right),float(area.bottom),center,sample_lead,compensated,position,mode,&compensated_hitbox,&compensated_inside)) {
                aim_point=compensated;line_position=position;prediction_active=true;lead=sample_lead;hitbox_index=compensated_hitbox;inside_hitbox=compensated_inside;
            }
        }else if(!hitscan&&fixed_lead>=0&&selected->velocity_valid) {
            Vec3 manual_lead=mul(selected->velocity,fixed_lead);float position{};int index{};bool inside{};
            if(focus_point(snapshot.matrix,*selected,bone,free_move,float(area.right),float(area.bottom),center,manual_lead,aim_point,position,mode,&index,&inside)) {
                prediction_active=true;flight_time=fixed_lead;lead=manual_lead;line_position=position;hitbox_index=index;inside_hitbox=inside;
            }
        }else if(!hitscan&&prediction&&(!auto_speed||snapshot.weapon.valid)&&selected->velocity_valid&&(inheritance==0||snapshot.local_velocity_valid)&&camera_origin(snapshot.matrix,source)){
            Vec3 relative=sub(selected->velocity,mul(snapshot.local_velocity,inheritance));
            if(intercept(source,aim_point,relative,projectile_speed,flight_time)){
                Vec3 predicted{};float position=line_position;
                Vec3 predicted_lead=mul(relative,flight_time);int predicted_hitbox=-1;bool predicted_inside=false;
                if(focus_point(snapshot.matrix,*selected,bone,free_move,float(area.right),float(area.bottom),center,predicted_lead,predicted,position,mode,&predicted_hitbox,&predicted_inside)) {
                    aim_point=predicted;line_position=position;prediction_active=true;lead=predicted_lead;hitbox_index=predicted_hitbox;inside_hitbox=predicted_inside;
                }
            }
        }
        // Check the corresponding current point as well as the predicted point,
        // including when prediction chooses a different limb or hitbox.
        auto actual_point=sub(aim_point,lead);
        occluded=!visibility_allowed(visible_only,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,actual_point);
        if(occluded){point_valid=false;last=now;remainder_x=remainder_y=0;return;}
        occluded=!visibility_allowed(visible_only,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,aim_point);
        if(occluded){point_valid=false;last=now;remainder_x=remainder_y=0;return;}
        point_valid=focus_project(snapshot.matrix,aim_point,float(area.right),float(area.bottom),point);
        if(!point_valid){last=now;remainder_x=remainder_y=0;return;}
        if(free_move&&mode==FreeMovementMode::Hitboxes&&inside_hitbox){last=now;remainder_x=remainder_y=0;return;}
        float dt=float((now-last)/1000.);last=now;
        auto dx=motion(point.x-center.x,dt,remainder_x,speed),dy=motion(point.y-center.y,dt,remainder_y,speed);
        if(!(dx||dy))return;
        // Recheck foreground immediately before sending relative mouse input.
        if(GetForegroundWindow()!=game||!focus_button_held(hold_button))return;
        tracking_motion.error=std::hypot(point.x-center.x,point.y-center.y);
        if(input.move(game,dx,dy,hold_button))++moves;
        else {++input_failures;lost=input_blocked=true;point_valid=false;remainder_x=remainder_y=0;}
    }
};
}
