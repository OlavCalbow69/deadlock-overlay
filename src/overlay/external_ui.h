#pragma once
#include "external_settings.h"
#include "health_visuals.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"
#include <cstring>
namespace overlay {
inline int visual_preview_section=-1;
inline bool external_preview_expanded=false;
inline ImRect minimap_corner_control;
inline void apply_overlay_control_style() {
    auto& style=ImGui::GetStyle();auto color=[](ImU32 value){return ImGui::ColorConvertU32ToFloat4(value);};
    style.Colors[ImGuiCol_Text]=color(solace::c_foreground);style.Colors[ImGuiCol_TextDisabled]=color(solace::c_muted_foreground);
    style.Colors[ImGuiCol_PopupBg]=color(solace::c_card);style.Colors[ImGuiCol_Border]=color(solace::c_border_strong);
    style.Colors[ImGuiCol_Button]=color(solace::c_card);style.Colors[ImGuiCol_ButtonHovered]=color(solace::mo::with_alpha(solace::c_foreground,.12f));style.Colors[ImGuiCol_ButtonActive]=color(solace::mo::with_alpha(solace::c_foreground,.2f));
    style.Colors[ImGuiCol_FrameBg]=color(solace::c_card);style.Colors[ImGuiCol_FrameBgHovered]=style.Colors[ImGuiCol_ButtonHovered];style.Colors[ImGuiCol_FrameBgActive]=style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_Header]=style.Colors[ImGuiCol_ButtonHovered];style.Colors[ImGuiCol_HeaderHovered]=style.Colors[ImGuiCol_ButtonActive];style.Colors[ImGuiCol_HeaderActive]=style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_CheckMark]=color(solace::c_primary);style.Colors[ImGuiCol_SliderGrab]=color(solace::c_primary);style.Colors[ImGuiCol_SliderGrabActive]=color(solace::c_foreground);
    style.Colors[ImGuiCol_Separator]=color(solace::c_border_strong);style.FrameRounding=solace::px(10);style.PopupRounding=solace::px(14);style.PopupBorderSize=solace::px(1);
}
inline bool extra_action(const char* label,float width=0,bool disabled=false,float height=0) {
    ImGui::PushID(label);auto state=solace::ui_runtime::animation_state<solace::stateful_button_state>(ImGui::GetID("button"));
    solace::stateful_button_update(*state,solace::btn_idle,label,ImGui::GetIO().DeltaTime);
    if(width<=0)width=std::min(ImGui::GetContentRegionAvail().x,ImGui::CalcTextSize(label).x+solace::px(32));
    const bool pressed=solace::stateful_button_draw("button",*state,solace::btn_idle,ImGui::GetCursorScreenPos(),width,disabled,height);ImGui::PopID();return pressed&&!disabled;
}
inline bool extra_checkbox(const char* label,bool& value) {
    ImGui::PushID(label);auto state=solace::ui_runtime::animation_state<solace::checkbox_state>(ImGui::GetID("checkbox"));
    solace::checkbox_update(*state,value,ImGui::GetIO().DeltaTime);
    const bool before=value;solace::checkbox_draw("checkbox",*state,&value,label,ImGui::GetCursorScreenPos(),false);
    ImGui::PopID();return value!=before;
}
inline bool extra_select_at(const char* id,ImVec2 at,float width,int& value,const char* const* options,int count) {
    const int before=value;solace::select(id,at,width,options,count,&value,"");return before!=value;
}
inline bool extra_slider(const char* label,float* value,float minimum,float maximum,const char* format="%.2f") {
    ImGui::PushID(label);const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
    char text[80]{};std::snprintf(text,sizeof(text),format,*value);ImGui::TextUnformatted(label);
    ImGui::GetWindowDrawList()->AddText({p.x+width-ImGui::CalcTextSize(text).x,p.y},ImGui::GetColorU32(ImGuiCol_Text),text);
    bool changed=solace::range_slider("value",{p.x,p.y+solace::px(18)},width,value,minimum,maximum);
    ImGui::SetCursorScreenPos({p.x,p.y+solace::px(62)});ImGui::Dummy({0,0});ImGui::PopID();return changed;
}
inline bool extra_color4(const char* label,float* color) {
    ImGui::PushID(label);auto p=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;ImGui::TextUnformatted(label);auto next=ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({p.x+width-solace::px(42),p.y});bool changed=ImGui::ColorEdit4("##color",color,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel|ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::SetCursorScreenPos({p.x,std::max(next.y,p.y+solace::px(34))});ImGui::Dummy({0,0});ImGui::PopID();return changed;
}
inline bool extra_color3(const char* label,float* color) {float rgba[]={color[0],color[1],color[2],1};if(!extra_color4(label,rgba))return false;std::copy_n(rgba,3,color);return true;}
inline bool extra_combo(const char* label,int* value,const char* const* options,int count) {
    ImGui::PushID(label);const auto p=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;ImGui::TextUnformatted(label);
    const int before=*value;float select_width=std::min(solace::px(245),width*.48f);
    if(std::strcmp(label,"Corner")==0)minimap_corner_control={{p.x+width-select_width,p.y},{p.x+width,p.y+solace::px(solace::select_h)}};
    solace::select("value",{p.x+width-select_width,p.y},select_width,options,count,value,label);
    ImGui::SetCursorScreenPos({p.x,p.y+solace::px(48)});ImGui::Dummy({0,0});ImGui::PopID();return *value!=before;
}
inline bool extra_combo(const char* label,int* value,const char* packed) {
    std::array<const char*,32> options{};int count{};for(auto at=packed;*at&&count<int(options.size());at+=std::strlen(at)+1)options[count++]=at;return extra_combo(label,value,options.data(),count);
}
inline bool extra_toggle(const char* label,bool& value) {
    ImGui::PushID(label);auto p=ImGui::GetCursorScreenPos();const float w=ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(label);const auto next=ImGui::GetCursorScreenPos();
    bool changed=solace::switch_toggle("switch",{p.x+w-solace::px(48),p.y},&value);
    ImGui::SetCursorScreenPos({next.x,std::max(next.y,p.y+solace::px(32))});ImGui::Dummy({0,0});ImGui::PopID();return changed;
}
inline bool extra_binding(const char* label,int& key) {
    bool changed=false;ImGui::PushID(label);
    static HealthKeyCapture capture;static ImGuiID owner{};
    auto id=ImGui::GetID("binding");auto text=key?health_visuals_key_name(key):std::string("None");
    const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,button=std::min(solace::px(120),width*.45f);
    ImGui::TextUnformatted(label);ImGui::SetCursorScreenPos({p.x+width-button,p.y});
    if(extra_action(text.c_str(),button)){owner=id;capture.begin();ImGui::OpenPopup("Bind key");}
    ImGui::SetCursorScreenPos({p.x,p.y+solace::px(48)});ImGui::Dummy({0,0});
    if(ImGui::BeginPopup("Bind key")) {
        ImGui::TextUnformatted("Release held keys, then press a key or mouse button.");
        if(owner==id){int result=capture.poll();if(result>0){key=result;changed=true;ImGui::CloseCurrentPopup();}else if(result<0)ImGui::CloseCurrentPopup();}
        if(extra_action("Clear binding")){key=0;changed=true;capture.cancel();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(owner==id&&!ImGui::IsPopupOpen("Bind key"))capture.cancel();ImGui::PopID();return changed;
}
template<class Settings> bool draw_visual_page(Settings& s,const Snapshot& snapshot,const ImRect& body) {
    using namespace solace;bool changed=false;
    static int section{};if(visual_preview_section>=0)section=std::clamp(visual_preview_section,0,4);
    ImGui::SetCursorScreenPos(body.Min+px(0,42));
    ImGui::BeginChild("Visual controls",{body.GetWidth(),body.GetHeight()-px(42)},false);
    if(section<3){changed|=extra_toggle("Hold visuals to display",s.health_visuals.hold_only);changed|=extra_binding("Display key",s.health_visuals.key);}
    else ImGui::TextDisabled("Independent from the player display key.");
    const char* labels[]={"Health bars","Skeletons / Hitboxes","Hero tags","World","Minimap"};
    const auto p=ImGui::GetCursorScreenPos();tabs("Visual sections",p,labels,5,&section,tabs_underline);
    ImGui::SetCursorScreenPos({p.x,p.y+tabs_height(tabs_underline)+px(15)});ImGui::Dummy({0,0});
    if(section==0) {
        changed|=extra_toggle("Health bars",s.enabled);
        changed|=extra_toggle("Health numbers",s.numbers);
        changed|=extra_toggle("Hide when visible",s.hide_visible_healthbars);
        changed|=extra_slider("World gap (m)",&s.healthbar_box_gap_meters,0,2);
        changed|=extra_toggle("Debug placement box",s.healthbar_debug_box);
    }else if(section==1) {
        changed|=extra_toggle("Skeletons",s.skeletons);
        changed|=extra_toggle("Hide visible skeletons",s.hide_visible_skeletons);
        changed|=extra_toggle("Hitboxes",s.hitboxes);
        changed|=extra_color3("Visible",&s.skeleton_visible.x);
        changed|=extra_color3("Blocked",&s.skeleton_hidden.x);
    }else if(section==2) {
        changed|=extra_toggle("Hero tags below feet",s.hidden_enemy_tags);
        changed|=extra_toggle("Hero icons instead of names",s.extras.hero_icons);
        if(s.extras.hero_icons)changed|=extra_slider("Icon size",&s.extras.hero_icon_size,16,56,"%.0f");
        changed|=extra_toggle("Distance",s.extras.player_distance);
        changed|=extra_toggle("Hide tags when visible",s.hide_visible_enemy_tags);
        ImGui::TextDisabled("Distance also works with hero tags switched off.");
    }else if(section==3) {
        auto& e=s.extras;changed|=extra_toggle("World markers",e.world);
        if(ImGui::BeginTable("World categories",2,ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();changed|=extra_toggle("Lane minions",e.world_lane);changed|=extra_toggle("Friendly minions",e.world_allies);changed|=extra_toggle("Jungle NPCs",e.world_jungle);
            ImGui::TableNextColumn();changed|=extra_toggle("Soul / buff boxes",e.world_breakables);changed|=extra_toggle("Ground pickups",e.world_pickups);changed|=extra_toggle("Buff spawners",e.world_spawners);ImGui::EndTable();
        }
        changed|=extra_toggle("Respawn timers",e.world_timers);changed|=extra_toggle("Show destroyed / waiting",e.world_destroyed);
        changed|=extra_toggle("Distance labels",e.world_distance);
        if(ImGui::BeginTable("World appearance",2,ImGuiTableFlags_SizingStretchSame)) {ImGui::TableNextColumn();changed|=extra_slider("Range (m)",&e.world_range,10,1000,"%.0f");ImGui::TableNextColumn();changed|=extra_slider("Icon size",&e.world_icon_size,12,40,"%.0f");ImGui::EndTable();}
        ImGui::TextDisabled("~ marks an estimate after an observed box destruction.");
        ImGui::TextWrapped("%s",snapshot.world.status.c_str());
    }else {
        auto& e=s.extras;changed|=extra_toggle("Minimap",e.minimap);changed|=extra_toggle("Players",e.minimap_players);
        changed|=extra_toggle("Include teammates",e.minimap_allies);changed|=extra_toggle("World objects",e.minimap_world);
        changed|=extra_toggle("Colored lanes",e.minimap_lanes);
        changed|=extra_combo("Corner",&e.minimap_corner,"Top left\0Top right\0Bottom left\0Bottom right\0");
        if(ImGui::BeginTable("Map sizes",2,ImGuiTableFlags_SizingStretchSame)) {ImGui::TableNextColumn();changed|=extra_slider("Map size",&e.minimap_size,180,600,"%.0f");ImGui::TableNextColumn();changed|=extra_slider("Hero icon size",&e.minimap_icon_size,12,32,"%.0f");ImGui::EndTable();}
        changed|=extra_slider("Opacity",&e.minimap_opacity,.1f,1,"%.2f");
        ImGui::TextDisabled("All detected enemies. Independent from world / player visuals.");
        ImGui::TextDisabled("World categories are shared with the World section.");
        ImGui::TextWrapped("%s",snapshot.world.status.c_str());
    }
    ImGui::EndChild();return changed;
}
inline bool draw_extra_appearance(ExternalSettings& e) {
    bool changed=false;
    bool limited=e.render_cap>0;if(extra_toggle("Limit overlay FPS",limited)){e.render_cap=limited?120.f:0.f;changed=true;}
    if(limited)changed|=extra_slider("FPS cap",&e.render_cap,30,240,"%.0f");
    ImGui::TextDisabled("Display refresh pacing remains the upper limit.");
    return changed;
}
}
