#pragma once
#include "game_reader.h"
#include "focus_hitboxes.h"
#include "makcu_input.h"

namespace overlay {
enum class CrosshairState { NoView, NoMesh, NoTeam, NoHit, OtherEntity, Friendly, Blocked, Enemy, NoOrb, SoulOrb, OrbUnavailable };
inline const char* crosshair_state_name(CrosshairState state) {
    switch(state) {
    case CrosshairState::NoView:return "Waiting for the render view";
    case CrosshairState::NoMesh:return "Waiting for map collision mesh";
    case CrosshairState::NoTeam:return "Waiting for local team";
    case CrosshairState::NoHit:return "Waiting for an enemy hitbox";
    case CrosshairState::OtherEntity:return "Another entity is in front";
    case CrosshairState::Friendly:return "Friendly or unknown team in front";
    case CrosshairState::Blocked:return "Crosshair hit point blocked";
    case CrosshairState::Enemy:return "Visible enemy hitbox";
    case CrosshairState::NoOrb:return "Waiting for a claimable soul orb";
    case CrosshairState::SoulOrb:return "Visible claimable soul orb";
    case CrosshairState::OrbUnavailable:return "Soul orb not attackable";
    default:return "Unavailable";
    }
}
struct CrosshairHit {
    CrosshairState state{CrosshairState::NoView};
    uint32_t target{};int hitbox{-1};double distance{};
    Vec3 origin{},point{};
    TargetKind kind{TargetKind::None};OrbPhase orb_phase{OrbPhase::Unknown};uint64_t valid_until{};
};
// Unlike closest_focus_hitboxes, this has no nearest-edge fallback: a miss stays
// a miss. Include known friendly/player/minion volumes before testing eligibility.
inline CrosshairHit crosshair_hit(const Snapshot& snapshot,float width,float height,ScreenPoint pixel,bool orbs_only=false) {
    CrosshairHit result;Vec3 direction{};
    if(!snapshot.projection.valid||!snapshot.camera_valid
        ||!hitbox_focus::ray(snapshot.matrix,width,height,pixel,result.origin,direction))return result;
    if(!snapshot.visibility){result.state=CrosshairState::NoMesh;return result;}
    if(snapshot.local_team!=2&&snapshot.local_team!=3){result.state=CrosshairState::NoTeam;return result;}
    const FocusTarget* nearest=nullptr;double distance=std::numeric_limits<double>::max();
    const auto now=GetTickCount64();
    auto eligible=[&](const FocusTarget& entity) {
        if(entity.team!=2&&entity.team!=3)return false;
        return orbs_only?entity.kind==TargetKind::SoulOrb&&entity.orb.phase(now)==OrbPhase::Attackable
            :entity.kind==TargetKind::Player&&entity.team!=snapshot.local_team;
    };
    auto consider_box=[&](const FocusTarget& entity,const Hitbox& box,int index) {
            double time{};
            if(!hitbox_focus::intersect(box,{},result.origin,direction,time))return;
            // At an overlapping boundary, a blocker takes precedence over a shot.
            if(time<distance||(time==distance&&nearest&&eligible(*nearest)&&!eligible(entity))) {
                nearest=&entity;distance=time;result.hitbox=index;
            }
    };
    auto consider=[&](const FocusTarget& entity) {
        if(entity.kind==TargetKind::SoulOrb) {
            if(entity.shot_bounds)consider_box(entity,*entity.shot_bounds,-1);
            return;
        }
        if(entity.health<=0||entity.maximum<=0)return;
        for(size_t i=0;i<entity.hitboxes.size();++i)consider_box(entity,entity.hitboxes[i],int(i));
    };
    for(const auto& player:snapshot.players)consider(player);
    for(const auto& entity:snapshot.focus_targets)consider(entity);
    if(!nearest){result.state=orbs_only?CrosshairState::NoOrb:CrosshairState::NoHit;return result;}
    result.target=nearest->handle;result.kind=nearest->kind;result.distance=distance;result.valid_until=snapshot.time+25;
    result.point=add(result.origin,mul(direction,float(distance)));
    if(orbs_only) {
        if(nearest->kind!=TargetKind::SoulOrb){result.state=CrosshairState::OtherEntity;return result;}
        result.orb_phase=nearest->orb.phase(now);
        if(!eligible(*nearest)){result.state=CrosshairState::OrbUnavailable;return result;}
        result.valid_until=std::min(result.valid_until,nearest->orb.deadline());
        result.state=snapshot.visibility->clear(result.origin,result.point)?CrosshairState::SoulOrb:CrosshairState::Blocked;
        return result;
    }
    if(nearest->kind!=TargetKind::Player){result.state=CrosshairState::OtherEntity;return result;}
    if(!eligible(*nearest)){result.state=CrosshairState::Friendly;return result;}
    // A visible head/other limb does not establish visibility at the ray hit.
    result.state=snapshot.visibility->clear(result.origin,result.point)?CrosshairState::Enemy:CrosshairState::Blocked;
    return result;
}
struct RayClick {
    CrosshairHit hit{};const char* status{"Off"};
    void release(MakcuInput& input,const char* reason="Off") {
        input.cancel_click();hit={};status=reason;
    }
    void update(MakcuInput& input,const Snapshot& snapshot,HWND game,bool enabled,bool menu,float interval_ms,bool camera_moved,bool orbs_only=false,bool orbs_hold_side=false) {
        if(!enabled){release(input);return;}
        if(!input.ready()){release(input,"Waiting for input");return;}
        if(menu||!game||!IsWindow(game)||IsIconic(game)||GetForegroundWindow()!=game){release(input,"Paused / game not focused");return;}
        const uint64_t now=GetTickCount64();
        if(!snapshot.pid||!snapshot.time||now<snapshot.time||now-snapshot.time>25){release(input,"Waiting for fresh game data");return;}
        if(snapshot.replay||!snapshot.local_alive){release(input,"Waiting for a living local player");return;}
        const bool hold_side=!orbs_only||orbs_hold_side;
        if(hold_side&&!focus_button_held(FocusHoldButton::SideButtons)){release(input,"Hold either side button");return;}
        if(focus_button_held(FocusHoldButton::LeftMouse)){release(input,"Left mouse already held");return;}
        RECT bounds{};
        if(!GetClientRect(game,&bounds)||bounds.right<=0||bounds.bottom<=0){release(input,"Waiting for the game window");return;}
        hit=crosshair_hit(snapshot,float(bounds.right),float(bounds.bottom),{bounds.right*.5f,bounds.bottom*.5f},orbs_only);
        status=hit.state==CrosshairState::OrbUnavailable?orb_phase_name(hit.orb_phase):crosshair_state_name(hit.state);
        if(hit.state!=CrosshairState::Enemy&&hit.state!=CrosshairState::SoulOrb){input.cancel_click();return;}
        // Do not send a shot based on a ray while also steering away from it.
        if(camera_moved){input.cancel_click();status="Waiting for camera to settle";return;}
        auto interval=static_cast<unsigned>(std::lround(std::clamp(std::isfinite(interval_ms)?interval_ms:150.f,80.f,1000.f)));
        if(now<input.last_click()+interval){input.cancel_click();status="Click interval";return;}
        status=input.click(game,hit.valid_until,interval,hold_side)?(input.hardware_ready()?"Click queued":"Click sent / Windows input"):"Waiting for click input";
    }
};
}
