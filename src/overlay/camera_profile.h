#pragma once
#include "focus_area.h"
#include "focus_input.h"
#include "focus_hitboxes.h"
#include "adaptive_settings.h"
#include <array>
#include <ostream>
#include <sstream>
#include <string_view>

namespace overlay {
struct CameraFocusSettings {
    bool exclude_teammates=false,visible_only=true;
    bool focus_players=true,focus_minions=false,focus_orbs=false;
    bool strict_enemy_players=false; // Runtime policy for Assassinate; not a user setting.
    bool replay_focus=true,debug_focus=true;
    int focus_bone=1;
    float focus_speed=12.f,projectile_speed=30000.f;
    // Zero means an older configuration: seed X2 from that profile's X1 speed.
    float focus_speed_x2=0.f,adaptive_speed_x2=0.f;
    bool x2_target_lock=false;
    bool free_focus=false,prediction=false,inherit_velocity=false,auto_speed=true;
    FreeMovementMode free_focus_mode{FreeMovementMode::BoneLine};
    FocusHoldButton focus_hold_button{FocusHoldButton::SideButtons};
    bool focus_visuals=true;
    bool auto_click=false,auto_click_orbs_only=false;
    bool auto_click_orbs_hold_side=false;
    float click_interval_ms=150.f;
    AdaptiveSettings adaptive{};
};
inline void restrict_assassinate_targets(CameraFocusSettings& settings) {
    settings.focus_players=true;settings.focus_minions=settings.focus_orbs=false;
    settings.exclude_teammates=settings.strict_enemy_players=true;
    settings.auto_click_orbs_only=false;
    settings.prediction=false; // Hitscan has no projectile travel time.
    settings.adaptive.prediction=true; // Keep only timestamp/data-age compensation while scoped.
}
inline CameraFocusSettings assassinate_controls(CameraFocusSettings settings={}) {
    restrict_assassinate_targets(settings);return settings;
}
inline void sanitize_camera_settings(CameraFocusSettings& s) {
    // Stored value 1 was V2. Preserve its full-body free movement through Adaptive.
    if(s.free_focus_mode==FreeMovementMode::Hitboxes){s.free_focus_mode=FreeMovementMode::Adaptive;s.adaptive.region=AdaptiveRegion::WholeBody;}
    s.focus_speed=std::isfinite(s.focus_speed)?std::clamp(s.focus_speed,1.f,120.f):12.f;
    s.focus_speed_x2=std::isfinite(s.focus_speed_x2)&&s.focus_speed_x2>0?std::clamp(s.focus_speed_x2,1.f,240.f):std::min(240.f,s.focus_speed*2);
    s.projectile_speed=std::isfinite(s.projectile_speed)?std::clamp(s.projectile_speed,1000.f,100000.f):30000.f;
    s.focus_bone=std::clamp(s.focus_bone,0,2);
    s.click_interval_ms=std::isfinite(s.click_interval_ms)?std::clamp(s.click_interval_ms,80.f,1000.f):150.f;
    sanitize_adaptive(s.adaptive);
    s.adaptive_speed_x2=std::isfinite(s.adaptive_speed_x2)&&s.adaptive_speed_x2>0?std::clamp(s.adaptive_speed_x2,1.f,140.f):std::min(140.f,s.adaptive.speed+27.2f);
}
inline bool read_camera_setting(CameraFocusSettings& s,std::string_view key,std::istringstream& data) {
    bool* flag=key=="exclude_teammates"?&s.exclude_teammates:key=="visible_only"?&s.visible_only
        :key=="focus_players"?&s.focus_players:key=="focus_minions"?&s.focus_minions:key=="focus_orbs"?&s.focus_orbs
        :key=="replay_focus"?&s.replay_focus:key=="debug_focus"?&s.debug_focus:key=="focus_visuals"?&s.focus_visuals
        :key=="free_focus"?&s.free_focus:key=="prediction"?&s.prediction:key=="inherit_velocity"?&s.inherit_velocity
        :key=="auto_speed"?&s.auto_speed:key=="auto_click"?&s.auto_click:key=="auto_click_orbs_only"?&s.auto_click_orbs_only
        :key=="auto_click_orbs_hold_side"?&s.auto_click_orbs_hold_side:key=="x2_target_lock"?&s.x2_target_lock:nullptr;
    if(flag){int n{};if(data>>n)*flag=n!=0;return true;}
    if(key=="focus_bone"){int n{};if(data>>n)s.focus_bone=std::clamp(n,0,2);return true;}
    if(key=="free_focus_mode"){int n{};if(data>>n)s.free_focus_mode=static_cast<FreeMovementMode>(std::clamp(n,0,2));return true;}
    if(key=="adaptive_region"){int n{};if(data>>n)s.adaptive.region=selectable_adaptive_region(n);return true;}
    if(key=="adaptive_area_mode"){int n{};if(data>>n)s.adaptive.area=static_cast<AdaptiveAreaMode>(std::clamp(n,0,1));return true;}
    if(key=="adaptive_show_zone"){int n{};if(data>>n)s.adaptive.show_target_zone=n!=0;return true;}
    if(key=="adaptive_prediction"||key=="adaptive_show_area"){int n{};if(data>>n)(key=="adaptive_prediction"?s.adaptive.prediction:s.adaptive.show_area)=n!=0;return true;}
    float* adaptive_value=key=="adaptive_radius"?&s.adaptive.radius_degrees:key=="adaptive_speed"?&s.adaptive.speed
        :key=="adaptive_max_rate"?&s.adaptive.max_rate:key=="adaptive_max_acceleration"?&s.adaptive.max_acceleration
        :key=="adaptive_padding_percent"?&s.adaptive.padding_percent:nullptr;
    if(adaptive_value){float n{};if(data>>n&&std::isfinite(n))*adaptive_value=n;return true;}
    if(key=="focus_hold_button"){int n{};if(data>>n)s.focus_hold_button=static_cast<FocusHoldButton>(std::clamp(n,0,3));return true;}
    float* value=key=="focus_speed"?&s.focus_speed:key=="focus_speed_x2"?&s.focus_speed_x2:key=="adaptive_speed_x2"?&s.adaptive_speed_x2
        :key=="projectile_speed"?&s.projectile_speed:key=="click_interval_ms"?&s.click_interval_ms:nullptr;
    if(value){float n{};if(data>>n&&std::isfinite(n))*value=n;return true;}
    return false;
}
inline void write_camera_settings(std::ostream& out,const CameraFocusSettings& s,std::string_view prefix={}) {
    auto write=[&](const char* key,const auto& value){out<<prefix<<key<<'='<<value<<'\n';};
    write("exclude_teammates",s.exclude_teammates);write("visible_only",s.visible_only);
    write("focus_players",s.focus_players);write("focus_minions",s.focus_minions);write("focus_orbs",s.focus_orbs);
    write("replay_focus",s.replay_focus);write("debug_focus",s.debug_focus);write("focus_visuals",s.focus_visuals);
    write("focus_bone",s.focus_bone);write("focus_speed",s.focus_speed);
    write("focus_speed_x2",s.focus_speed_x2);write("adaptive_speed_x2",s.adaptive_speed_x2);
    write("x2_target_lock",s.x2_target_lock);
    write("free_focus",s.free_focus);write("free_focus_mode",int(s.free_focus_mode==FreeMovementMode::Hitboxes?FreeMovementMode::Adaptive:s.free_focus_mode));write("focus_hold_button",int(s.focus_hold_button));
    write("prediction",s.prediction);write("auto_speed",s.auto_speed);
    write("inherit_velocity",s.inherit_velocity);write("projectile_speed",s.projectile_speed);
    write("auto_click",s.auto_click);write("auto_click_orbs_only",s.auto_click_orbs_only);write("click_interval_ms",s.click_interval_ms);
    write("auto_click_orbs_hold_side",s.auto_click_orbs_hold_side);
    write("focus_area_version",2);
    write("adaptive_region",int(s.free_focus_mode==FreeMovementMode::Hitboxes?AdaptiveRegion::WholeBody:selectable_adaptive_region(int(s.adaptive.region))));write("adaptive_radius",s.adaptive.radius_degrees);write("adaptive_speed",s.adaptive.speed);
    write("adaptive_max_rate",s.adaptive.max_rate);write("adaptive_max_acceleration",s.adaptive.max_acceleration);
    write("adaptive_prediction",s.adaptive.prediction);write("adaptive_show_area",s.adaptive.show_area);
    write("adaptive_area_mode",int(s.adaptive.area));write("adaptive_padding_percent",s.adaptive.padding_percent);
    write("adaptive_show_zone",s.adaptive.show_target_zone);
}
struct FocusActivation {
    FocusHoldButton button{FocusHoldButton::X1};
    float speed{},adaptive_speed{};
    bool target_lock{};
};
inline FocusActivation resolve_focus_activation(const CameraFocusSettings& settings,bool x2_down,bool lock_session=false) {
    auto button=settings.focus_hold_button;
    const bool locked=(settings.x2_target_lock||lock_session)&&x2_down;
    if(locked)button=FocusHoldButton::X2;
    // X2 takes priority when both are held; queued input keeps this exact button.
    if(button==FocusHoldButton::SideButtons)button=x2_down?FocusHoldButton::X2:FocusHoldButton::X1;
    const bool secondary=button==FocusHoldButton::X2;
    const float speed_x2=settings.focus_speed_x2>0?settings.focus_speed_x2:std::min(240.f,settings.focus_speed*2);
    const float adaptive_x2=settings.adaptive_speed_x2>0?settings.adaptive_speed_x2:std::min(140.f,settings.adaptive.speed+27.2f);
    return {button,secondary?speed_x2:settings.focus_speed,secondary?adaptive_x2:settings.adaptive.speed,locked};
}
inline FocusActivation resolve_focus_activation(const CameraFocusSettings& settings) {return resolve_focus_activation(settings,focus_button_held(FocusHoldButton::X2));}
inline bool show_focus_area(const CameraFocusSettings& settings) {
    return settings.adaptive.area==AdaptiveAreaMode::TargetZone?settings.adaptive.show_target_zone:settings.adaptive.show_area;
}
// The area follows the movement mode's permitted anatomy; all modes share the
// same angle/zone settings without changing the selected movement controller.
inline AdaptiveSettings focus_area_settings(const CameraFocusSettings& settings) {
    auto result=settings.adaptive;
    if(!settings.free_focus)result.region=AdaptiveRegion::SelectedBone;
    else if(settings.free_focus_mode==FreeMovementMode::BoneLine)result.region=AdaptiveRegion::HeadTorso;
    else if(settings.free_focus_mode==FreeMovementMode::Hitboxes)result.region=AdaptiveRegion::WholeBody;
    return result;
}
struct FocusAreaMigration {
    int legacy_mode{-1};bool current{};
    std::array<bool,3> drawing{},drawing_present{};
    bool read(std::string_view key,std::istringstream& data) {
        if(key=="focus_area_version"){int value{};if(data>>value)current=value>=2;return true;}
        if(key=="focus_area_mode"){int value{};if(data>>value)legacy_mode=std::clamp(value,0,2);return true;}
        if(key=="focus_fov_percent"||key=="focus_radius_meters")return true;
        if(key=="show_focus_area_0"||key=="show_focus_area_1"||key=="show_focus_area_2") {
            int value{};if(data>>value){const auto index=key.back()-'0';drawing[index]=value!=0;drawing_present[index]=true;}return true;
        }
        return false;
    }
    void apply(CameraFocusSettings& settings)const {
        if(current||legacy_mode<0||(settings.free_focus&&settings.free_focus_mode==FreeMovementMode::Adaptive))return;
        // Screen percentages/meters cannot be converted into degrees/body-height
        // padding without a particular live view. Migrate to the new defaults.
        settings.adaptive.area=legacy_mode==0?AdaptiveAreaMode::CrosshairAngle:AdaptiveAreaMode::TargetZone;
        if(legacy_mode==0)settings.adaptive.radius_degrees=6.f;
        else settings.adaptive.padding_percent=12.f;
        if(drawing_present[legacy_mode]) {
            if(legacy_mode==0)settings.adaptive.show_area=drawing[legacy_mode];
            else settings.adaptive.show_target_zone=drawing[legacy_mode];
        }
    }
};
}
