#pragma once
#include "external_ui.h"
#include "camera_profile.h"
namespace overlay {
inline bool draw_tracking_controls(CameraFocusSettings& settings,bool ultimate) {
    bool changed=false;auto& t=settings.tracking;
    if(external_preview_expanded)ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    if(!ImGui::CollapsingHeader("Extended camera controls"))return false;
    ImGui::BeginChild("Extended controls",{solace::px(390),solace::px(350)},false);
    changed|=extra_toggle("Extended tracking",t.enabled);
    changed|=extra_toggle("Toggle activation",t.toggle);changed|=extra_binding("Primary key (None = Hold button)",t.activation_key);changed|=extra_binding("Second key",t.second_key);changed|=extra_binding("Cycle target",t.cycle_key);
    changed|=extra_toggle("Lock one subject",t.lock_subject);changed|=extra_toggle("Replace lost subject",t.auto_switch);
    int joint=t.joint+1;const char* choices[]={"Use existing bone","Head","Neck","Chest","Pelvis","Left upper arm","Right upper arm","Left forearm","Right forearm","Left hand","Right hand","Left thigh","Right thigh","Left knee","Right knee","Left foot","Right foot"};
    if(extra_combo("Joint",&joint,choices,17)){t.joint=joint-1;changed=true;}
    changed|=extra_combo("Bone selection",&t.strategy,"Fixed\0Closest available\0Random available\0");
    if(t.strategy) {
        auto group=[&](const char* label,int bit){bool enabled=(t.groups&bit)!=0;if(extra_toggle(label,enabled)){if(enabled)t.groups|=bit;else if(t.groups!=bit)t.groups&=~bit;changed=true;}};
        group("Head / neck",1);group("Chest / pelvis",2);group("Arms / hands",4);group("Legs / feet",8);
        if(t.strategy==2)changed|=extra_slider("Switch interval (s)",&t.interval,.1f,5);
    }
    ImGui::TextWrapped("Fixed mode aims at the selected joint. Adaptive uses the selected joint as a preference and keeps its free region. V1 keeps the head-to-pelvis line.");
    changed|=extra_toggle("Ignore horizontal movement",t.ignore_x);changed|=extra_toggle("Ignore vertical movement",t.ignore_y);
    changed|=extra_slider("Additional smoothness",&t.smoothness,0,30);changed|=extra_slider("Extra X smoothing",&t.extra_x,0,30);changed|=extra_slider("Extra Y smoothing",&t.extra_y,0,30);
    changed|=extra_slider("Deadzone (px)",&t.deadzone,0,20);changed|=extra_slider("Movement limit (counts/s)",&t.max_counts,100,2400);
    changed|=extra_slider("Ease-in (s)",&t.ease,0,1);changed|=extra_slider("Horizontal framing (px)",&t.offset_x,-200,200);changed|=extra_slider("Vertical framing (px)",&t.offset_y,-200,200);
    changed|=extra_slider("Tracking range (0 = unlimited)",&t.range,0,20000);
    if(!ultimate){changed|=extra_toggle("Fixed-time velocity lead",t.fixed_lead);if(t.fixed_lead)changed|=extra_slider("Lead (s)",&t.lead,0,.5f);}
    changed|=extra_toggle("RGB guide",t.rgb);changed|=extra_toggle("Line to subject",t.target_line);changed|=extra_color4("Guide color",&t.guide_color.x);
    ImGui::EndChild();return changed;
}
}
