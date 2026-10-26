#pragma once
#include "external_ui.h"
#include "ui/foundation/rounded_panel.h"

namespace overlay {
inline ImRect rift_testing_control;

template<class Settings> bool draw_hud_settings(Settings& settings,const ImRect& body,float alpha) {
    using namespace solace;
    bool changed=false;auto& e=settings.extras;
    ImGui::PushID("hud_page");
    ImGui::SetCursorScreenPos(body.Min+px(0,42));
    ImGui::BeginChild("HUD controls",{body.GetWidth(),body.GetHeight()-px(42)},false);
    auto* draw=ImGui::GetWindowDrawList();const auto origin=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x,gap=px(16),half=(width-gap)*.5f;
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=14.f) {
        draw_text(draw,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);
    };
    auto card=[&](const ImRect& box,const char* title,icons::id icon) {
        rounded_panel::draw(draw,box.Min,box.Max,mo::with_alpha(c_card,(settings.transparent_frame?1.f:.72f)*alpha),px(16));
        draw->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),0,px(1));
        icons::draw(icon,draw,box.Min+px(18,18),px(16),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(draw,font_semibold(12),box.Min+px(44,19),mo::with_alpha(c_muted_foreground,alpha),title,px(.8f));
    };
    auto toggle=[&](const ImRect& box,float y,const char* id,const char* label,bool& value) {
        text(box.Min+px(18,y+4),label);
        return switch_toggle(id,{box.Max.x-px(18+switch_w),box.Min.y+px(y)},&value);
    };
    ImRect events(origin,origin+ImVec2(half,px(160)));
    ImRect status(origin+px(0,176),origin+ImVec2(half,px(350)));
    ImRect preview(origin+px(0,366),origin+ImVec2(half,px(490)));
    ImRect crosshair({events.Max.x+gap,origin.y},origin+ImVec2(width,px(e.crosshair?490.f:104.f)));
    card(events,"EVENTS",icons::id::clock);card(status,"STATUS",icons::id::layout_grid);
    card(preview,"PREVIEW",icons::id::eye);card(crosshair,"CROSSHAIR",icons::id::crosshair_simple);
    changed|=toggle(events,54,"midboss","Mid Boss",settings.show_midboss_hud);
    changed|=toggle(events,98,"rift","Rift",e.rift_hud);
    auto warning=events.Min+ImVec2(px(26)+text_width(font_semibold(14),"Rift"),px(101));
    rift_testing_control={warning,warning+px(20,20)};
    ImGui::SetCursorScreenPos(warning);ImGui::InvisibleButton("rift_testing",px(20,20));
    const auto warning_color=mo::with_alpha(IM_COL32(225,182,105,255),alpha);
    draw->AddCircle(rift_testing_control.GetCenter(),px(8),warning_color,24,px(1));
    const float mark_width=text_width(font_semibold(12),"!");
    text({rift_testing_control.GetCenter().x-mark_width*.5f,warning.y+px(2)},"!",false,12);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("In testing");
    changed|=toggle(status,50,"parry","Parry",settings.auto_parry.show_hud);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Shown while Auto Parry is enabled.");
    changed|=toggle(status,90,"session","Session",settings.show_monitor_hud);
    changed|=toggle(status,130,"spectators","Spectators",e.spectators);
    ImGui::SetCursorScreenPos(preview.Min+px(18,50));
    if(extra_action("Preview HUDs",std::min(px(170),half-px(36)),false,px(30))) {
        e.visual_preview=true;e.preview_page=1;changed=true;
    }
    text(preview.Min+px(18,91),"Examples in the side preview window",true,12);
    changed|=toggle(crosshair,54,"crosshair","Custom crosshair",e.crosshair);
    if(e.crosshair) {
        ImGui::SetCursorScreenPos(crosshair.Min+px(18,100));
        ImGui::BeginChild("Crosshair appearance",{crosshair.GetWidth()-px(36),crosshair.GetHeight()-px(118)},false);
        changed|=extra_toggle("Center dot",e.crosshair_dot);
        changed|=extra_toggle("Outline",e.crosshair_outline);
        changed|=extra_slider("Size",&e.crosshair_size,2,24);
        changed|=extra_slider("Gap",&e.crosshair_gap,0,16);
        changed|=extra_slider("Width",&e.crosshair_width,1,4);
        changed|=extra_color4("Color",&e.crosshair_color.x);
        ImGui::EndChild();
    }
    ImGui::SetCursorScreenPos(origin+px(0,490));ImGui::Dummy({0,0});
    ImGui::EndChild();ImGui::PopID();return changed;
}
}
