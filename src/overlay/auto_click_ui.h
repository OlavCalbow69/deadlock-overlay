#pragma once
#include "ray_click.h"
#include "camera_profile.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"

namespace overlay {
inline bool draw_auto_click_settings(CameraFocusSettings& regular,CameraFocusSettings& ultimate,bool& edit_ultimate,
    bool ultimate_active,const RayClick& controller,MakcuInput& input,const ImRect& body,float alpha,bool transparent) {
    using namespace solace;
    auto* draw=ImGui::GetWindowDrawList();bool changed=false;ImGui::PushID("auto_click_page");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,px(10,6));
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=14.f) {
        draw_text(draw,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);
    };
    auto card=[&](const ImRect& box,const char* title,icons::id icon) {
        rounded_panel::draw(draw,box.Min,box.Max,mo::with_alpha(c_card,(transparent?1.f:.72f)*alpha),px(16));
        draw->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),0,px(1));
        icons::draw(icon,draw,box.Min+px(20,20),px(16),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(draw,font_semibold(12),box.Min+px(46,21),mo::with_alpha(c_muted_foreground,alpha),title,px(.8f));
    };
    auto combo=[&](const ImRect& box,float y,const char* label,const char* id,int& value,const char* const* choices,int count) {
        text(box.Min+px(20,y+6),label,true);
        const float width=std::min(px(205),box.GetWidth()*.57f);
        ImGui::SetCursorScreenPos({box.Max.x-px(20)-width,box.Min.y+px(y)});ImGui::SetNextItemWidth(width);
        return ImGui::Combo(id,&value,choices,count);
    };
    text(body.Min+px(0,48),"Profile",true);
    ImGui::SetCursorScreenPos(body.Min+px(76,42));ImGui::SetNextItemWidth(px(222));
    constexpr const char* profiles[]={"Regular view","Vindicta Ultimate"};int profile=edit_ultimate?1:0;
    if(ImGui::Combo("##profile",&profile,profiles,2))edit_ultimate=profile!=0;
    const bool active=edit_ultimate==ultimate_active;
    badge("profile_active",draw,{body.Max.x-px(150),body.Min.y+px(46)},active?"Active profile":"Profile inactive",active?badge_good:badge_warn,false,alpha);
    auto& settings=edit_ultimate?ultimate:regular;
    if(edit_ultimate)restrict_assassinate_targets(settings);
    const float gap=px(16),left=(body.GetWidth()-gap)*.52f;
    ImRect activation(body.Min+px(0,90),body.Min+ImVec2(left,px(350)));
    ImRect rules({activation.Max.x+gap,activation.Min.y},{body.Max.x,activation.Max.y});
    ImRect live(body.Min+px(0,366),{body.Max.x,body.Min.y+px(514)});
    card(activation,"ACTIVATION",icons::id::target);card(rules,"SHOT RULES",icons::id::lock);card(live,"LIVE STATUS",icons::id::workflow);
    text(activation.Min+px(20,56),"Auto click");
    changed|=switch_toggle("enabled",{activation.Max.x-px(20+switch_w),activation.Min.y+px(50)},&settings.auto_click);
    if(edit_ultimate) {
        text(activation.Min+px(20,96),"Target",true);
        text({activation.Max.x-px(170),activation.Min.y+px(96)},"Enemy players");
    }else {
        constexpr const char* targets[]={"Enemy players","Soul orbs"};int target=settings.auto_click_orbs_only?1:0;
        if(combo(activation,90,"Target","##target",target,targets,2)){settings.auto_click_orbs_only=target!=0;changed=true;}
    }
    constexpr const char* activations[]={"Automatic","Hold side button"};int hold=(!settings.auto_click_orbs_only||settings.auto_click_orbs_hold_side)?1:0;
    ImGui::BeginDisabled(!settings.auto_click_orbs_only);
    if(combo(activation,130,"Activation","##activation",hold,activations,2)){settings.auto_click_orbs_hold_side=hold!=0;changed=true;}
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Automatic is available for soul orbs. Enemy-player clicks require either side button.");
    ImGui::EndDisabled();
    text(activation.Min+px(20,181),"Click interval",true);char label[128]{};
    snprintf(label,sizeof(label),"%.0f ms",settings.click_interval_ms);
    text({activation.Max.x-px(20)-text_width(font_semibold(14),label),activation.Min.y+px(181)},label);
    changed|=range_slider("interval",activation.Min+px(20,204),activation.GetWidth()-px(40),&settings.click_interval_ms,80,1000);
    text(activation.Min+px(20,235),"Camera focus can stay disabled",true,12);
    const char* checks[]={"Game focused and player alive","Crosshair inside current collision","Clear view to the hit point","Manual left mouse takes priority",
        settings.auto_click_orbs_only?"Protected / expired orbs skipped":"Living enemy players only"};
    for(int i=0;i<5;++i) {
        auto at=rules.Min+px(20,57+36.f*i);
        icons::draw(icons::id::check,draw,at,px(15),mo::with_alpha(IM_COL32(96,175,248,255),alpha));
        text(at+px(25,0),checks[i],true,12);
    }
    const char* status=!active?"Profile inactive / switches with Assassinate":!settings.auto_click?"Off":controller.status;
    draw->PushClipRect(live.Min+px(20,42),live.Max-px(20,12),true);
    text(live.Min+px(20,49),status);
    snprintf(label,sizeof(label),"Input: %s / %s",input.label(),hold?"Hold either side button":"Automatic orb shooting");
    text(live.Min+px(20,82),label,true,12);
    snprintf(label,sizeof(label),"Clicks sent this session: %llu",static_cast<unsigned long long>(input.clicks()));
    text(live.Min+px(20,110),label,true,12);
    draw->PopClipRect();
    ImGui::PopStyleVar();ImGui::PopID();return changed;
}
}
