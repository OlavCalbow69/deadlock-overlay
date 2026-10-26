#pragma once
#include "external_ui.h"
#include "preset_store.h"
#include <shellapi.h>
namespace overlay {
template<class Settings,class Save,class Load,class Cancel>
bool draw_presets_page(Settings& settings,const std::filesystem::path& root,const ImRect& body,Save save,Load load,Cancel cancel) {
    bool changed=false;static char name[33]{};static std::string selected,message;static bool confirm{};
    ImGui::SetCursorScreenPos(body.Min+solace::px(0,45));ImGui::BeginChild("Preset controls",{body.GetWidth(),body.GetHeight()-solace::px(45)},false);
    ImGui::Text("Active: %s",active_preset(root).empty()?"Default settings":active_preset(root).c_str());
    auto name_state=solace::ui_runtime::animation_state<solace::input_state>(ImGui::GetID("config_name"));
    solace::input_desc description;description.label="Config name";description.placeholder="Name this configuration";description.buf=name;description.buf_size=sizeof(name);
    solace::input_update(*name_state,description,ImGui::GetIO().DeltaTime);
    solace::input_draw("config_name",*name_state,description,ImGui::GetCursorScreenPos(),std::min(ImGui::GetContentRegionAvail().x,solace::px(420)),nullptr);
    if(extra_action("Save as named configuration")) {
        if(valid_preset_name(name)){std::error_code ec;std::filesystem::create_directories(root/L"configs",ec);auto path=preset_path(root,name);auto temp=path;temp+=L".tmp";save(settings,temp);if(valid_preset_file(temp)&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)&&remember_preset(root,name)){selected=name;message="Configuration saved";}else message="Save failed";}
        else message="Use 1-32 letters, digits, spaces, - or _.";
    }
    ImGui::SeparatorText("Saved configurations");
    for(auto& preset:preset_names(root))if(ImGui::Selectable(preset.c_str(),selected==preset)){selected=preset;confirm=false;}
    if(extra_action("Load selected")) {
        auto path=preset_path(root,selected);
        if(!path.empty()&&valid_preset_file(path)){auto next=load(path);disable_preset_automation(next);cancel();settings=std::move(next);remember_preset(root,selected);message="Loaded. Automation is off.";changed=true;}else message="Select a valid configuration";
    }
    ImGui::SameLine();if(extra_action("Save selected")&&!selected.empty()){auto path=preset_path(root,selected),temp=path;temp+=L".tmp";save(settings,temp);message=valid_preset_file(temp)&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?"Saved":"Save failed";}
    if(extra_action(confirm?"Confirm delete":"Delete selected")&&!selected.empty()){if(confirm){std::error_code ec;std::filesystem::remove(preset_path(root,selected),ec);if(active_preset(root)==selected)remember_preset(root,"");selected.clear();confirm=false;message=ec?"Delete failed":"Deleted";}else confirm=true;}
    changed|=extra_toggle("Auto save active preset / on exit",settings.extras.auto_save);
    if(extra_action("Use default settings file")){cancel();settings=load(root/L"settings.ini");disable_preset_automation(settings);remember_preset(root,"");message="Default settings loaded. Automation is off.";changed=true;}
    ImGui::SameLine();if(extra_action("Open configs folder")){std::error_code ec;std::filesystem::create_directories(root/L"configs",ec);ShellExecuteW(nullptr,L"open",(root/L"configs").c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
    ImGui::TextWrapped("%s",message.c_str());ImGui::EndChild();return changed;
}
}
