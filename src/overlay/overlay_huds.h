#pragma once
#include "external_settings.h"
#include "game_reader.h"
#include "imgui.h"
#include "hud_style.h"
#include <cstdio>
namespace overlay {
inline void draw_rift_hud(ImDrawList* draw,const RiftState& state,float scale,ImVec2 origin,float opacity=1) {
    std::string value="--:--",detail=rift_lane_name(state.lane);ImU32 accent=hud_muted();
    if(state.phase==RiftPhase::Countdown){value=(state.estimated?"~":"")+midboss_countdown(state.remaining);accent=state.remaining<=30?hud_warning():solace::c_foreground;}
    else if(state.phase==RiftPhase::Incoming){value="Incoming";accent=hud_warning();}
    else if(state.phase==RiftPhase::Active){value=state.timer_valid?midboss_countdown(state.remaining):"Active";accent=hud_good();}
    if(state.phase==RiftPhase::Waiting)detail="Awaiting announcement";
    else if(state.phase==RiftPhase::Unavailable)detail="Data unavailable";
    else if(state.paused)detail+=" / paused";
    else if(state.phase==RiftPhase::Active&&state.timer_valid)detail+=" / fades";
    draw_hud_card(draw,origin*scale,scale,"Unstable rift",value.c_str(),detail.c_str(),HudGlyph::Rift,accent,opacity);
}
inline void draw_overlay_huds(ImDrawList* draw,const Snapshot& snap,const ExternalSettings& s,float width,float height,float frame_opacity,float scale=1,bool session_hud=false) {
    if(s.crosshair) {
        ImVec2 c{width*.5f,height*.5f};auto color=ImGui::ColorConvertFloat4ToU32(s.crosshair_color);
        for(int axis=0;axis<2;++axis)for(int side:{-1,1}){auto a=c,b=c;(axis?a.y:a.x)+=side*s.crosshair_gap;(axis?b.y:b.x)+=side*(s.crosshair_gap+s.crosshair_size);if(s.crosshair_outline)draw->AddLine(a,b,IM_COL32(0,0,0,220),s.crosshair_width+2);draw->AddLine(a,b,color,s.crosshair_width);}
        if(s.crosshair_dot){if(s.crosshair_outline)draw->AddCircleFilled(c,2.5f,IM_COL32(0,0,0,220));draw->AddCircleFilled(c,1.5f,color);}
    }
    if(s.spectators) {
        char value[24]{};snprintf(value,sizeof(value),"%zu",snap.spectators.size());std::string detail;
        if(snap.spectators.empty())detail=snap.observers_valid?"No spectators":"Observer data unavailable";
        else{detail=snap.spectators.front();if(snap.spectators.size()>1)detail+="  +"+std::to_string(snap.spectators.size()-1);}
        draw_hud_card(draw,{width-(hud_card_width+24)*scale,(24+(session_hud?hud_card_height+hud_card_gap:0))*scale},scale,"Spectators",value,detail.c_str(),HudGlyph::Eye,solace::c_foreground,frame_opacity);
    }
}
}
