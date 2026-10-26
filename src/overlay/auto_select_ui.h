#pragma once
#include "vindicta_auto_select.h"
#include "external_ui.h"
#include "external_diagnostics.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"

namespace overlay {
inline bool draw_auto_select_settings(VindictaAutoSelectSettings& settings,const VindictaAutoSelect& controller,
    const Snapshot& snapshot,const MakcuInput& input,const ImRect& body,float alpha,bool transparent) {
    using namespace solace;auto* draw=ImGui::GetWindowDrawList();bool changed=false;ImGui::PushID("vindicta_auto_select_page");
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=14.f) {
        draw_text(draw,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);
    };
    auto card=[&](const ImRect& box,const char* label,icons::id icon) {
        rounded_panel::draw(draw,box.Min,box.Max,mo::with_alpha(c_card,(transparent?1.f:.72f)*alpha),px(16));
        draw->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),0,px(1));
        icons::draw(icon,draw,box.Min+px(20,20),px(16),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(draw,font_semibold(12),box.Min+px(46,21),mo::with_alpha(c_muted_foreground,alpha),label,px(.8f));
    };
    text(body.Min+px(0,48),"Select, scope and fire / no hold button",true);
    const bool hero=vindicta(snapshot.hero);
    badge("hero_ready",draw,{body.Max.x-px(150),body.Min.y+px(46)},hero?"Vindicta selected":"Vindicta required",hero?badge_good:badge_warn,false,alpha);
    const float gap=px(16),left=(body.GetWidth()-gap)*.51f;
    ImRect controls(body.Min+px(0,80),body.Min+ImVec2(left,px(340)));
    ImRect calculation({controls.Max.x+gap,controls.Min.y},{body.Max.x,controls.Max.y});
    ImRect live(body.Min+px(0,356),{body.Max.x,body.Min.y+px(484)});
    card(controls,"FOCUS AREA",icons::id::target);card(calculation,"LETHAL SHOT",icons::id::lock);card(live,"LIVE STATUS",icons::id::workflow);
    char label[256]{};
    auto slider=[&](const char* id,const char* name,float y,float& value,float low,float high,const char* format) {
        text(controls.Min+px(20,y),name,true);snprintf(label,sizeof(label),format,value);
        text({controls.Max.x-px(20)-text_width(font_semibold(14),label),controls.Min.y+px(y)},label);
        return range_slider(id,controls.Min+px(20,y+25),controls.GetWidth()-px(40),&value,low,high);
    };
    changed|=slider("angle","Crosshair angle",56,settings.angle,1,25,"%.1f degrees");
    changed|=slider("speed","Aim speed",123,settings.speed,1,140,"%.0f");
    text(controls.Min+px(20,199),"Show area",true);
    changed|=switch_toggle("show_area",{controls.Max.x-px(20+switch_w),controls.Min.y+px(193)},&settings.show_area);
    text(controls.Min+px(20,235),"Independent of Camera and Auto Click",true,12);
    const char* rules[]={"Enemy heroes below 49% health","Visible head and torso only","Acquire by 60% charge","Recalculate until the shot is lethal"};
    for(int i=0;i<4;++i){auto at=calculation.Min+px(20,57+34.f*i);icons::draw(icons::id::check,draw,at,px(15),mo::with_alpha(IM_COL32(96,175,248,255),alpha));text(at+px(25,0),rules[i],true,12);}
    text(calculation.Min+px(20,198),"60% ring = 80% shot power",true,12);
    ImGui::SetCursorScreenPos(calculation.Min+px(20,222));
    if(extra_action("Calculation details",px(176),false,px(28)))ImGui::OpenPopup("Damage calculation");
    ImGui::SetNextWindowSizeConstraints(px(360,0),px(520,480));
    if(ImGui::BeginPopup("Damage calculation")) {
        ImGui::TextWrapped("60%% is the acquisition limit. An existing engagement may charge longer if the target heals or conditions change.");
        const auto& a=snapshot.assassinate;
        ImGui::Separator();ImGui::Text("Spirit: %.1f / Base: %.1f / Low-health bonus: %.1f",a.spirit,a.damage,a.low_bonus);
        ImGui::Text("Head bonus: %.0f%% / Full charge: %.2f s",a.head_bonus,a.full_charge);
        ImGui::TextWrapped("Source: %s",a.source.c_str());
        ImGui::TextWrapped("Estimate omissions: %s",controller.solution.omissions.empty()?(a.omissions.empty()?"Unmodeled item procs are excluded":a.omissions.c_str()):controller.solution.omissions.c_str());
        ImGui::TextWrapped("Automatic scopes close after a confirmed shot or target loss. Manually opened scopes stay open.");
        ImGui::EndPopup();
    }
    draw->PushClipRect(live.Min+px(20,43),live.Max-px(20,8),true);
    text(live.Min+px(20,48),controller.status.c_str());
    const auto& solution=controller.solution;
    if(solution.valid)snprintf(label,sizeof(label),"%s / %d HP / %s / charge %.0f%% of %.0f%% needed",solution.hero.c_str(),solution.health,solution.head?"Head":"Torso",solution.charge*100,solution.required*100);
    else snprintf(label,sizeof(label),"%s / %d enemies below 49%% / %s",snapshot.hero.name.c_str(),controller.candidates,snapshot.assassinate.status.c_str());
    text(live.Min+px(20,76),label,true,12);
    snprintf(label,sizeof(label),"%s / %s / %.0f estimated damage / %llu confirmed shots",input.label(),solution.estimated||snapshot.assassinate.estimated?"Estimate":"Live data",solution.damage,static_cast<unsigned long long>(controller.shots));
    text(live.Min+px(20,101),label,true,12);draw->PopClipRect();ImGui::PopID();return changed;
}
inline void write_auto_select_data(std::ostream& out,const VindictaAutoSelect& c,const Snapshot& s) {
    const auto& a=s.assassinate;const auto& p=c.solution;
    out<<"{\"phase\":";external_json_string(out,auto_select_phase_name(c.phase));out<<",\"status\":";external_json_string(out,c.status);
    out<<",\"active\":"<<(c.active()?"true":"false")<<",\"owns_scope\":"<<(c.owns_scope()?"true":"false")<<",\"target\":"<<p.target
       <<",\"candidate_count\":"<<c.candidates<<",\"hero\":";external_json_string(out,p.hero);
    out<<",\"required_charge\":"<<p.required<<",\"current_charge\":"<<p.charge<<",\"estimated_damage\":"<<p.damage<<",\"full_damage\":"<<p.full_damage
       <<",\"health\":"<<p.health<<",\"head\":"<<(p.head?"true":"false")<<",\"estimated\":"<<(p.estimated||a.estimated?"true":"false")
       <<",\"confirmed_shots\":"<<c.shots<<",\"confirmed_scope_exits\":"<<c.scope_exits<<",\"scoped\":"<<(s.hero.sniper_scoped?"true":"false")
       <<",\"movement_commands\":"<<c.moves<<",\"data_valid\":"<<(a.valid?"true":"false")
       <<",\"damage\":"<<a.damage<<",\"low_bonus\":"<<a.low_bonus<<",\"spirit\":"<<a.spirit<<",\"data_status\":";external_json_string(out,a.status);
    out<<",\"source\":";external_json_string(out,a.source);out<<",\"omissions\":";external_json_string(out,p.omissions.empty()?a.omissions:p.omissions);out<<"}\n";
}
}
