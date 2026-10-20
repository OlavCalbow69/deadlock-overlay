#pragma once
#include "auto_parry.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"

namespace overlay {
inline bool draw_auto_parry_settings(AutoParrySettings& settings,const AutoParry& controller,
    const ParryReadState& state,const ImRect& body,float alpha,bool transparent) {
    using namespace solace;
    auto* draw=ImGui::GetWindowDrawList();bool changed=false;
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=13.f) {
        draw_text(draw,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);
    };
    auto card=[&](const ImRect& box,const char* title,icons::id icon) {
        rounded_panel::draw(draw,box.Min,box.Max,mo::with_alpha(c_card,(transparent?1.f:.72f)*alpha),px(16));
        draw->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),0,px(1));
        icons::draw(icon,draw,box.Min+px(18,18),px(15),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(draw,font_semibold(11),box.Min+px(42,19),mo::with_alpha(c_muted_foreground,alpha),title,px(.8f));
    };
    auto toggle=[&](const ImRect& box,float y,const char* id,const char* label,bool& value,const char* hint=nullptr) {
        text(box.Min+px(18,y),label);
        changed|=switch_toggle(id,{box.Max.x-px(66),box.Min.y+px(y-2)},&value);
        if(hint&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",hint);
    };
    const float gap=px(14),half=(body.GetWidth()-gap)*.5f;
    ImRect input(body.Min+px(0,62),body.Min+ImVec2(half,px(312)));
    ImRect attacks({input.Max.x+gap,input.Min.y},{body.Max.x,input.Max.y});
    ImRect timing(body.Min+px(0,326),{body.Max.x,body.Min.y+px(408)});
    ImRect diagnostics(body.Min+px(0,422),{body.Max.x,body.Min.y+px(532)});
    card(input,"INPUT & DISPLAY",icons::id::lock);card(attacks,"INCOMING ATTACKS",icons::id::target);
    card(timing,"TIMING",icons::id::clock);card(diagnostics,"LIVE STATUS",icons::id::workflow);
    ImGui::PushID("auto_parry");
    toggle(input,53,"enabled","Auto parry",settings.enabled);
    toggle(input,85,"observe","Observation only",settings.observe_only,"Detect and draw incoming threats without sending the parry key.");
    toggle(input,117,"hud","Status HUD",settings.show_hud);
    toggle(input,149,"threat","Threat marker",settings.show_threat);
    text(input.Min+px(18,187),"Parry key",true,12);
    char binding[17]{};strncpy_s(binding,settings.key.c_str(),_TRUNCATE);
    ImGui::SetCursorScreenPos({input.Max.x-px(108),input.Min.y+px(179)});ImGui::SetNextItemWidth(px(90));
    if(ImGui::InputText("##key",binding,sizeof(binding),ImGuiInputTextFlags_CharsUppercase|ImGuiInputTextFlags_CharsNoBlank)) {
        settings.key=binding;changed=true;
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Match your in-game keyboard parry binding, e.g. F, G or a number key.");
    text(input.Min+px(18,220),"Automatic while enabled and game focused",true,11);
    toggle(attacks,53,"melee","Melee attacks",settings.melee);
    ImGui::BeginDisabled(!settings.melee);
    toggle(attacks,85,"light","Light melee",settings.light,"Requires the attack ability to be exposed. The replicated fallback detects heavy melee only.");
    toggle(attacks,117,"heavy","Heavy melee",settings.heavy);
    ImGui::EndDisabled();
    toggle(attacks,159,"spells","Counterspell projectiles",settings.counterspell,
        "Requires Counterspell owned and ready. Currently checks Charged Shot and Bebop Hook only; other spells are skipped.");
    text(attacks.Min+px(18,198),"Current bodies + incoming trajectory",true,11);
    text(attacks.Min+px(18,220),"Walls, cooldown and player state checked",true,11);
    text(timing.Min+px(18,45),"Parry before predicted contact",true,12);
    char label[96]{};snprintf(label,sizeof(label),"%.0f ms",settings.lead_ms);
    draw_text(draw,font_semibold(13),{timing.Max.x-px(18)-text_width(font_semibold(13),label),timing.Min.y+px(44)},
        mo::with_alpha(c_foreground,alpha),label);
    changed|=range_slider("lead",timing.Min+px(18,68),timing.GetWidth()-px(36),&settings.lead_ms,30,300);
    draw->PushClipRect(diagnostics.Min,diagnostics.Max,true);
    text(diagnostics.Min+px(18,43),controller.status.c_str(),false,13);
    if(state.valid)snprintf(label,sizeof(label),"Parry %s / %.2fs CD / base %s / %u heavy-only readers",state.active?"active":state.ready?"ready":"waiting",state.cooldown_remaining,state.melee_definition_valid?"ready":"missing",state.replicated_readers);
    else snprintf(label,sizeof(label),"%s",settings.enabled?state.status.c_str():"Enable to read incoming attacks");
    text(diagnostics.Min+px(18,67),label,true,11);
    snprintf(label,sizeof(label),"Counterspell %s / %.2fs / inputs sent %llu",!state.counterspell_owned?"not owned":state.counterspell_active?"active":state.counterspell_ready?"ready":"cooldown",state.spell_cooldown_remaining,static_cast<unsigned long long>(controller.confirmed_inputs));
    text(diagnostics.Min+px(18,87),label,true,11);
    draw->PopClipRect();
    ImGui::PopID();
    if(changed)sanitize_parry_settings(settings);
    return changed;
}

inline void draw_auto_parry_hud(ImDrawList* draw,const AutoParry& controller,const ParryReadState& state,
    float scale,ImVec2 origin={24,180}) {
    origin=origin*scale;const ImVec2 size{340*scale,74*scale};
    const auto color=controller.decision.valid?IM_COL32(255,199,76,240):IM_COL32(96,175,248,230);
    draw->AddRectFilled(origin,origin+size,IM_COL32(15,20,28,210),10*scale);
    draw->AddRect(origin,origin+size,color,10*scale,0,scale);
    auto line=[&](float y,const char* value,ImU32 c) {draw->AddText(ImGui::GetFont(),12*scale,origin+ImVec2(12*scale,y*scale),c,value);};
    line(9,"AUTO PARRY",color);
    draw->PushClipRect(origin+ImVec2(12*scale,0),origin+size-ImVec2(12*scale,0),true);
    line(29,controller.status.c_str(),IM_COL32(235,242,255,255));
    char label[96]{};
    if(controller.decision.valid)snprintf(label,sizeof(label),"%s / contact %.0f ms",parry_threat_name(controller.decision.kind),controller.decision.impact_ms);
    else snprintf(label,sizeof(label),"Parry CD %.2fs / spell CD %.2fs",state.cooldown_remaining,state.spell_cooldown_remaining);
    line(49,label,IM_COL32(170,183,201,255));draw->PopClipRect();
}
inline void draw_parry_threat(ImDrawList* draw,const Snapshot& snapshot,const ParryDecision& threat,float width,float height,float scale) {
    if(!threat.valid)return;ScreenPoint a{},b{};
    if(!project(snapshot.matrix,threat.origin,width,height,a))return;
    const ImU32 color=IM_COL32(255,199,76,235);
    draw->AddCircle({a.x,a.y},11*scale,color,24,2*scale);
    if(project(snapshot.matrix,threat.point,width,height,b))draw->AddLine({a.x,a.y},{b.x,b.y},color,1.5f*scale);
}
}
