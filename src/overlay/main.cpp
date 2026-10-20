#include "game_reader.h"
#include "replay_camera.h"
#include "camera_profile.h"
#include "ray_click.h"
#include "auto_parry_ui.h"
#include "auto_click_ui.h"
#include "adaptive_focus.h"
#include "x2_target_lock.h"
#include "target_zone_outline.h"
#include "health_visuals.h"
#include "frame_pacing.h"
#include "game_frames.h"
#include "data_update.h"
#include "session_monitor_ui.h"
#include "platform/d3d11_renderer.h"
#include "ui/foundation/theme.h"
#include "ui/foundation/typography/font_cache.h"
#include "ui/controls/widgets.h"
#include "generated/fonts/geist_data.h"
#include "ui/screens/shell.h"
#include "ui/foundation/primitives.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/theme_toggle.h"
#include "ui/effects/glass_cursor.h"
#include "graphics/snapshot.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <iostream>
#include <psapi.h>
#include <tlhelp32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace overlay {
double clock_ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool prefer_composition=true;
bool high_priority=true;
bool menu_preview_mode=false;
std::atomic_bool shutdown_requested{false};
HANDLE shutdown_complete{};
BOOL WINAPI console_handler(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT && event!=CTRL_CLOSE_EVENT
        && event!=CTRL_LOGOFF_EVENT && event!=CTRL_SHUTDOWN_EVENT)return FALSE;
    shutdown_requested.store(true);
    if(event==CTRL_CLOSE_EVENT || event==CTRL_LOGOFF_EVENT || event==CTRL_SHUTDOWN_EVENT)
        if(shutdown_complete)WaitForSingleObject(shutdown_complete,4000);
    return TRUE;
}
std::filesystem::path executable_directory() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);return std::filesystem::path(path).parent_path();
}
std::filesystem::path config_path() {
    wchar_t path[32768]{}; DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    auto directory=size && size<32768 ? std::filesystem::path(path)/L"DeadlockOverlay" : executable_directory();
    std::filesystem::create_directories(directory); return directory/L"settings.ini";
}
struct Settings : CameraFocusSettings {
    bool enabled=true, numbers=true,glass=true,dark=true;
    float healthbar_box_gap_meters=.25f;
    bool healthbar_debug_box=false,hide_visible_healthbars=false;
    bool transparent_frame=true;float frame_opacity=.72f;
    bool show_monitor_hud=false,show_midboss_hud=true;
    bool skeletons=false,hitboxes=false,hide_visible_skeletons=false;
    bool hidden_enemy_tags=false,hide_visible_enemy_tags=true;
    HealthVisualsActivation health_visuals{};
    ImVec4 skeleton_visible{.2f,.62f,1.f,1.f},skeleton_hidden{.68f,.32f,1.f,1.f};
    bool vindicta_ult_override=true;
    CameraFocusSettings vindicta_ult=assassinate_controls();
    AutoParrySettings auto_parry{};
};
void save(const Settings& s,const std::filesystem::path& path=config_path()) {
    if(menu_preview_mode)return;
    std::ofstream out(path);out<<"enabled="<<s.enabled<<"\nnumbers="<<s.numbers<<"\nglass="<<s.glass<<"\ndark="<<s.dark<<'\n';
    write_camera_settings(out,s);
    out<<"vindicta_ult_override="<<s.vindicta_ult_override<<'\n';
    write_camera_settings(out,assassinate_controls(s.vindicta_ult),"vindicta_ult_");
    write_parry_settings(out,s.auto_parry);
    out<<"transparent_frame="<<s.transparent_frame<<"\nframe_opacity="<<s.frame_opacity<<'\n';
    out<<"show_monitor_hud="<<s.show_monitor_hud<<'\n';
    out<<"show_midboss_hud="<<s.show_midboss_hud<<'\n';
    out<<"healthbar_box_gap_meters="<<s.healthbar_box_gap_meters<<"\nhealthbar_debug_box="<<s.healthbar_debug_box<<'\n';
    out<<"hide_visible_healthbars="<<s.hide_visible_healthbars<<"\nhide_visible_enemy_tags="<<s.hide_visible_enemy_tags<<'\n';
    out<<"skeletons="<<s.skeletons<<'\n';
    out<<"hide_visible_skeletons="<<s.hide_visible_skeletons<<'\n';
    out<<"hitboxes="<<s.hitboxes<<'\n';
    out<<"hidden_enemy_tags="<<s.hidden_enemy_tags<<'\n';
    out<<"health_visuals_hold="<<s.health_visuals.hold_only<<"\nhealth_visuals_key="<<s.health_visuals.key<<'\n';
    out<<"skeleton_visible="<<s.skeleton_visible.x<<' '<<s.skeleton_visible.y<<' '<<s.skeleton_visible.z<<'\n';
    out<<"skeleton_hidden="<<s.skeleton_hidden.x<<' '<<s.skeleton_hidden.y<<' '<<s.skeleton_hidden.z<<'\n';
}
Settings load(const std::filesystem::path& path=config_path()) {
    Settings s;std::ifstream in(path);std::string line;
    bool legacy_override=true,loaded_override=false,legacy_tags_hold=false,loaded_visuals_hold=false;float legacy_speed{};
    std::vector<std::pair<std::string,std::string>> ult_values;
    FocusAreaMigration area_migration,ult_area_migration;
    while(std::getline(in,line)) {
        auto at=line.find('=');if(at==std::string::npos) continue;
        auto key=line.substr(0,at),value=line.substr(at+1);std::istringstream data(value);
        if(key=="vindicta_ult_override") {int n{};if(data>>n){s.vindicta_ult_override=n!=0;loaded_override=true;}continue;}
        if(key.starts_with("vindicta_ult_")){ult_values.emplace_back(key.substr(13),value);continue;}
        if(area_migration.read(key,data))continue;
        if(read_camera_setting(s,key,data))continue;
        if(read_parry_setting(s.auto_parry,key,data))continue;
        if(key=="enabled") {int n{};if(data>>n)s.enabled=n!=0;}
        else if(key=="numbers") {int n{};if(data>>n)s.numbers=n!=0;}
        else if(key=="glass") {int n{};if(data>>n)s.glass=n!=0;}
        else if(key=="transparent_frame") {int n{};if(data>>n)s.transparent_frame=n!=0;}
        else if(key=="show_monitor_hud") {int n{};if(data>>n)s.show_monitor_hud=n!=0;}
        else if(key=="show_midboss_hud") {int n{};if(data>>n)s.show_midboss_hud=n!=0;}
        else if(key=="healthbar_debug_box") {int n{};if(data>>n)s.healthbar_debug_box=n!=0;}
        else if(key=="healthbar_box_gap_meters") {float n{};if(data>>n&&std::isfinite(n))s.healthbar_box_gap_meters=n;}
        else if(key=="dark") {int n{};if(data>>n)s.dark=n!=0;}

        else if(key=="sniper_speed_override") {int n{};if(data>>n)legacy_override=n!=0;}
        else if(key=="sniper_focus_speed") {float speed{};if(data>>speed&&std::isfinite(speed))legacy_speed=std::clamp(speed,1.f,120.f);}
        else if(key=="skeletons") {int n{};if(data>>n)s.skeletons=n!=0;}
        else if(key=="hide_visible_healthbars") {int n{};if(data>>n)s.hide_visible_healthbars=n!=0;}
        else if(key=="hide_visible_enemy_tags") {int n{};if(data>>n)s.hide_visible_enemy_tags=n!=0;}
        else if(key=="hide_visible_skeletons") {int n{};if(data>>n)s.hide_visible_skeletons=n!=0;}
        else if(key=="hitboxes") {int n{};if(data>>n)s.hitboxes=n!=0;}
        else if(key=="hidden_enemy_tags") {int n{};if(data>>n)s.hidden_enemy_tags=n!=0;}
        else if(key=="hidden_enemy_tags_hold_h") {int n{};if(data>>n)legacy_tags_hold=n!=0;}
        else if(key=="health_visuals_hold") {int n{};if(data>>n){s.health_visuals.hold_only=n!=0;loaded_visuals_hold=true;}}
        else if(key=="health_visuals_key") {int n{};if(data>>n)s.health_visuals.key=n;}
        else {
            float* target=key=="frame_opacity"?&s.frame_opacity:nullptr;
            if(target) {float v{};if(data>>v && std::isfinite(v))*target=v;}
            ImVec4* color=key=="skeleton_visible"?&s.skeleton_visible:key=="skeleton_hidden"?&s.skeleton_hidden:nullptr;
            if(color) {float r{},g{},b{};if(data>>r>>g>>b && std::isfinite(r)&&std::isfinite(g)&&std::isfinite(b)) *color={std::clamp(r,0.f,1.f),std::clamp(g,0.f,1.f),std::clamp(b,0.f,1.f),1};}
        }
    }
    s.frame_opacity=std::clamp(s.frame_opacity,.4f,.9f);
    s.healthbar_box_gap_meters=std::clamp(s.healthbar_box_gap_meters,0.f,2.f);
    if(!loaded_visuals_hold)s.health_visuals.hold_only=legacy_tags_hold;
    sanitize_health_visuals(s.health_visuals);
    area_migration.apply(s);sanitize_camera_settings(s);
    sanitize_parry_settings(s.auto_parry);
    // Seed old installations from their current controls, then apply independent saved values.
    s.vindicta_ult=static_cast<const CameraFocusSettings&>(s);
    // Seed new X2 settings after loading this profile's original X1 values.
    // Older ultimate profiles can have different speeds from the regular profile.
    s.vindicta_ult.focus_speed_x2=s.vindicta_ult.adaptive_speed_x2=0.f;
    if(legacy_speed>0)s.vindicta_ult.focus_speed=legacy_speed;
    if(!loaded_override)s.vindicta_ult_override=legacy_override;
    for(const auto& [key,value]:ult_values){std::istringstream data(value);if(!ult_area_migration.read(key,data))read_camera_setting(s.vindicta_ult,key,data);}
    ult_area_migration.apply(s.vindicta_ult);sanitize_camera_settings(s.vindicta_ult);
    restrict_assassinate_targets(s.vindicta_ult);
    s.vindicta_ult.prediction=false; // Assassinate is hitscan, regardless of primary weapon settings.
    return s;
}
std::string escaped(const std::string& input) {
    std::string result;for(char c:input) {if(c=='"'||c=='\\')result+='\\';if(c=='\n'){result+="\\n";continue;}if(static_cast<unsigned char>(c)>=32)result+=c;}return result;
}
void visibility_report(const Snapshot& s,const std::filesystem::path& path,bool enabled,bool occluded){
    std::ofstream out(path);out<<"{\"enabled\":"<<(enabled?"true":"false")<<",\"map\":\""<<escaped(s.map_name)<<"\",\"level\":\""<<escaped(s.level_name)<<"\",\"map_source\":\""<<escaped(s.map_source)<<"\",\"status\":\""<<escaped(s.visibility_status)<<"\",\"triangles\":"<<(s.visibility?s.visibility->triangle_count():0)<<",\"ray_ms\":"<<s.visibility_us/1000<<",\"camera_valid\":"<<(s.camera_valid?"true":"false")<<",\"camera_position\":["<<s.camera_position.x<<','<<s.camera_position.y<<','<<s.camera_position.z<<"],\"focus_occluded\":"<<(occluded?"true":"false")<<",\"players\":[";
    bool first=true;for(const auto& p:s.players){if(!first)out<<',';first=false;out<<"{\"handle\":"<<p.handle<<",\"visible\":"<<(p.visible?"true":"false")<<",\"bones\":[";for(int i=0;i<3;++i){if(i)out<<',';out<<(p.dot_visible[i]?"true":"false");}out<<"]}";}out<<"]}\n";
}
void report(const Snapshot& s,const std::filesystem::path& path,int bars=-1,bool visible=false,LONG_PTR style=0,HWND overlay_window=nullptr,bool settings_visible=false,double target_hz=0,double render_fps=0,int drawn_hitboxes=0,int hidden_tags=0) {
    DWORD foreground_pid{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    RECT bounds{};if(overlay_window)GetWindowRect(overlay_window,&bounds);
    std::ofstream out(path);out<<"{\n\"pid\":"<<s.pid<<",\"status\":\""<<escaped(s.status)<<"\",\"reader_profile\":\""<<escaped(s.reader_profile)<<"\",\"sample_time\":"<<s.time<<",\"hidden_tags_drawn\":"<<hidden_tags
        <<",\"map\":\""<<escaped(s.map_name)<<"\",\"level\":\""<<escaped(s.level_name)<<"\",\"map_source\":\""<<escaped(s.map_source)<<"\",\"visibility_status\":\""<<escaped(s.visibility_status)<<"\",\"triangles\":"<<(s.visibility?s.visibility->triangle_count():0)
        <<",\"controllers\":"<<s.controllers<<",\"local_team\":"<<unsigned(s.local_team)<<",\"local_alive\":"<<(s.local_alive?"true":"false")<<",\"invalid_handles\":"<<s.invalid_handles<<",\"missing_anchors\":"<<s.missing_anchors
        <<",\"drawn_bars\":"<<bars<<",\"overlay_visible\":"<<(visible?"true":"false")<<",\"overlay_extended_style\":"<<style
        <<",\"drawn_hitboxes\":"<<drawn_hitboxes
        <<",\"foreground_pid\":"<<foreground_pid<<",\"settings_visible\":"<<(settings_visible?"true":"false")
        <<",\"practice\":"<<(s.practice?"true":"false")<<",\"match_mode\":"<<s.match_mode<<",\"game_mode\":"<<s.game_mode<<",\"replay\":"<<(s.replay?"true":"false")<<",\"target_hz\":"<<target_hz<<",\"render_fps\":"<<render_fps<<",\"sample_us\":"<<s.sample_us<<",\"sample_reads\":"<<s.read_calls
        <<",\"hero\":{\"valid\":"<<(s.hero.valid?"true":"false")<<",\"id\":"<<s.hero.id<<",\"name\":\""<<escaped(s.hero.name)<<"\",\"token\":\""<<escaped(s.hero.token)<<"\",\"sniper_present\":"<<(s.hero.sniper_present?"true":"false")<<",\"sniper_valid\":"<<(s.hero.sniper_valid?"true":"false")<<",\"sniper_scoped\":"<<(s.hero.sniper_scoped?"true":"false")<<",\"scope_start\":"<<s.hero.scope_start<<",\"ability_status\":\""<<escaped(s.hero.ability_status)<<"\"}"
        <<",\"projection\":{\"source\":\"final_render_view\",\"valid\":"<<(s.projection.valid?"true":"false")<<",\"horizontal_fov\":"<<s.projection.horizontal_fov<<",\"vertical_fov\":"<<s.projection.vertical_fov<<",\"aspect\":"<<s.projection.aspect<<"}"
        <<",\"overlay_rect\":["<<bounds.left<<','<<bounds.top<<','<<bounds.right<<','<<bounds.bottom<<"],\"skipped_players\":[";
    bool first=true;for(const auto& skip:s.skipped_players) {if(!first)out<<',';first=false;
        out<<"{\"controller\":"<<skip.controller<<",\"pawn\":"<<skip.pawn<<",\"reason\":\""<<escaped(skip.reason)<<"\",\"model\":\""<<escaped(skip.model)<<"\"}";
    }
    out<<"],\"players\":[";
    first=true;for(const auto& p:s.players) {if(!first)out<<',';first=false;
        out<<"{\"handle\":"<<p.handle<<",\"team\":"<<unsigned(p.team)<<",\"health\":"<<p.health<<",\"maximum\":"<<p.maximum<<",\"anchor\":\""<<escaped(p.anchor)
            <<"\",\"model\":\""<<escaped(p.model_name)<<"\",\"bone_rig\":\""<<escaped(p.bone_rig)<<"\",\"anchor_status\":\""<<escaped(p.anchor_status)
            <<"\",\"hero_name\":\""<<escaped(p.hero_name)<<"\",\"feet_valid\":"<<(p.feet_valid?"true":"false")
            <<",\"feet\":["<<p.feet.x<<','<<p.feet.y<<','<<p.feet.z<<"],\"world\":["<<p.head.x<<','<<p.head.y<<','<<p.head.z<<"],\"dots\":[";
        for(size_t i=0;i<3;++i){if(i)out<<',';if(p.dot_valid[i])out<<'['<<p.dots[i].x<<','<<p.dots[i].y<<','<<p.dots[i].z<<']';else out<<"null";}
        size_t visible_segments=0,unknown_segments=0;for(const auto& segment:p.skeleton){if(segment.visible)++visible_segments;if(!segment.visibility_known)++unknown_segments;}
        out<<"],\"body_bound_points\":"<<p.body_point_count<<",\"skeleton_segments\":"<<p.skeleton.size()<<",\"skeleton_visible\":"<<visible_segments<<",\"skeleton_unknown\":"<<unknown_segments
            <<",\"hitbox_set\":"<<p.hitbox_set<<",\"hitbox_status\":\""<<escaped(p.hitbox_status)<<"\",\"hitboxes\":[";
        for(size_t i=0;i<p.hitboxes.size();++i) {
            if(i)out<<',';const auto& box=p.hitboxes[i];
            out<<"{\"shape\":"<<int(box.shape)<<",\"group\":"<<box.group<<",\"a\":["<<box.a.x<<','<<box.a.y<<','<<box.a.z<<"],\"b\":["<<box.b.x<<','<<box.b.y<<','<<box.b.z<<"],\"radius\":"<<box.radius
                <<",\"visible\":"<<(box.visible?"true":"false")<<",\"visibility_known\":"<<(box.visibility_known?"true":"false")<<"}";
        }
        out<<"],\"velocity_valid\":"<<(p.velocity_valid?"true":"false")<<",\"velocity\":["<<p.velocity.x<<','<<p.velocity.y<<','<<p.velocity.z<<"]}";
    }
    out<<"],\"focus_targets\":[";first=true;
    for(const auto& target:s.focus_targets) {
        if(!first)out<<',';first=false;
        out<<"{\"handle\":"<<target.handle<<",\"kind\":\""<<target_kind_name(target.kind)<<"\",\"team\":"<<unsigned(target.team)<<",\"health\":"<<target.health<<",\"maximum\":"<<target.maximum<<",\"visible\":"<<(target.visible?"true":"false")<<",\"dots\":[";
        for(int i=0;i<3;++i){if(i)out<<',';if(target.dot_valid[i])out<<'['<<target.dots[i].x<<','<<target.dots[i].y<<','<<target.dots[i].z<<']';else out<<"null";}
        out<<"],\"hitboxes\":"<<target.hitboxes.size()<<",\"velocity_valid\":"<<(target.velocity_valid?"true":"false")<<",\"velocity\":["<<target.velocity.x<<','<<target.velocity.y<<','<<target.velocity.z<<']'
            <<",\"orb_state_valid\":"<<(target.orb.valid?"true":"false")<<",\"orb_phase\":\""<<orb_phase_name(target.orb.phase(GetTickCount64()))
            <<"\",\"orb_clock\":"<<target.orb.clock<<",\"orb_launch\":"<<target.orb.launch<<",\"orb_attackable\":"<<target.orb.opens
            <<",\"orb_end_attackable\":"<<target.orb.closes<<",\"orb_launch_number\":"<<target.orb.launch_number<<",\"orb_claim_pending\":"<<(target.orb.claim_pending?"true":"false")
            <<",\"orb_shot_bounds\":"<<(target.shot_bounds?"true":"false")<<"}";
    }
    out<<"],\"matrix\":[";for(size_t i=0;i<s.matrix.size();++i){if(i)out<<',';out<<s.matrix[i];}out<<"]}\n";
}
void write_motion(std::ostream& out,const MotionSample& motion) {
    auto vector=[&](Vec3 v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
    out<<"{\"source\":\""<<motion_source_name(motion.source)<<"\",\"measured_ms\":"<<std::setprecision(15)<<motion.measured_ms
        <<",\"position_valid\":"<<(motion.position_valid?"true":"false")<<",\"velocity_valid\":"<<(motion.velocity_valid?"true":"false")<<",\"root\":";
    vector(motion.root);out<<",\"velocity\":";vector(motion.velocity);out<<",\"root_estimate\":";vector(motion.root_velocity);
    out<<",\"engine_candidate_valid\":"<<(motion.engine_candidate_valid?"true":"false")<<",\"engine_candidate\":";vector(motion.engine_velocity);out<<'}';
}
void write_parry_data(std::ostream& out,const ParryReadState& state,const ParryDecision& decision={},const AutoParry* controller=nullptr) {
    auto vector=[&](Vec3 value){out<<'['<<value.x<<','<<value.y<<','<<value.z<<']';};
    out<<"{\"valid\":"<<(state.valid?"true":"false")<<",\"ready\":"<<(state.ready?"true":"false")
        <<",\"active\":"<<(state.active?"true":"false")<<",\"status\":\""<<escaped(state.status)<<"\",\"sampled\":"<<state.sampled
        <<",\"local\":"<<state.local_handle<<",\"clock\":"<<state.game_time<<",\"cooldown\":"<<state.cooldown_remaining
        <<",\"counterspell_owned\":"<<(state.counterspell_owned?"true":"false")<<",\"counterspell_ready\":"<<(state.counterspell_ready?"true":"false")
        <<",\"counterspell_active\":"<<(state.counterspell_active?"true":"false")<<",\"spell_cooldown\":"<<state.spell_cooldown_remaining
        <<",\"replicated_readers\":"<<state.replicated_readers<<",\"unavailable_melee\":"<<state.unavailable_melee
        <<",\"unsupported_projectiles\":"<<state.unsupported_projectiles<<",\"local_shapes\":"<<state.local_hitboxes.size()
        <<",\"melee_definition_valid\":"<<(state.melee_definition_valid?"true":"false")<<",\"melee_definition_status\":\""<<escaped(state.melee_definition_status)<<'"';
    if(controller)out<<",\"controller_status\":\""<<escaped(controller->status)<<"\",\"inputs_sent\":"<<controller->confirmed_inputs;
    out<<",\"decision\":{\"valid\":"<<(decision.valid?"true":"false")<<",\"kind\":\""<<parry_threat_name(decision.kind)
        <<"\",\"source\":"<<decision.source<<",\"key\":"<<decision.key<<",\"impact_ms\":"<<decision.impact_ms<<"},\"melee\":[";
    bool first=true;
    for(const auto& attack:state.melee) {
        if(!first)out<<',';first=false;
        out<<"{\"pawn\":"<<attack.pawn<<",\"ability\":"<<attack.ability<<",\"team\":"<<unsigned(attack.team)
            <<",\"valid\":"<<(attack.valid?"true":"false")<<",\"replicated\":"<<(attack.replicated?"true":"false")<<",\"status\":\""<<escaped(attack.status)
            <<"\",\"state\":"<<attack.state<<",\"type\":"<<attack.attack_type<<",\"start\":"<<attack.state_start<<",\"activated\":"<<attack.activated
            <<",\"trigger\":"<<attack.trigger_time<<",\"range\":"<<attack.range<<",\"half_angle\":"<<attack.half_angle
            <<",\"parryable\":"<<(attack.parryable?"true":"false")<<",\"piercing\":"<<(attack.piercing?"true":"false")<<",\"origin\":";
        vector(attack.origin);out<<",\"direction\":";vector(attack.direction);out<<",\"velocity\":";vector(attack.velocity);out<<'}';
    }
    out<<"],\"projectiles\":[";first=true;
    for(const auto& projectile:state.projectiles) {
        if(!first)out<<',';first=false;
        out<<"{\"handle\":"<<projectile.handle<<",\"owner\":"<<projectile.owner<<",\"name\":\""<<escaped(projectile.name)<<"\",\"radius\":"<<projectile.radius<<",\"position\":";
        vector(projectile.position);out<<",\"velocity\":";vector(projectile.velocity);out<<'}';
    }
    out<<"]}";
}
void report_adaptive(const Snapshot& snapshot,const CameraFocusSettings& settings,const AdaptiveFocus& focus,
    bool sniper,double queue_ms,const std::filesystem::path& path) {
    const auto activation=resolve_focus_activation(settings);
    const auto& solution=focus.solution;const auto* zone=focus.zone();std::ofstream out(path);
    out<<"{\"mode\":\"adaptive\",\"selected\":"<<(adaptive_mode(settings)?"true":"false")<<",\"enabled\":"<<(settings.replay_focus?"true":"false")
        <<",\"status\":\""<<escaped(focus.status)<<"\",\"response_backend\":\"render_projection\",\"calibration_required\":false"
        <<",\"hold_button_held\":"<<(focus_button_held(settings.focus_hold_button)?"true":"false")
        <<",\"active_button\":\""<<focus_button_name(activation.button)<<"\",\"x1_speed\":"<<settings.adaptive.speed<<",\"x2_speed\":"<<settings.adaptive_speed_x2
        <<",\"view_horizontal_fov\":"<<snapshot.projection.horizontal_fov<<",\"view_vertical_fov\":"<<snapshot.projection.vertical_fov
        <<",\"view_input_valid\":"<<(snapshot.view_input.valid?"true":"false")
        <<",\"sliding\":"<<(snapshot.view_input.sliding?"true":"false")<<",\"weapon_zoom\":"<<(snapshot.view_input.weapon_zoom?"true":"false")
        <<",\"fov_sensitivity\":"<<snapshot.view_input.fov_sensitivity<<",\"mouse_sensitivity\":"<<snapshot.view_input.mouse_sensitivity
        <<",\"pixel_error\":["<<solution.pixel_error.x<<','<<solution.pixel_error.y<<"],\"requested_pixels\":["<<solution.requested_pixels.x<<','<<solution.requested_pixels.y
        <<"],\"requested_counts\":["<<solution.requested_x<<','<<solution.requested_y<<']'
        <<",\"region_setting\":\""<<adaptive_region_name(settings.adaptive.region)<<"\",\"selected_region\":\""<<region_name(solution.region)
        <<"\",\"target\":"<<focus.target()<<",\"target_kind\":\""<<target_kind_name(solution.kind)<<"\",\"hitbox_source\":"<<solution.source_id
        <<",\"retained\":"<<(solution.retained?"true":"false")<<",\"occluded\":"<<(solution.occluded?"true":"false")<<",\"inside_region\":"<<(solution.inside?"true":"false")
        <<",\"solution_valid\":"<<(solution.valid?"true":"false")<<",\"permitted_hitboxes\":"<<solution.box_count
        <<",\"acquisition_degrees\":"<<settings.adaptive.radius_degrees<<",\"retention_degrees\":"<<settings.adaptive.radius_degrees*1.3f
        <<",\"area_mode\":\""<<adaptive_area_name(settings.adaptive.area)<<"\",\"zone_status\":\""<<focus.zone_status()
        <<"\",\"zone_preview_target\":"<<focus.preview_target()<<",\"zone_target\":"<<(zone?zone->target:0)
        <<",\"zone_body_height\":"<<(zone?zone->body_height:0)<<",\"zone_padding_percent\":"<<settings.adaptive.padding_percent
        <<",\"zone_padding\":"<<(zone?zone->padding:0)<<",\"zone_retention_padding\":"<<(zone?zone->retention_padding:0)
        <<",\"zone_primitives\":"<<(zone?zone->activation.size():0)<<",\"zone_activation_hit\":"<<(zone&&zone->activation_hit?"true":"false")
        <<",\"zone_retention_hit\":"<<(zone&&zone->retention_hit?"true":"false")
        <<",\"nearest_degrees\":"<<solution.acquisition_angle<<",\"speed\":"<<activation.adaptive_speed<<",\"response_ms\":"<<adaptive_response_ms(activation.adaptive_speed)
        <<",\"prediction_enabled\":"<<(settings.adaptive.prediction?"true":"false")<<",\"prediction_active\":"<<(solution.prediction_active?"true":"false")
        <<",\"prediction_status\":\""<<solution.prediction_status<<"\",\"motion_source\":\""<<motion_source_name(solution.motion_source)
        <<"\",\"target_age_ms\":"<<solution.data_age_ms<<",\"projectile_flight_ms\":"<<solution.flight_time*1000<<",\"host_queue_ms\":"<<queue_ms
        <<",\"assassinate_hitscan\":"<<(sniper?"true":"false")<<",\"weapon_valid\":"<<(snapshot.weapon.valid?"true":"false")<<",\"weapon_speed\":"<<snapshot.weapon.speed
        <<",\"moves\":"<<focus.moves<<",\"input_failures\":"<<focus.input_failures<<",\"current_point\":["<<solution.current_point.x<<','<<solution.current_point.y<<','<<solution.current_point.z
        <<"],\"predicted_point\":["<<solution.aim_point.x<<','<<solution.aim_point.y<<','<<solution.aim_point.z<<"],\"zone_exclusions\":[";
    bool first=true;for(const auto& issue:focus.zone_issues()) {
        if(!first)out<<',';first=false;
        out<<"{\"target\":"<<issue.target<<",\"kind\":\""<<target_kind_name(issue.kind)<<"\",\"reason\":\""<<escaped(issue.reason)<<"\"}";
    }
    out<<"],\"local_motion\":";
    write_motion(out,snapshot.local_motion);out<<"}\n";
}
void report_x2_lock(const Snapshot& snapshot,const CameraFocusSettings& settings,const X2TargetLock& lock,const FocusActivation& activation,const std::filesystem::path& path) {
    const auto& point=lock.solution;std::ofstream out(path);
    out<<"{\"enabled\":"<<(settings.x2_target_lock?"true":"false")<<",\"active\":"<<(lock.active()?"true":"false")
        <<",\"release_required\":"<<(lock.spent()?"true":"false")<<",\"status\":\""<<escaped(lock.status)<<"\",\"target\":"<<lock.target()
        <<",\"target_kind\":\""<<target_kind_name(point.kind)<<"\",\"fov_degrees\":360,\"speed\":"<<(adaptive_mode(settings)?activation.adaptive_speed:activation.speed)
        <<",\"solution_valid\":"<<(point.valid?"true":"false")<<",\"inside_region\":"<<(point.inside?"true":"false")
        <<",\"angular_distance\":"<<point.acquisition_angle<<",\"prediction\":\""<<point.prediction_status<<"\",\"flight_ms\":"<<point.flight_time*1000
        <<",\"data_age_ms\":"<<point.data_age_ms<<",\"requested_counts\":["<<point.requested_x<<','<<point.requested_y<<']'
        <<",\"moves\":"<<lock.moves<<",\"input_failures\":"<<lock.input_failures<<",\"pid\":"<<snapshot.pid<<"}\n";
}
void report_adaptive_geometry(const Snapshot& snapshot,const std::filesystem::path& path) {
    std::ofstream out(path);out<<"{\"input_disabled\":true,\"local_motion\":";write_motion(out,snapshot.local_motion);
    out<<",\"targets\":[";bool first=true;
    for(const auto& target:snapshot.players) {
        if(!first)out<<',';first=false;std::array<size_t,5> counts{};
        for(const auto& box:target.hitboxes)++counts[size_t(box.region)];
        out<<"{\"handle\":"<<target.handle<<",\"model\":\""<<escaped(target.model_name)<<"\",\"rig\":\""<<escaped(target.bone_rig)<<"\",\"active_hitboxes\":"<<target.hitboxes.size()
            <<",\"unknown\":"<<counts[0]<<",\"head_neck\":"<<counts[1]<<",\"torso\":"<<counts[2]<<",\"pelvis\":"<<counts[3]<<",\"limbs\":"<<counts[4]<<",\"motion\":";
        write_motion(out,target.motion);out<<",\"regions\":[";
        for(int i=0;i<2;++i) {
            if(i)out<<',';AdaptiveSettings settings;settings.region=selectable_adaptive_region(i);
            auto point=adaptive_closest(snapshot,target,settings,1,{},true);
            out<<"{\"name\":\""<<adaptive_region_name(settings.region)<<"\",\"geometry\":"<<(point.geometry?"true":"false")<<",\"visible_point\":"<<(point.valid?"true":"false")
                <<",\"inside\":"<<(point.inside?"true":"false")<<",\"nearest_degrees\":"<<point.distance<<",\"source_id\":"<<point.source_id<<'}';
        }
        out<<"]}";
    }
    out<<"]}\n";
}
void report_hitbox_focus(const Snapshot& snapshot,float width,float height,const std::filesystem::path& path) {
    std::ofstream out(path);ScreenPoint center{width*.5f,height*.5f};
    out<<"{\"pid\":"<<snapshot.pid<<",\"width\":"<<width<<",\"height\":"<<height<<",\"mode\":\"hitboxes_v2\",\"targets\":[";
    bool first=true;size_t calls=0;double microseconds=0;
    auto target_report=[&](const FocusTarget& target) {
        if(!first)out<<',';first=false;ClosestHitboxPoint nearest;
        double start=clock_ms();bool valid=closest_focus_hitboxes(snapshot.matrix,target.hitboxes,width,height,center,{},nearest);
        microseconds+=(clock_ms()-start)*1000;++calls;
        int projected=0,inside=0;float maximum_error=0;
        for(const auto& box:target.hitboxes) {
            ScreenPoint sample;if(!focus_project(snapshot.matrix,hitbox_center(box),width,height,sample))continue;
            ++projected;ClosestHitboxPoint point;
            if(closest_focus_hitboxes(snapshot.matrix,target.hitboxes,width,height,sample,{},point)&&point.inside){++inside;maximum_error=std::max(maximum_error,std::hypot(point.screen.x-sample.x,point.screen.y-sample.y));}
        }
        out<<"{\"handle\":"<<target.handle<<",\"kind\":\""<<target_kind_name(target.kind)<<"\",\"hitboxes\":"<<target.hitboxes.size()<<",\"valid\":"<<(valid?"true":"false")
            <<",\"inside\":"<<(valid&&nearest.inside?"true":"false")<<",\"hitbox_index\":"<<nearest.index<<",\"distance_pixels\":"<<nearest.distance
            <<",\"world\":["<<nearest.world.x<<','<<nearest.world.y<<','<<nearest.world.z<<"],\"projected_centers\":"<<projected<<",\"intersected_centers\":"<<inside<<",\"maximum_projection_error\":"<<maximum_error<<"}";
    };
    for(const auto& target:snapshot.players)target_report(target);
    for(const auto& target:snapshot.focus_targets)if(target.kind==TargetKind::Minion)target_report(target);
    out<<"],\"average_geometry_us\":"<<(calls?microseconds/calls:0)<<"}\n";
}
void write_crosshair_hit(std::ostream& out,const CrosshairHit& hit) {
    out<<"{\"status\":\""<<(hit.state==CrosshairState::OrbUnavailable?orb_phase_name(hit.orb_phase):crosshair_state_name(hit.state))<<"\",\"eligible\":"<<((hit.state==CrosshairState::Enemy||hit.state==CrosshairState::SoulOrb)?"true":"false")
        <<",\"target\":"<<hit.target<<",\"hitbox\":"<<hit.hitbox<<",\"distance\":"<<hit.distance
        <<",\"kind\":\""<<target_kind_name(hit.kind)<<"\",\"valid_until\":"<<hit.valid_until
        <<",\"origin\":["<<hit.origin.x<<','<<hit.origin.y<<','<<hit.origin.z<<"],\"point\":["<<hit.point.x<<','<<hit.point.y<<','<<hit.point.z<<"]}";
}
void report_ray_click(const Snapshot& snapshot,float width,float height,const std::filesystem::path& path) {
    // Diagnostic only: this path never starts the MAKCU backend or sends input.
    std::ofstream out(path);
    out<<"{\"input_disabled\":true,\"pid\":"<<snapshot.pid<<",\"local_team\":"<<unsigned(snapshot.local_team)
        <<",\"local_alive\":"<<(snapshot.local_alive?"true":"false")<<",\"width\":"<<width<<",\"height\":"<<height<<",\"crosshair\":";
    write_crosshair_hit(out,crosshair_hit(snapshot,width,height,{width*.5f,height*.5f}));
    out<<",\"orb_crosshair\":";write_crosshair_hit(out,crosshair_hit(snapshot,width,height,{width*.5f,height*.5f},true));
    out<<",\"projected_hitbox_queries\":[";bool first=true;
    for(const auto& player:snapshot.players) {
        unsigned projected{},same_player{},eligible{},blocked{};
        for(const auto& box:player.hitboxes) {
            ScreenPoint pixel{};if(!project(snapshot.matrix,hitbox_center(box),width,height,pixel))continue;
            ++projected;auto hit=crosshair_hit(snapshot,width,height,pixel);
            if(hit.target==player.handle)++same_player;
            if(hit.state==CrosshairState::Enemy)++eligible;
            if(hit.state==CrosshairState::Blocked)++blocked;
        }
        if(!first)out<<',';first=false;
        out<<"{\"handle\":"<<player.handle<<",\"team\":"<<unsigned(player.team)<<",\"hitboxes\":"<<player.hitboxes.size()
            <<",\"projected_centers\":"<<projected<<",\"nearest_is_this_player\":"<<same_player<<",\"eligible_enemy_rays\":"<<eligible<<",\"blocked_rays\":"<<blocked<<"}";
    }
    out<<"],\"orb_queries\":[";first=true;
    for(const auto& orb:snapshot.focus_targets)if(orb.kind==TargetKind::SoulOrb) {
        if(!first)out<<',';first=false;ScreenPoint pixel{};
        const bool projected=orb.dot_valid[1]&&project(snapshot.matrix,orb.dots[1],width,height,pixel);
        out<<"{\"handle\":"<<orb.handle<<",\"state_valid\":"<<(orb.orb.valid?"true":"false")<<",\"phase\":\""<<orb_phase_name(orb.orb.phase(GetTickCount64()))
            <<"\",\"shot_bounds\":"<<(orb.shot_bounds?"true":"false")<<",\"projected\":"<<(projected?"true":"false")<<",\"hit\":";
        if(projected)write_crosshair_hit(out,crosshair_hit(snapshot,width,height,pixel,true));else out<<"null";out<<'}';
    }
    out<<"]}\n";
}
int self_test() {
    int passed{},failed{};auto check=[&](bool value){value?++passed:++failed;};
    check(makcu_move_command(10,-3)=="km.move(10,-3)\r\n");
    check(makcu_move_command(-32768,32767)=="km.move(-32768,32767)\r\n");
    check(makcu_move_command(0,0).empty());check(makcu_move_command(32768,0).empty());
    check(makcu_move_command(0,-32769).empty());
    check(makcu_identity("km.MAKCU\r\n>>> "));
    check(makcu_identity("km.version()\r\nkm.MAKCU_L_V3.2\r\n>>> "));
    check(!makcu_identity("km.version()\r\n>>> "));
    check(!makcu_identity("noise km.MAKCU\r\n"));check(!makcu_identity("km.MAKCUbad\r\n"));
    check(makcu_fresh(1020,1000));check(!makcu_fresh(1021,1000));
    check(!makcu_fresh(999,1000));check(!makcu_fresh(1000,0));
    Matrix m{};m[0]=m[5]=m[10]=m[15]=1;ScreenPoint p;
    check(project(m,{0,0,0},1920,1080,p)&&p.x==960&&p.y==540);
    check(project(m,{-1,1,0},1920,1080,p)&&p.x==0&&p.y==0);
    check(!project(m,{2,0,0},1920,1080,p));
    {
    Matrix perspective{};perspective[0]=1;perspective[5]=16.f/9;perspective[10]=perspective[14]=1;
    auto projection=projection_info(perspective);
    check(projection.valid&&std::abs(projection.horizontal_fov-90)<.001f&&std::abs(projection.aspect-16.f/9)<.001f);
    auto zoom_projection=perspective;zoom_projection[0]*=2;zoom_projection[5]*=2;
    auto zoom_info=projection_info(zoom_projection);check(zoom_info.valid&&zoom_info.horizontal_fov<projection.horizontal_fov&&zoom_info.vertical_fov<projection.vertical_fov);
    auto overridden=perspective;overridden[5]=2.3f;check(std::abs(projection_info(overridden).aspect-2.3f)<.001f);
    ScreenPoint final_point{},legacy_point{};
    check(project(perspective,{.1f,.2f,1},1920,1080,final_point)&&project(overridden,{.1f,.2f,1},1920,1080,legacy_point)
        &&final_point.x==legacy_point.x&&final_point.y>legacy_point.y);
    auto scaled=perspective;for(auto& value:scaled)value*=3;check(std::abs(projection_info(scaled).horizontal_fov-90)<.001f);
    auto torn=perspective;torn[4]=.2f;check(!projection_info(torn).valid);torn=perspective;torn[3]=NAN;check(!projection_info(torn).valid);
    check(!projection_info(Matrix{}).valid);
    HeroProfile detected;detected.valid=true;detected.id=3;detected.token="hero_hornet";
    check(vindicta(detected)&&hero_token("#hero_hornet_sort")=="hero_hornet"&&hero_token("#hero_orion_search")=="hero_orion");
    check(hero_token("#wrong").empty()&&hero_token("hero_<bad>").empty());
    detected.sniper_present=detected.sniper_valid=detected.sniper_scoped=true;
    check(hero_focus_speed(detected,true,16,40)==40&&hero_focus_speed(detected,false,16,40)==16);
    detected.sniper_scoped=false;check(hero_focus_speed(detected,true,16,40)==16);
    detected.sniper_scoped=true;detected.sniper_valid=false;check(hero_focus_speed(detected,true,16,40)==16);
    detected.sniper_valid=true;detected.id=4;check(hero_focus_speed(detected,true,16,40)==16);
    detected.id=3;detected.valid=false;check(hero_focus_speed(detected,true,16,40)==16);
    check(!sniper_scope(0)&&!sniper_scope(NAN)&&!sniper_scope(-1)&&sniper_scope(10));
    check(hero_focus_speed({},true,999,40)==120&&hero_focus_speed({},true,NAN,40)==12);
    }
    m[15]=-1;check(!project(m,{0,0,0},1920,1080,p));
    m[15]=0.0001f;check(!project(m,{0,0,0},1920,1080,p));
    m[15]=1;check(!project(m,{NAN,0,0},1920,1080,p));check(!project(m,{0,0,0},0,1080,p));
    check(health_fraction(50,100)==0.5f);check(health_fraction(-1,100)==0);check(health_fraction(200,100)==1);check(health_fraction(100,0)==0);
    check(valid_handle(0x10001,0x10001,1));check(!valid_handle(0x10001,0x20001,1));check(!valid_handle(UINT32_MAX,UINT32_MAX,0x7fff));
    check(fresh(1250,1000));check(!fresh(1251,1000));check(!fresh(1000,0));check(!fresh(900,1000));
    check(select_anchor(93,7)==93);check(select_anchor(-1,7)==7);check(select_anchor(-1,-1)==-1);
    check(render_active(true,false,true,false,false));
    check(render_active(true,false,false,true,true));
    check(!render_active(true,false,false,true,false));
    check(!render_active(true,true,false,true,true));
    check(!render_active(false,false,false,true,true));
    check(!render_active(true,false,false,false,true));
    check(valid_refresh(144)==144);check(valid_refresh(240)==240);
    check(std::abs(valid_refresh(60000.0/1001)-59.94005994)<0.0001);
    check(valid_refresh(0)==60);check(valid_refresh(NAN)==60);
    GameFrames events;events.target(123);LARGE_INTEGER stamp,freq;QueryPerformanceCounter(&stamp);QueryPerformanceFrequency(&freq);
    events.observe(321,GameFrames::provider(),42,stamp.QuadPart);check(events.count()==0&&!events.fresh());
    events.observe(123,GameFrames::provider(),43,stamp.QuadPart);check(events.count()==0);
    GUID other{};events.observe(123,other,42,stamp.QuadPart);check(events.count()==0);
    events.observe(123,GameFrames::provider(),42,stamp.QuadPart-freq.QuadPart);check(events.count()==1&&!events.fresh());
    QueryPerformanceCounter(&stamp);events.observe(123,GameFrames::provider(),42,stamp.QuadPart);check(events.count()==2&&events.fresh());
    events.target(456);check(events.count()==0&&!events.fresh());
    check(include_player(false,2,2));check(!include_player(true,2,2));check(include_player(true,2,3));
    check(include_player(true,0,2));check(include_player(true,2,0));check(!include_player(true,3,3));
    check(camera_allowed(true,true,true,false,true,1020,1000));
    check(!camera_allowed(true,false,true,false,true,1020,1000));
    check(!camera_allowed(true,true,false,false,true,1020,1000));
    check(!camera_allowed(true,true,true,true,true,1020,1000));
    check(!camera_allowed(true,true,true,false,false,1020,1000));
    check(!camera_allowed(true,true,true,false,true,1051,1000));
    check(!camera_allowed(true,true,true,false,true,1000,0));
    check(!camera_allowed(false,true,true,false,true,1020,1000));
    float residual{};long total{};for(int i=0;i<100;++i)total+=ReplayCamera::motion(3,.004f,residual);check(total>=4);
    check(ReplayCamera::motion(NAN,.004f,residual)==0&&residual==0);
    check(ReplayCamera::motion(10000,.025f,residual)<=60);
    check(ReplayCamera::motion(-10000,.025f,residual)>=-60);
    float slow_fraction{},fast_fraction{};
    check(ReplayCamera::motion(100,.01f,fast_fraction,12)>ReplayCamera::motion(100,.01f,slow_fraction,4));
    check(ReplayCamera::motion(100,.01f,residual,NAN)==0);
    float normal_fraction{},rapid_fraction{};
    check(ReplayCamera::motion(100,.004f,rapid_fraction,60)>ReplayCamera::motion(100,.004f,normal_fraction,30));
    rapid_fraction=0;check(ReplayCamera::motion(100,.1f,rapid_fraction,120)<=85);
    rapid_fraction=0;check(ReplayCamera::motion(-100,.1f,rapid_fraction,120)>=-85);
    rapid_fraction=0;check(ReplayCamera::motion(100,0,rapid_fraction,120)==0);
check(camera_mode_allowed(true,false,false));
check(camera_mode_allowed(false,true,true));
check(camera_mode_allowed(false,true,false));
check(camera_mode_allowed(false,false,true));
    std::array<Vec3,3> line{Vec3{0,0,100},Vec3{10,0,50},Vec3{0,0,0}};
    check(focus_line(line,-5).z==100);check(focus_line(line,5).z==0);
    check(focus_line(line,.5f).x==5&&focus_line(line,.5f).z==75);
    check(focus_line(line,1.5f).z==25);check(focus_line(line,NAN).z==50);
    check(slide_focus(1,-1000,50)==0);check(slide_focus(1,1000,50)==2);
    Matrix identity{};identity[0]=identity[5]=identity[10]=identity[15]=1;
    std::array<Vec3,3> body_line{Vec3{.2f,.5f,0},Vec3{.2f,0,0},Vec3{.2f,-.5f,0}};ClosestLinePoint nearest{};
    check(closest_focus_line(identity,body_line,1000,1000,{500,375},nearest)&&std::abs(nearest.position-.5f)<.0001f&&std::abs(nearest.distance-100)<.001f);
    check(closest_focus_line(identity,body_line,1000,1000,{600,350},nearest)&&nearest.distance<.001f&&std::abs(nearest.world.y-.3f)<.0001f);
    check(closest_focus_line(identity,body_line,1000,1000,{500,625},nearest)&&std::abs(nearest.position-1.5f)<.0001f);
    check(closest_focus_line(identity,body_line,1000,1000,{600,50},nearest)&&nearest.position==0);
    check(closest_focus_line(identity,body_line,1000,1000,{600,950},nearest)&&nearest.position==2);
    check(closest_focus_line(identity,body_line,1000,1000,{600,500},nearest)&&nearest.position==1);
    auto collapsed=body_line;collapsed.fill({.1f,0,0});check(closest_focus_line(identity,collapsed,1000,1000,{500,500},nearest)&&std::abs(nearest.distance-50)<.001f);
    check(!closest_focus_line(identity,body_line,0,1000,{500,500},nearest));check(!closest_focus_line(identity,body_line,1000,1000,{NAN,500},nearest));
    auto invalid_line=body_line;invalid_line[1].z=NAN;check(!closest_focus_line(identity,invalid_line,1000,1000,{500,500},nearest));
    Matrix depth_matrix=identity;depth_matrix[14]=1;depth_matrix[15]=0;
    std::array<Vec3,3> depth_line{Vec3{.2f,.5f,1},Vec3{.2f,-.5f,3},Vec3{.2f,-1,4}};
    check(closest_focus_line(depth_matrix,depth_line,1000,1000,{566.6667f,416.6667f},nearest)&&std::abs(nearest.position-.25f)<.0001f);
    ScreenPoint reprojected{};check(project(depth_matrix,nearest.world,1000,1000,reprojected)&&std::hypot(reprojected.x-nearest.screen.x,reprojected.y-nearest.screen.y)<.001f);
    depth_line[0].z=-1;check(!closest_focus_line(depth_matrix,depth_line,1000,1000,{500,500},nearest));
    check(skeleton_bone("arm_upper_L")&&skeleton_bone("pelvis")&&!skeleton_bone("finger_index_L")&&!skeleton_bone("spine_2_TWIST"));
    auto links=skeleton_edges({"pelvis","HLPR","head","weapon_hand_R"},{-1,0,1,2});
    check(links.empty());
    auto anatomy=skeleton_edges({"pelvis","spine_2","neck","head","head_end","clavicle_L","arm_upper_L","arm_lower_L","hand_L","clavicle_R","arm_upper_R","leg_upper_L","spine_3"},{-1,0,12,2,3,1,5,6,7,2,9,0,1});
    check(anatomy==std::vector<std::pair<int,int>>{{0,1},{5,6},{6,7},{7,8},{9,10},{0,11},{1,12},{12,5},{12,9}});
    check(skeleton_edges({"head","pelvis"},{-1}).empty());check(skeleton_edges({"head","helper"},{1,1}).empty());
    check(extra_target_kind(".?AVCItemXP@@")==TargetKind::SoulOrb);check(extra_target_kind(".?AVC_NPC_Trooper@@")==TargetKind::Minion);
    check(extra_target_kind(".?AVC_NPC_TrooperNeutral@@")==TargetKind::Minion);check(extra_target_kind(".?AVC_NPC_TrooperBoss@@")==TargetKind::None);
    check(extra_target_kind(".?AVC_Citadel_Pickup_AssignedGold@@")==TargetKind::None);
    Snapshot choices;choices.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};choices.local_team=2;choices.players.resize(1);
    choices.players[0].handle=10;choices.players[0].team=3;choices.players[0].dot_valid.fill(true);choices.players[0].dots={Vec3{.05f,.5f,1},Vec3{.05f,0,1},Vec3{.05f,-.5f,1}};
    FocusTarget minion;minion.handle=20;minion.kind=TargetKind::Minion;minion.team=3;minion.dot_valid.fill(true);minion.dots.fill({.01f,0,1});
    FocusTarget orb=minion;orb.handle=30;orb.kind=TargetKind::SoulOrb;orb.team=2;orb.dots.fill({0,0,1});choices.focus_targets={minion,orb};
    auto pick=[&](FocusOptions options,bool enemies=false,bool free_move=false,bool visible=false){return select_focus_target(choices,options,enemies,1,free_move,visible,1000,1000,{500,500});};
    check(pick({true,false,false})==&choices.players[0]);check(pick({true,true,false})==&choices.focus_targets[0]);
    check(pick({true,true,true},true)==&choices.focus_targets[1]);check(!pick({false,false,false}));check(!pick({true,true,true},false,false,true));
    choices.focus_targets[0].team=2;check(!pick({false,true,false},true));choices.replay=true;check(pick({false,true,false},true)==&choices.focus_targets[0]);choices.replay=false;
    choices.players[0].dots={Vec3{0,1,1},Vec3{0,.6f,1},Vec3{0,-.5f,1}};
    check(pick({true,true,false},false,true)==&choices.players[0]);
    Vec3 focus_world{};float focus_position{};
    check(focus_point(identity,orb,1,true,1000,1000,{500,500},{},focus_world,focus_position)&&focus_world.x==0&&focus_world.y==0);
    check(!focus_point(identity,orb,3,true,1000,1000,{500,500},{},focus_world,focus_position));
    check(scene_point({10,20,30,2,0,0,0,1},{1,2,3},focus_world)&&focus_world.x==12&&focus_world.y==24&&focus_world.z==36);
    check(scene_point({0,0,0,2,0,0,.70710678f,.70710678f},{1,0,0},focus_world)&&std::abs(focus_world.x)<.0001f&&std::abs(focus_world.y-2)<.0001f);
    check(!scene_point({0,0,0,0,0,0,0,1},{1,0,0},focus_world));check(!scene_point({0,0,0,1,0,0,0,0},{1,0,0},focus_world));
    float flight{};check(intercept({0,0,0},{100,0,0},{0,0,0},1000,flight)&&std::abs(flight-.1f)<.00001f);
    check(intercept({0,0,0},{100,0,0},{100,0,0},1000,flight)&&std::abs(flight-1.f/9)<.00001f);
    check(!intercept({0,0,0},{100,0,0},{2000,0,0},1000,flight));
    check(!intercept({0,0,0},{100,0,0},{0,0,0},NAN,flight));
    check(!intercept({0,0,0},{2000,0,0},{0,0,0},1000,flight));
    VelocityEstimate estimate;estimate.update({0,0,0},1000);check(!estimate.valid);
    estimate.update({3,0,0},1030);check(estimate.valid&&std::abs(estimate.velocity.x-100)<.001f);
    estimate.update({1000,0,0},1060);check(!estimate.valid);
    estimate.update({1001,0,0},1500);check(!estimate.valid);
    Matrix perspective{1,0,0,-10,0,1,0,-20,0,0,1,0,0,0,1,-30};Vec3 eye{};
    check(camera_origin(perspective,eye)&&eye.x==10&&eye.y==20&&eye.z==30);
    Matrix zero{};check(!camera_origin(zero,eye));
    auto area_settings_path=executable_directory()/L"focus-settings-test.ini";Settings restored;
    Settings hero_settings;hero_settings.focus_speed=17;hero_settings.vindicta_ult.focus_speed=42;hero_settings.vindicta_ult_override=false;
    save(hero_settings,area_settings_path);restored=load(area_settings_path);
    check(restored.focus_speed==17&&restored.vindicta_ult.focus_speed==42&&!restored.vindicta_ult_override);
    {std::ofstream legacy(area_settings_path);legacy<<"focus_speed=19\n";}
    restored=load(area_settings_path);check(restored.vindicta_ult.focus_speed==19&&restored.vindicta_ult_override);
    {std::ofstream bad(area_settings_path);bad<<"sniper_focus_speed=999\n";}
    check(load(area_settings_path).vindicta_ult.focus_speed==120);
    check(valid_weapon_numbers(25984.3f,60,0,0));
    check(!valid_weapon_numbers(NAN,60,0,0));check(!valid_weapon_numbers(10000,0,0,.1f));
    check(!valid_weapon_numbers(10000,-100,0,0));check(!valid_weapon_numbers(10000,0,3,0));
    check(valid_modifier_cache(2,5,5,5,3,10,20,0,2,60,1664691683));
    check(!valid_modifier_cache(3,5,5,5,3,10,20,0,2,60,1664691683));
    check(!valid_modifier_cache(2,4,5,5,3,10,20,0,2,60,1664691683));
    check(!valid_modifier_cache(2,5,5,6,3,10,20,0,2,60,1664691683));
    check(!valid_modifier_cache(2,5,5,5,4,10,20,0,2,60,1664691683));
    check(valid_modifier_cache(2,5,5,5,4,20,20,0,2,60,1664691683));
    check(!valid_modifier_cache(2,5,5,5,3,10,20,1,2,60,1664691683));
    check(!valid_modifier_cache(2,5,5,5,3,10,20,0,2,60,123));
    check(!valid_modifier_cache(2,5,5,5,1,10,20,0,2,60,1664691683));
    check(!valid_modifier_cache(2,5,5,5,3,10,20,0,2,NAN,1664691683));
    auto mesh=std::make_shared<VisibilityMesh>();
    check(mesh->assign({{{5,-10,-10},{5,10,-10},{5,10,10}},{{5,-10,-10},{5,10,10},{5,-10,10}}}));
    check(!mesh->clear({0,0,0},{10,0,0}));check(!mesh->clear({10,0,0},{0,0,0}));
    check(mesh->clear({0,0,0},{4,0,0}));check(mesh->clear({0,20,0},{10,20,0}));
    check(mesh->clear({0,0,0},{0,0,0}));check(mesh->clear({5,0,0},{10,0,0}));
    check(!mesh->clear({4.99f,0,0},{10,0,0}));check(!mesh->clear({NAN,0,0},{10,0,0}));
    check(visibility_map_key("maps/hero_testing.vpk")=="hero_testing");check(visibility_map_key("dl_midtown")=="dl_midtown");check(visibility_map_key("..\\..\\evil") == "evil");check(visibility_map_key("bad:name").empty());
    check(visibility_world_map_key("maps/dl_midtown/world")=="dl_midtown");
    check(visibility_world_map_key("maps\\DL_HIDEOUT\\world.vwrld_c")=="dl_hideout");
    check(visibility_world_map_key("maps/hero_testing/world.vwrld")=="hero_testing");
    check(visibility_world_map_key("maps/future_arena/world")=="future_arena");
    check(visibility_world_map_key("maps/start/world").empty());
    check(visibility_world_map_key("maps/scenes/hero_container/world").empty());
    check(visibility_world_map_key("maps/ui/hero_container/world").empty());
    check(visibility_world_map_key("maps/dl_midtown/entities/prop.vmdl").empty());
    check(visibility_world_map_key("maps/../world").empty());
    check(visibility_world_map_key("maps/dl_midtown").empty());
    check(valid_world_vector(26,32,0x12345000));check(valid_world_vector(0,0,0));
    check(!valid_world_vector(-1,32,0x12345000));check(!valid_world_vector(257,512,0x12345000));
    check(!valid_world_vector(26,16,0x12345000));check(!valid_world_vector(26,1024,0x12345000));
    check(!valid_world_vector(1,1,0));
    std::string arena;check(add_visibility_world(arena,visibility_world_map_key("maps/start/world"))&&arena.empty());
    check(add_visibility_world(arena,visibility_world_map_key("maps/dl_midtown/world"))&&arena=="dl_midtown");
    check(add_visibility_world(arena,visibility_world_map_key("maps/scenes/common/world"))&&arena=="dl_midtown");
    check(add_visibility_world(arena,"dl_midtown"));check(!add_visibility_world(arena,"dl_hideout"));
    check(!visibility_allowed(true,{},Vec3{},true,{10,0,0}));check(visibility_allowed(false,{},Vec3{},false,{10,0,0}));
    check(!visibility_allowed(true,mesh,Vec3{},false,{4,0,0}));check(visibility_allowed(true,mesh,Vec3{},true,{4,0,0}));
    std::vector<Triangle> walls;for(int i=1;i<=128;++i){float x=float(i)*10;walls.push_back({{x,-10,-10},{x,10,-10},{x,10,10}});walls.push_back({{x,-10,-10},{x,10,10},{x,-10,10}});}
    check(mesh->assign(std::move(walls))&&mesh->triangle_count()==256);
    for(int i=0;i<128;++i){float x=float(i)*10+.1f;check(!mesh->clear({x,0,0},{x+10,0,0}));check(mesh->clear({x,20,0},{x+10,20,0}));}
    check(!mesh->assign({{{NAN,0,0},{0,1,0},{0,0,1}}}));check(!mesh->clear({0,0,0},{1,0,0}));
    check(!mesh->assign({{{0,0,0},{0,0,0},{0,0,0}}}));check(!mesh->assign({}));
    auto invalid_mesh=executable_directory()/L"invalid-test.tri";{std::ofstream bad(invalid_mesh,std::ios::binary);bad<<"bad";}
    check(!mesh->load(invalid_mesh));std::filesystem::remove(invalid_mesh);
    std::ofstream out(executable_directory()/L"self-test.json");out<<"{\"passed\":"<<passed<<",\"failed\":"<<failed<<"}\n";
    return failed?1:0;
}
struct Application;
int visibility_test(){
    int passed{},failed{};std::ofstream out(executable_directory()/L"visibility-test.json");out<<"{\"maps\":[";bool first=true;
    for(const auto& file:std::filesystem::directory_iterator(executable_directory()/L"maps")){
        if(file.path().extension()!=L".tri")continue;
        auto begin=clock_ms();VisibilityMesh mesh;bool loaded=mesh.load(file.path());double load_ms=clock_ms()-begin;if(!loaded){++failed;continue;}
        std::ifstream in(file.path(),std::ios::binary);Triangle t{};int tested{};
        while(tested<64&&in.read(reinterpret_cast<char*>(&t),sizeof(t))){
            auto normal=cross(sub(t.b,t.a),sub(t.c,t.a));float length=std::sqrt(dot(normal,normal));if(length<.01f)continue;
            auto center=mul(add(add(t.a,t.b),t.c),1.f/3),offset=mul(normal,4.f/length);
            if(!mesh.clear(sub(center,offset),add(center,offset)))++passed;else ++failed;
            if(!mesh.clear(add(center,offset),sub(center,offset)))++passed;else ++failed;++tested;
        }
        begin=clock_ms();int clear_count{};for(int i=0;i<2000;++i){float angle=float(i)*.174532925f;Vec3 eye{std::sin(angle)*500,std::cos(angle)*500,120};Vec3 target{std::sin(angle*3)*2500,std::cos(angle*3)*2500,70};if(mesh.clear(eye,target))++clear_count;}
        auto ray_us=(clock_ms()-begin)*1000/2000;
        if(!first)out<<',';first=false;out<<"{\"map\":\""<<escaped(file.path().stem().string())<<"\",\"triangles\":"<<mesh.triangle_count()<<",\"load_ms\":"<<load_ms<<",\"ray_us\":"<<ray_us<<",\"clear_rays\":"<<clear_count<<",\"known_wall_tests\":"<<tested*2<<"}";
    }
    out<<"],\"passed\":"<<passed<<",\"failed\":"<<failed<<"}\n";return failed||!passed?1:0;
}
struct Surface {
    Application* app{}; HWND hwnd{};ImGuiContext* context{};
    solace::platform::d3d11_renderer renderer;
    double submit_ms{},present_ms{};
    bool last_presented{};uint64_t busy_presents{};
    bool overlay{}, resized{},dragging{},panel_shader{},glass_shader{};UINT width{},height{};POINT drag_origin{},drag_window{};
    void shutdown() {
        if(!overlay&&context){solace::icons::shutdown_textures();solace::glass::cursor_shutdown();solace::rounded_panel::shutdown();solace::snapshot::shutdown();solace::fonts.reset();solace::ui_runtime::clear_animation_states();}
        if(context) {ImGui::SetCurrentContext(context);ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);context=nullptr;}
        if(hwnd){DestroyWindow(hwnd);hwnd=nullptr;}
    }
    ~Surface() {shutdown();}
};
struct Application {
    DataUpdate data_update;
    SessionMonitor session_monitor;
    std::filesystem::path reports_directory{config_path().parent_path()/L"reports"};
    std::atomic<unsigned> data_revision{};
    MakcuInput makcu;
    uint64_t input_backend_generation{};
    ReplayCamera camera;
    AdaptiveFocus adaptive;
    X2TargetLock target_lock;
    target_zone::ZoneOutlineCache zone_outline;
    RayClick ray_click;
    AutoParry auto_parry;
    Surface settings_window,bar_window;
    Settings settings=load();
    HealthKeyCapture health_key_capture;
    CameraFocusSettings scoped_controls;
    bool quit{},show_settings{},diagnostics{},ult_scope_active{},ult_profile_active{},click_ultimate{};
    solace::shell_page menu_page{solace::shell_page::camera};
    int page{-1},bars{},hidden_tags{};float page_time{1.f};
    HWND game_hwnd{};
    double target_hz{60},render_fps{};std::string display,frame_status="Refresh pacing";
    std::mutex mutex; Snapshot snapshot;
    std::atomic_bool sampling{true};
    std::atomic<double> sample_hz{60};
    std::atomic<unsigned> read_options{};
    void sync_input_backend() {
        const auto generation=makcu.backend_generation();
        if(generation==input_backend_generation)return;
        makcu.cancel();camera.release();adaptive.release();
        target_lock.invalidate("Input backend changed / release X2");
        ray_click.release(makcu,"Input backend changed");auto_parry.cancel(makcu,"Input backend changed");
        input_backend_generation=generation;
    }
    const CameraFocusSettings& focus_profile(const Snapshot& state) {
        const bool scoped=sniper_profile_active(state.hero,true);
        const bool use_ult=scoped&&settings.vindicta_ult_override;
        if(scoped!=ult_scope_active||use_ult!=ult_profile_active) {
            // Drop selection and queued corrections before changing profiles or shot type.
            camera.release();adaptive.release();makcu.cancel();ray_click.release(makcu);
            ult_scope_active=scoped;ult_profile_active=use_ult;
        }
        const auto& profile=use_ult?settings.vindicta_ult:static_cast<const CameraFocusSettings&>(settings);
        if(scoped){scoped_controls=assassinate_controls(profile);return scoped_controls;}
        return profile;
    }
    void open_settings() {
        makcu.cancel();ray_click.release(makcu,"Paused / menu open");
        target_lock.invalidate("Menu open / release X2");
        show_settings=true;
        ShowWindow(settings_window.hwnd,SW_SHOW);
        ShowWindow(settings_window.hwnd,SW_SHOW);
        SetWindowPos(settings_window.hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        DWORD foreground_thread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
        DWORD current_thread=GetCurrentThreadId();
        bool attached=foreground_thread&&foreground_thread!=current_thread&&AttachThreadInput(current_thread,foreground_thread,TRUE);
        SetForegroundWindow(settings_window.hwnd);
        SetActiveWindow(settings_window.hwnd);
        SetFocus(settings_window.hwnd);
        if(attached)AttachThreadInput(current_thread,foreground_thread,FALSE);
        // The game owns relative input while active; the menu needs an unrestricted pointer.
        if(GetForegroundWindow()==settings_window.hwnd){ClipCursor(nullptr);SetCapture(settings_window.hwnd);}
    }
    void hide_settings() {
        bool owned_focus=GetForegroundWindow()==settings_window.hwnd;
        show_settings=false;health_key_capture.cancel();save(settings);
        if(GetCapture()==settings_window.hwnd)ReleaseCapture();
        settings_window.dragging=false;
        ShowWindow(settings_window.hwnd,SW_HIDE);
        if(owned_focus&&IsWindow(game_hwnd)&&!IsIconic(game_hwnd))SetForegroundWindow(game_hwnd);
    }
};
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    auto s=reinterpret_cast<Surface*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {s=reinterpret_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,message,w,l);
    // Move the frameless menu from its header while the GUI owns input.
    if(!s->overlay) {
        if(message==WM_LBUTTONDOWN) {
            POINT point{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
            RECT client{};GetClientRect(hwnd,&client);
            float dpi=float(GetDpiForWindow(hwnd))/96;
            if(point.y<24*dpi || (point.y<88*dpi && point.x>560*dpi && point.x<client.right-200*dpi)) {
                s->dragging=true;GetCursorPos(&s->drag_origin);RECT bounds{};GetWindowRect(hwnd,&bounds);
                s->drag_window={bounds.left,bounds.top};SetCapture(hwnd);return 0;
            }
        }
        if(message==WM_MOUSEMOVE && s->dragging) {
            POINT cursor{};GetCursorPos(&cursor);SetWindowPos(hwnd,nullptr,s->drag_window.x+cursor.x-s->drag_origin.x,
                s->drag_window.y+cursor.y-s->drag_origin.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return 0;
        }
        if(message==WM_LBUTTONUP && s->dragging){s->dragging=false;return 0;}
        if(message==WM_CAPTURECHANGED)s->dragging=false;
    }
    if(!s->overlay && s->context) {
        auto previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(s->context);
        auto handled=ImGui_ImplWin32_WndProcHandler(hwnd,message,w,l);ImGui::SetCurrentContext(previous);
        if((message==WM_LBUTTONUP||message==WM_RBUTTONUP||message==WM_MBUTTONUP)
            &&s->app->show_settings&&GetForegroundWindow()==hwnd&&GetCapture()!=hwnd)SetCapture(hwnd);
        if(handled)return handled;
    }
    switch(message) {
    case WM_SIZE: if(w!=SIZE_MINIMIZED){s->width=LOWORD(l);s->height=HIWORD(l);s->resized=true;} return 0;
    case WM_DPICHANGED: if(!s->overlay) {auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);} return 0;
    case WM_CLOSE: if(!s->overlay)s->app->hide_settings();return 0;
    case WM_KILLFOCUS:if(!s->overlay&&s->app->health_key_capture.active())s->app->health_key_capture.request_cancel();break;
    case WM_KEYDOWN:if(!s->overlay&&w==VK_ESCAPE){
        if(s->app->health_key_capture.active())s->app->health_key_capture.request_cancel();
        else s->app->hide_settings();return 0;
    }break;
    case WM_SETCURSOR:
        if(!s->overlay&&LOWORD(l)==HTCLIENT&&solace::glass::enabled()&&solace::glass::cursor_live()) {SetCursor(nullptr);return TRUE;}
        break;
    case WM_MOUSEACTIVATE:return s->overlay?MA_NOACTIVATE:MA_ACTIVATE;
    case WM_NCHITTEST:if(s->overlay)return HTTRANSPARENT;break;
    case WM_SYSCOMMAND:if((w&0xfff0)==SC_KEYMENU)return 0;break;
    case WM_DESTROY:return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
bool create_surface(Application& app,Surface& s,bool overlay) {
    s.app=&app;s.overlay=overlay;const wchar_t* cls=overlay?L"DeadlockHealthOverlay":L"DeadlockOverlaySettings";
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=cls;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    DWORD ex=WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW;
    if(overlay)ex|=WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
    auto dpi=GetDpiForSystem();int width=MulDiv(1168,dpi,96),height=MulDiv(768,dpi,96);
    s.hwnd=CreateWindowExW(ex,cls,overlay?L"Deadlock healthbars":L"Deadlock overlay menu",WS_POPUP,
        overlay?0:(GetSystemMetrics(SM_CXSCREEN)-width)/2,overlay?0:(GetSystemMetrics(SM_CYSCREEN)-height)/2,
        overlay?1:width,overlay?1:height,nullptr,nullptr,wc.hInstance,&s);
    if(!s.hwnd)return false;
    SetLayeredWindowAttributes(s.hwnd,0,255,LWA_ALPHA);MARGINS margins{-1};if(FAILED(DwmExtendFrameIntoClientArea(s.hwnd,&margins)))return false;
    if(!s.renderer.initialize(s.hwnd,prefer_composition))return false;
    s.context=ImGui::CreateContext();ImGui::SetCurrentContext(s.context);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
    if(overlay)io.ConfigFlags|=ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange;
    if(!ImGui_ImplWin32_Init(s.hwnd)||!ImGui_ImplDX11_Init(s.renderer.device(),s.renderer.context()))return false;
    ImGui::StyleColorsDark();
    if(overlay) {ImFontConfig config;config.FontDataOwnedByAtlas=false;io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(solace::geist_semibold.data()),static_cast<int>(solace::geist_semibold.size()),15.f,&config);}
    else {
        solace::ui_runtime::set_scale(float(GetDpiForWindow(s.hwnd))/96,false);solace::fonts.install_kerning();
        solace::icons::initialize_textures(s.renderer.device(),s.renderer.context());
        solace::font_regular(10);solace::font_regular(12);solace::font_regular(14);solace::font_regular(16);solace::font_semibold(20);
        solace::font_medium(10);solace::font_medium(12);solace::font_medium(14);
        solace::set_dark(app.settings.dark);
        s.panel_shader=solace::rounded_panel::init(s.renderer.device(),s.renderer.context());
        s.glass_shader=solace::glass::cursor_init(s.renderer.device(),s.renderer.context());
        solace::glass::enabled()=app.settings.glass;
    }
    return true;
}
bool begin_frame(Surface& s) {
    ImGui::SetCurrentContext(s.context);
    if(!s.overlay) {
        float scale=float(GetDpiForWindow(s.hwnd))/96;
        if(std::abs(scale-solace::ui_runtime::scale)>0.001f)solace::ui_runtime::set_scale(scale,true);
        solace::fonts.update();
    }
    if(s.resized && s.width && s.height) {s.resized=false;if(!s.overlay)solace::snapshot::invalidate_backdrop();if(!s.renderer.resize(s.width,s.height))return false;}
    if(!s.overlay)solace::snapshot::attach(s.renderer.device(),s.renderer.context(),s.renderer.swap_chain());
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();return true;
}
void render_frame(Surface& s) {
    ImGui::Render();s.renderer.clear({0,0,0,0});ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(!s.overlay)solace::snapshot::poll(s.renderer.device(),s.renderer.context(),s.renderer.swap_chain());
}
bool end_frame(Surface& s) {
    auto started=clock_ms();render_frame(s);s.submit_ms=clock_ms()-started;
    started=clock_ms();auto result=s.renderer.present(0);s.present_ms=clock_ms()-started;s.last_presented=result==S_OK;
    if(result==DXGI_ERROR_WAS_STILL_DRAWING){++s.busy_presents;return true;}return SUCCEEDED(result);
}
void draw_health_number(ImDrawList* draw,BarRect rect,int health,const Settings& s,float scale) {
    if(s.numbers){auto text=std::to_string(health);auto font=ImGui::GetFont();float size=15*scale;auto extent=font->CalcTextSizeA(size,FLT_MAX,0,text.c_str());
        ImVec2 pos{rect.left-extent.x-4*scale,rect.top-2*scale};
        draw->AddText(font,size,{pos.x+1,pos.y+1},IM_COL32(0,0,0,240),text.c_str());draw->AddText(font,size,pos,IM_COL32(242,242,242,255),text.c_str());}
}
bool healthbar_world_gap_pixels(const Matrix& matrix,Vec3 edge,float viewport_width,float meters,float& pixels) {
    if(!std::isfinite(viewport_width)||viewport_width<=0||!std::isfinite(meters)||meters<0)return false;
    FocusClip clip;if(!focus_clip(matrix,edge,clip)||clip.w<=.001f)return false;
    const float right_length=std::sqrt(matrix[0]*matrix[0]+matrix[1]*matrix[1]+matrix[2]*matrix[2]);
    if(!std::isfinite(right_length)||right_length<=1e-8f)return false;
    // A camera-facing horizontal distance at the leftmost joint's depth.
    // Use the render projection and the existing game-unit conversion.
    pixels=(meters/meters_per_world_unit)*(viewport_width*.5f)*right_length/clip.w;
    return std::isfinite(pixels)&&pixels>=0;
}
BarRect vertical_healthbar_rect(BarRect body,float gap) {
    const float outer_top=std::floor(body.top),outer_bottom=std::max(outer_top+3.f,std::ceil(body.bottom));
    // Enclose all projected body joints, then leave an exact pixel gap between
    // the box's left edge and the bar's outer black border. Gap is already in pixels.
    const float left=std::floor(body.left)-std::round(gap)-2.f;
    return {left,outer_top+1,left+1,outer_bottom-1};
}
bool player_healthbar_rect(const Player& player,const Matrix& matrix,float width,float height,const Settings& s,BarRect& rect,BarRect* bounds=nullptr) {
    if(player.body_point_count<2||player.body_point_count>player.body_points.size())return false;
    BarRect body{FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX};Vec3 left_edge{};
    for(size_t i=0;i<player.body_point_count;++i) {
        ScreenPoint point;if(!project(matrix,player.body_points[i],width,height,point))return false;
        if(point.x<body.left){body.left=point.x;left_edge=player.body_points[i];}
        body.right=std::max(body.right,point.x);
        body.top=std::min(body.top,point.y);body.bottom=std::max(body.bottom,point.y);
    }
    if(body.bottom<=body.top||body.right<0||body.left>width||body.bottom<0||body.top>height)return false;
    float gap{};if(!healthbar_world_gap_pixels(matrix,left_edge,width,s.healthbar_box_gap_meters,gap))return false;
    if(bounds)*bounds={std::floor(body.left),std::floor(body.top),std::ceil(body.right),std::ceil(body.bottom)};
    rect=vertical_healthbar_rect(body,gap);return true;
}
void draw_vertical_bar(ImDrawList* draw,BarRect rect,int health,int maximum,const Settings& s,float scale) {
    // One integer-aligned green column with a one-pixel opaque black border.
    draw->AddRectFilled({rect.left-1,rect.top-1},{rect.right+1,rect.bottom+1},IM_COL32(0,0,0,255));
    const float ratio=health_fraction(health,maximum),length=rect.bottom-rect.top;
    const float pixels=ratio>0?std::max(1.f,std::round(length*ratio)):0.f;
    if(pixels>0)draw->AddRectFilled({rect.left,rect.bottom-pixels},{rect.right,rect.bottom},IM_COL32(0,255,0,255));
    draw_health_number(draw,rect,health,s,scale);
}
void draw_dot(ImDrawList* draw,ScreenPoint p,int index,float scale) {
    constexpr ImU32 colors[]={IM_COL32(245,245,245,255),IM_COL32(56,205,239,255),IM_COL32(255,181,61,255)};
    draw->AddCircleFilled({p.x,p.y},5*scale,IM_COL32(8,9,10,240),16);
    draw->AddCircleFilled({p.x,p.y},3.5f*scale,colors[index],16);
}
bool enemy_visual_visibility_allows(const Snapshot& snapshot,const Player& player,bool hide_when_visible) {
    if(!hide_when_visible)return true;
    if(!snapshot.visibility||!snapshot.camera_valid||player.visible
        ||std::none_of(player.dot_valid.begin(),player.dot_valid.end(),[](bool valid){return valid;}))return false;
    // Treat an exposed hitbox as visible even if all three anchor points are blocked.
    return std::none_of(player.hitboxes.begin(),player.hitboxes.end(),[](const auto& box){return box.visibility_known&&box.visible;});
}
bool hidden_enemy_tag_eligible(const Snapshot& snapshot,const Player& player,bool hide_when_visible=true) {
    return enemy_teams(snapshot.local_team,player.team)&&player.health>0&&player.feet_valid&&!player.hero_name.empty()
        &&enemy_visual_visibility_allows(snapshot,player,hide_when_visible);
}
bool draw_hidden_enemy_tag(ImDrawList* draw,const Snapshot& snapshot,const Player& player,float width,float height,const Settings& settings,float scale) {
    if(!hidden_enemy_tag_eligible(snapshot,player,settings.hide_visible_enemy_tags)||width<80*scale||height<40*scale)return false;
    ScreenPoint feet;
    if(!project(snapshot.matrix,player.feet,width,height,feet))return false;
    auto font=ImGui::GetFont();const float size=13*scale,padding=10*scale;
    const auto text_size=font->CalcTextSizeA(size,FLT_MAX,0,player.hero_name.c_str());
    const float tag_width=std::min(text_size.x+padding*2,width-8*scale),tag_height=text_size.y+10*scale;
    const ImVec2 at{std::clamp(feet.x-tag_width*.5f,4*scale,width-4*scale-tag_width),feet.y+8*scale};
    const ImVec2 end{at.x+tag_width,at.y+tag_height};
    if(end.y>height-4*scale)return false;
    const float opacity=settings.transparent_frame?settings.frame_opacity:1.f,rounding=8*scale;
    draw->AddRectFilled(at+ImVec2{0,2*scale},end+ImVec2{0,2*scale},IM_COL32(0,0,0,int(32*opacity)),rounding);
    // The overlay has its own D3D device. Use the same frame palette and rounded
    // shape without binding the menu's device-specific panel shader here.
    draw->AddRectFilled(at,end,solace::mo::with_alpha(solace::c_background,opacity),rounding);
    draw->AddRect(at,end,solace::c_border,rounding,0,scale);
    const ImVec4 clip{at.x+padding,at.y,end.x-padding,end.y};
    draw->AddText(font,size,at+ImVec2{padding,5*scale},solace::c_foreground,player.hero_name.c_str(),nullptr,0,&clip);
    return true;
}
void draw_skeleton(ImDrawList* draw,const Player& player,const Matrix& matrix,float width,float height,const Settings& settings,float scale) {
    if(settings.hide_visible_skeletons&&player.visible)return;
    for(const auto& segment:player.skeleton) {
        ScreenPoint a{},b{};
        if(!project(matrix,segment.a,width,height,a)||!project(matrix,segment.b,width,height,b))continue;
        auto color=segment.visibility_known?ImGui::ColorConvertFloat4ToU32(segment.visible?settings.skeleton_visible:settings.skeleton_hidden):IM_COL32(155,160,170,225);
        draw->AddLine({a.x,a.y},{b.x,b.y},IM_COL32(5,8,15,210),3.5f*scale);
        draw->AddLine({a.x,a.y},{b.x,b.y},color,1.7f*scale);
    }
}
int draw_hitboxes(ImDrawList* draw,const FocusTarget& player,const Matrix& matrix,float width,float height,const Settings& settings,float scale,ImVec2 offset={},bool visible_only=false) {
    int drawn=0;
    // Reject a whole offscreen shape before generating/clipping its wire mesh.
    const auto plane_length=[&](int row,float sign) {
        Vec3 n{matrix[12]+sign*matrix[row],matrix[13]+sign*matrix[row+1],matrix[14]+sign*matrix[row+2]};return std::sqrt(dot(n,n));
    };
    const float normals[]={std::sqrt(matrix[12]*matrix[12]+matrix[13]*matrix[13]+matrix[14]*matrix[14]),plane_length(0,1),plane_length(0,-1),plane_length(4,1),plane_length(4,-1)};
    for(const auto& box:player.hitboxes) {
        if(visible_only&&(!box.visibility_known||!box.visible))continue;
        FocusClip clip;if(!focus_clip(matrix,hitbox_center(box),clip))continue;
        auto axis=sub(box.b,box.a);float extent=.5f*std::sqrt(dot(axis,axis))+(box.shape==HitboxShape::Box?0:box.radius);
        const float distance[]={clip.w-.0011f,clip.w+clip.x,clip.w-clip.x,clip.w+clip.y,clip.w-clip.y};
        bool outside=false;for(int i=0;i<5;++i)if(distance[i]+extent*normals[i]<0){outside=true;break;}if(outside)continue;
        auto color=box.visibility_known?ImGui::ColorConvertFloat4ToU32(box.visible?settings.skeleton_visible:settings.skeleton_hidden):IM_COL32(155,160,170,225);
        bool submitted=false;
        hitbox_segments(box,[&](Vec3 from,Vec3 to) {
            ScreenPoint a{},b{};if(!focus_project_segment(matrix,from,to,width,height,a,b))return;
            draw->AddLine(ImVec2{a.x,a.y}+offset,ImVec2{b.x,b.y}+offset,IM_COL32(5,8,15,190),2.8f*scale);
            draw->AddLine(ImVec2{a.x,a.y}+offset,ImVec2{b.x,b.y}+offset,color,1.2f*scale);submitted=true;
        });
        if(submitted)++drawn;
    }
    return drawn;
}
void draw_angular_area(ImDrawList* draw,const Snapshot& snapshot,float radius,float width,float height,ImU32 color,float thickness) {
    CameraBasis basis;if(!camera_basis(snapshot.matrix,basis))return;
    const float tangent=std::tan(radius/angular_degrees);constexpr int segments=96;
    auto world=[&](int i){float a=float(i)*6.28318530718f/segments;return add(basis.origin,add(mul(basis.forward,100),mul(add(mul(basis.right,std::cos(a)),mul(basis.up,std::sin(a))),100*tangent)));};
    Vec3 previous=world(segments-1);
    for(int i=0;i<segments;++i){auto next=world(i);ScreenPoint a{},b{};if(focus_project_segment(snapshot.matrix,previous,next,width,height,a,b))draw->AddLine({a.x,a.y},{b.x,b.y},color,thickness);previous=next;}
}
int draw_adaptive_solution(ImDrawList* draw,const Snapshot& snapshot,const AdaptiveFocusSolution& solution,float width,float height,float scale) {
    if(!solution.valid)return 0;
    for(size_t i=0;i<solution.box_count;++i) {
        Hitbox box=solution.boxes[i];box.a=add(box.a,solution.lead);box.b=add(box.b,solution.lead);
        for(auto& corner:box.corners)corner=add(corner,solution.lead);
        const bool selected=box.source_id==solution.source_id;
        hitbox_segments(box,[&](Vec3 a,Vec3 b){ScreenPoint x{},y{};if(focus_project_segment(snapshot.matrix,a,b,width,height,x,y))
            draw->AddLine({x.x,x.y},{y.x,y.y},selected?IM_COL32(255,200,70,150):IM_COL32(70,160,255,75),selected?1.5f*scale:scale);});
    }
    ScreenPoint point;if(focus_project(snapshot.matrix,solution.aim_point,width,height,point))
        draw->AddCircle({point.x,point.y},solution.inside?5*scale:9*scale,solution.inside?IM_COL32(60,220,160,230):IM_COL32(255,200,70,240),24,2*scale);
    return int(solution.box_count);
}
void draw_focus_area(ImDrawList* draw,const Snapshot& snapshot,const CameraFocusSettings& settings,float width,float height,uint32_t selected,float scale,
    const AdaptiveTargetZone* zone=nullptr,target_zone::ZoneOutlineCache* outline=nullptr) {
    if(!show_focus_area(settings)||width<=0||height<=0)return;
    if(settings.adaptive.area==AdaptiveAreaMode::TargetZone) {
        if(!zone||!zone->valid||!outline)return;
        const bool retained=selected&&selected==zone->target;
        if(!outline->prepare(*zone,snapshot.matrix,width,height,retained))return;
        auto lines=[&](const target_zone::UnionOutline& shape,ImU32 color) {
            for(const auto& line:shape.segments)draw->AddLine({float(line.a.x),float(line.a.y)},{float(line.b.x),float(line.b.y)},color,scale);
        };
        if(retained)lines(outline->retention,IM_COL32(255,200,70,55));
        lines(outline->activation,retained?IM_COL32(255,200,70,230):IM_COL32(70,155,255,215));
        return;
    }
    draw_angular_area(draw,snapshot,settings.adaptive.radius_degrees,width,height,IM_COL32(90,170,255,190),1.4f*scale);
    if(selected)draw_angular_area(draw,snapshot,settings.adaptive.radius_degrees*1.3f,width,height,IM_COL32(90,170,255,65),scale);
    return;
}

int draw_focus_guide(ImDrawList* draw,const Snapshot& snapshot,const FocusTarget& target,const CameraFocusSettings& focus,
    const Settings& settings,float width,float height,float scale,bool hitboxes_drawn=false) {
    if(!focus.focus_visuals||!focus.free_focus||target.kind==TargetKind::SoulOrb
        ||!focus_candidate(target,snapshot,{focus.focus_players,focus.focus_minions,focus.focus_orbs,focus.strict_enemy_players},focus.exclude_teammates))return 0;
    if(focus.free_focus_mode==FreeMovementMode::Hitboxes)
        return hitboxes_drawn?0:draw_hitboxes(draw,target,snapshot.matrix,width,height,settings,scale,{},focus.visible_only);
    for(int i=0;i<2;++i) {
        ScreenPoint a{},b{};
        if(target.dot_valid[i]&&target.dot_valid[i+1]&&(!focus.visible_only||(target.dot_visible[i]&&target.dot_visible[i+1]))
            &&project(snapshot.matrix,target.dots[i],width,height,a)&&project(snapshot.matrix,target.dots[i+1],width,height,b))
            draw->AddLine({a.x,a.y},{b.x,b.y},IM_COL32(100,210,230,180),2*scale);
    }
    for(int i=0;i<3;++i) {
        ScreenPoint point{};
        if(target.dot_valid[i]&&(!focus.visible_only||target.dot_visible[i])&&project(snapshot.matrix,target.dots[i],width,height,point))
            draw_dot(draw,point,i,scale);
    }
    return 0;
}
struct PageContext { Application& app; const Snapshot& snapshot; };
void draw_midboss_hud(ImDrawList* draw,const MidBossState& state,float scale,ImVec2 anchor={24,100}) {
    const ImVec2 at=anchor*scale,end=at+ImVec2{248*scale,67*scale};
    const ImU32 accent=state.phase==MidBossPhase::Alive?IM_COL32(70,220,165,255)
        :state.phase==MidBossPhase::Countdown?(state.remaining<=30?IM_COL32(255,196,75,255):IM_COL32(90,170,255,255))
        :IM_COL32(160,170,185,255);
    draw->AddRectFilled(at,end,IM_COL32(15,20,28,222),10*scale);
    draw->AddRect(at,end,IM_COL32(110,135,165,70),10*scale,0,scale);
    draw->AddLine(at+ImVec2{1*scale,15*scale},at+ImVec2{1*scale,52*scale},accent,2*scale);
    draw->AddText(ImGui::GetFont(),14*scale,at+ImVec2{15*scale,13*scale},IM_COL32(230,236,245,255),"MID BOSS");
    const std::string value=state.phase==MidBossPhase::Countdown?midboss_countdown(state.remaining)
        :state.phase==MidBossPhase::Alive?"ALIVE":state.phase==MidBossPhase::Due?"DUE":"--:--";
    auto font=ImGui::GetFont();const auto size=font->CalcTextSizeA(21*scale,FLT_MAX,0,value.c_str());
    draw->AddText(font,21*scale,{end.x-15*scale-size.x,at.y+10*scale},accent,value.c_str());
    const char* detail=state.paused?"Paused / game clock stopped":state.phase==MidBossPhase::Countdown?"Until next spawn"
        :state.phase==MidBossPhase::Alive?"Living boss confirmed":state.phase==MidBossPhase::Due?"Awaiting boss entity":"Spawn data unavailable";
    draw->AddText(font,12*scale,at+ImVec2{15*scale,43*scale},IM_COL32(160,174,193,255),detail);
}
void draw_adaptive_response_info(const Snapshot& snap) {
    ImGui::TextWrapped("Screen feedback / no calibration required");
    ImGui::Text("Current view: %.1f x %.1f deg / aspect %.3f",snap.projection.horizontal_fov,snap.projection.vertical_fov,snap.projection.aspect);
    ImGui::TextWrapped("Uses the current view during zoom, slides and ability FOV changes. Adjust Speed for your mouse sensitivity.");
}
void draw_zone_diagnostics(const AdaptiveFocus& focus) {
    ImGui::TextWrapped("%s",focus.zone_status());
    if(const auto* zone=focus.zone())ImGui::Text("Height %.1f / padding %.1f / %zu volumes",zone->body_height,zone->padding,zone->activation.size());
    const auto issues=focus.zone_issues();
    if(!issues.empty()&&ImGui::TreeNode("zone_issues","Unavailable targets (%zu)",issues.size())) {
        ImGui::BeginChild("##zone_issue_list",{0,solace::px(150)},true);
        for(const auto& issue:issues)ImGui::TextWrapped("%s %u: %s",target_kind_name(issue.kind),issue.target,issue.reason);
        ImGui::EndChild();ImGui::TreePop();
    }
}
void overlay_page(solace::shell_page page,const ImRect& body,float alpha,void* context) {
    auto& data=*static_cast<PageContext*>(context);auto& app=data.app;const auto& snap=data.snapshot;
    using namespace solace;
    if(app.page!=int(page)){app.page=int(page);app.page_time=0;}
    app.page_time+=ImGui::GetIO().DeltaTime;
    float reveal=mo::EASE_OUT(std::clamp(app.page_time/.22f,0.f,1.f));alpha*=reveal;
    ImVec2 origin=body.Min+px(0.f,8.f*(1-reveal));auto dl=ImGui::GetWindowDrawList();
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=16.f){draw_text(dl,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);};
    draw_text_tracked(dl,font_semibold(24),origin,mo::with_alpha(c_foreground,alpha),page==shell_page::camera?"Camera Focus":page==shell_page::vindicta_ult?"Vindicta Ultimate":page==shell_page::health?"Health bars":page==shell_page::test?"Diagnostics":page==shell_page::auto_click?"Auto Click":page==shell_page::auto_parry?"Auto Parry":"Connection",px(-.4f));
    auto card=[&](ImRect box,const char* title,icons::id icon){
        rounded_panel::draw(dl,box.Min,box.Max,mo::with_alpha(c_card,(app.settings.transparent_frame?1.f:.72f)*alpha),px(16));
        dl->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),0,px(1));
        icons::draw(icon,dl,box.Min+px(20,20),px(16),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(dl,font_semibold(12),box.Min+px(46,21),mo::with_alpha(c_muted_foreground,alpha),title,px(.8f));
    };
    bool changed=false;
    ui_runtime::push_font(font_semibold(16));ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(c_foreground));
    if(page==shell_page::health) {
        text({body.Max.x-px(354),origin.y+px(6)},"Hold to display",true,14);
        changed|=switch_toggle("health_visuals_hold",{body.Max.x-px(218),origin.y},&app.settings.health_visuals.hold_only);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Hold the bound key to show all Health bars features: bars, numbers, skeletons, hitboxes, hero tags and the debug box.");
        const auto key_label=health_visuals_key_name(app.settings.health_visuals.key);
        ImGui::SetCursorScreenPos({body.Max.x-px(148),origin.y-px(1)});
        if(ImGui::Button(("Key: "+key_label).c_str(),px(148,30))){app.health_key_capture.begin();ImGui::OpenPopup("Health display key");}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Click to bind a keyboard key or mouse button. Default: H. Escape and Insert are reserved.");
        if(ImGui::BeginPopup("Health display key")) {
            ImGui::TextUnformatted("Release held keys, then press a key or mouse button.");
            ImGui::TextUnformatted("Escape cancels. Insert closes the menu.");
            if(GetForegroundWindow()!=app.settings_window.hwnd){app.health_key_capture.cancel();ImGui::CloseCurrentPopup();}
            else {
                const int key=app.health_key_capture.poll();
                if(key>0){app.settings.health_visuals.key=key;changed=true;ImGui::CloseCurrentPopup();}
                else if(key<0)ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(!ImGui::IsPopupOpen("Health display key"))app.health_key_capture.cancel();
        const float gap=px(16),column=(body.GetWidth()-gap)*.5f;
        ImRect controls(origin+px(0,60),{body.Max.x,origin.y+px(244)});
        ImRect geometry(origin+px(0,256),origin+ImVec2(column,px(480)));
        ImRect tags({geometry.Max.x+gap,geometry.Min.y},{body.Max.x,origin.y+px(420)});
        card(controls,"HEALTH BARS",icons::id::eye);
        card(geometry,"SKELETONS & HITBOXES",icons::id::sparkles);
        card(tags,"HERO NAME TAGS",icons::id::user);
        auto toggle=[&](const ImRect& box,float y,const char* id,const char* title,bool& value) {
            text(box.Min+px(20,y+5),title,false,14);
            changed|=switch_toggle(id,{box.Max.x-px(20+switch_w),box.Min.y+px(y)},&value);
        };
        ImRect left=controls,right=controls;left.Max.x=controls.GetCenter().x-gap*.5f;right.Min.x=controls.GetCenter().x+gap*.5f;
        toggle(left,48,"draw_health","Health bars",app.settings.enabled);
        toggle(left,90,"show_numbers","Health numbers",app.settings.numbers);
        ImGui::BeginDisabled(!app.settings.enabled);
        toggle(left,132,"hide_visible_healthbars","Hide when visible",app.settings.hide_visible_healthbars);
        ImGui::EndDisabled();
        char distance[32]{};snprintf(distance,sizeof(distance),"%.2f m",app.settings.healthbar_box_gap_meters);
        text(right.Min+px(20,50),"Box gap",true,14);
        text({right.Max.x-px(20)-text_width(font_semibold(14),distance),right.Min.y+px(50)},distance,false,14);
        changed|=range_slider("healthbar_box_gap_meters",right.Min+px(20,70),right.GetWidth()-px(40),&app.settings.healthbar_box_gap_meters,0,2);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("World distance from the body box's left edge to the bar. The gap projects through the current camera view.");
        toggle(right,132,"healthbar_debug_box","Debug placement box",app.settings.healthbar_debug_box);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Draw the exact projected bone box used to place the health bar. Works with health bars disabled.");
        toggle(geometry,46,"skeletons","Skeletons",app.settings.skeletons);
        ImGui::BeginDisabled(!app.settings.skeletons);
        toggle(geometry,82,"hide_visible_skeletons","Hide visible skeletons",app.settings.hide_visible_skeletons);
        ImGui::EndDisabled();
        toggle(geometry,118,"hitboxes","Hitboxes",app.settings.hitboxes);
        ImGui::SetCursorScreenPos(geometry.Min+px(20,168));
        constexpr auto color_flags=ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel;
        changed|=ImGui::ColorEdit3("##skeleton_visible",&app.settings.skeleton_visible.x,color_flags);
        ImGui::SameLine();ImGui::TextUnformatted("Visible");ImGui::SameLine();
        changed|=ImGui::ColorEdit3("##skeleton_hidden",&app.settings.skeleton_hidden.x,color_flags);
        ImGui::SameLine();ImGui::TextUnformatted("Blocked");
        toggle(tags,48,"hidden_enemy_tags","Hero name tags",app.settings.hidden_enemy_tags);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Hero names below enemies. Uses Appearance transparency and opacity.");
        ImGui::BeginDisabled(!app.settings.hidden_enemy_tags);
        toggle(tags,90,"hide_visible_enemy_tags","Hide when visible",app.settings.hide_visible_enemy_tags);
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(origin+px(0,522));
        if(ImGui::SmallButton("Reset all settings")){app.settings=Settings{};set_dark(true);changed=true;}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Reset every tab, including camera profiles and automation.");
    } else if(page==shell_page::camera||page==shell_page::vindicta_ult) {
        const bool ultimate=page==shell_page::vindicta_ult;
        auto& focus=ultimate?app.settings.vindicta_ult:static_cast<CameraFocusSettings&>(app.settings);
        if(ultimate)restrict_assassinate_targets(focus);
        bool adaptive=adaptive_mode(focus);
        const bool scoped=sniper_profile_active(snap.hero,true);
        const bool ult_active=scoped&&app.settings.vindicta_ult_override;
        ImGui::PushID(ultimate?"vindicta_ult":"regular_focus");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,px(10,6));
        text({body.Max.x-px(190),origin.y+px(5)},"Camera focus",true,14);
        changed|=switch_toggle("replay_focus",{body.Max.x-px(48),origin.y},&focus.replay_focus);
        const char* input_label=app.makcu.label();
        badge("input_ready",dl,origin+px(0,43),input_label,app.makcu.ready()?badge_good:badge_warn,false,alpha);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",app.makcu.status().c_str());
        char hero_label[160]{};
        snprintf(hero_label,sizeof(hero_label),"%s%s",snap.hero.name.empty()?"Waiting for hero":snap.hero.name.c_str(),
            ultimate?(ult_active?" / Assassinate active":" / Assassinate profile"):ult_active?" / Ultimate profile active":"");
        dl->PushClipRect(origin+px(0,35),{body.Max.x-px(164),origin.y+px(70)},true);
        text({origin.x+badge_width(input_label)+px(16),origin.y+px(46)},hero_label,true,14);
        dl->PopClipRect();
        ImGui::SetCursorScreenPos({body.Max.x-px(148),origin.y+px(37)});
        if(ImGui::Button(ultimate?"Profile options":"Hero / abilities",px(148,30)))ImGui::OpenPopup("Hero ability settings");
        ImGui::SetNextWindowSizeConstraints(px(370,0),px(440,FLT_MAX));
        if(ImGui::BeginPopup("Hero ability settings")) {
            ImGui::Text("Detected hero: %s",snap.hero.name.empty()?"Waiting for hero":snap.hero.name.c_str());
            if(snap.hero.valid)ImGui::Text("Hero ID: %d",snap.hero.id);
            ImGui::TextWrapped("%s",snap.hero.ability_status.c_str());ImGui::Separator();
            if(ultimate) {
                changed|=ImGui::Checkbox("Use separate ultimate profile",&app.settings.vindicta_ult_override);
                ImGui::TextWrapped("Applies while Vindicta is scoped with Assassinate. Assassinate always targets enemy players only.");
                if(ImGui::Button("Copy regular controls")){focus=assassinate_controls(static_cast<const CameraFocusSettings&>(app.settings));focus.prediction=false;changed=true;}
                if(ImGui::Button("Reset ultimate controls")){focus=assassinate_controls();changed=true;}
            }else {
                changed|=ImGui::Checkbox("Sandbox & bots",&focus.debug_focus);
                if(ImGui::Button("Open Vindicta Ultimate")){app.menu_page=shell_page::vindicta_ult;ImGui::CloseCurrentPopup();}
            }
            if(ImGui::Button("Open Auto Click")){app.click_ultimate=ultimate;app.menu_page=shell_page::auto_click;ImGui::CloseCurrentPopup();}
            ImGui::Separator();
            if(snap.projection.valid) {
                ImGui::Text("Live view: %.1f x %.1f degrees",snap.projection.horizontal_fov,snap.projection.vertical_fov);
                ImGui::Text("Render aspect ratio: %.3f",snap.projection.aspect);
            }else ImGui::TextUnformatted("Waiting for the game's render view");
            ImGui::EndPopup();
        }
        const float gap=px(16),left=(body.GetWidth()-gap)*.53f;
        ImRect movement(origin+px(0,80),origin+ImVec2(left,px(ultimate?510.f:354.f)));
        ImRect prediction({movement.Min.x,origin.y+px(366)},{movement.Max.x,origin.y+px(510)});
        ImRect targets({movement.Max.x+gap,movement.Min.y},{body.Max.x,origin.y+px(262)});
        ImRect area({targets.Min.x,origin.y+px(274)},{targets.Max.x,origin.y+px(510)});
        card(movement,"MOVEMENT",icons::id::crosshair_simple);
        if(!ultimate)card(prediction,"PREDICTION",icons::id::sparkles);
        card(targets,"TARGETS",icons::id::user);
        card(area,"FOCUS AREA",icons::id::target);
        auto row_combo=[&](const ImRect& box,float y,const char* label,const char* id,int& value,const char* const* options,int count) {
            text(box.Min+px(20,y+6),label,true,14);
            const float width=std::min(px(230),box.GetWidth()*.60f);
            ImGui::SetCursorScreenPos({box.Max.x-px(20)-width,box.Min.y+px(y)});ImGui::SetNextItemWidth(width);
            return ImGui::Combo(id,&value,options,count);
        };
        auto row_toggle=[&](const ImRect& box,float y,const char* label,const char* id,bool& value) {
            text(box.Min+px(20,y+5),label,false,14);
            return switch_toggle(id,{box.Max.x-px(20+switch_w),box.Min.y+px(y)},&value);
        };
        auto slider=[&](const ImRect& box,float y,const char* label,const char* id,float& value,float low,float high,const char* format) {
            char value_label[64]{};snprintf(value_label,sizeof(value_label),format,value);
            text(box.Min+px(20,y),label,true,14);
            text({box.Max.x-px(20)-text_width(font_semibold(14),value_label),box.Min.y+px(y)},value_label,false,14);
            return range_slider(id,box.Min+px(20,y+20),box.GetWidth()-px(40),&value,low,high);
        };
        constexpr const char* hold_buttons[]={"X1 / X2","Left mouse","X1 only","X2 only","X1 / X2 lock (360 deg)"};int hold=focus.x2_target_lock?4:int(focus.focus_hold_button);
        if(row_combo(movement,50,"Hold button","##focus_hold_button",hold,hold_buttons,5)){
            focus.x2_target_lock=hold==4;focus.focus_hold_button=hold==4?FocusHoldButton::SideButtons:static_cast<FocusHoldButton>(hold);changed=true;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("X1 uses your original speed. X2 has its own speed and takes priority.\nX1 / X2 lock: X1 works normally; X2 acquires one eligible target across 360 degrees and ignores area limits.\nLoss ends that lock until X2 is released and pressed again. Visibility/team/type filters still apply.\nMenu opening, focus loss or stale data also end the current lock.");
        constexpr const char* movement_modes[]={"Fixed bone","V1: Bone line","Adaptive"};
        int mode=!focus.free_focus?0:focus.free_focus_mode==FreeMovementMode::Adaptive?2:1;
        if(row_combo(movement,88,"Movement","##free_focus_mode",mode,movement_modes,3)) {
            focus.free_focus=mode!=0;if(mode)focus.free_focus_mode=mode==2?FreeMovementMode::Adaptive:FreeMovementMode::BoneLine;changed=true;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("V1: free along head, body and pelvis.\nAdaptive: permitted regions, screen feedback and automatic prediction.");
        adaptive=adaptive_mode(focus);
        constexpr const char* bones[]={"Head","Body","Pelvis"};
        changed|=row_combo(movement,126,adaptive?"Preferred bone":"Target bone","##focus_bone",focus.focus_bone,bones,3);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",adaptive?"Breaks near-ties only. Movement stays free inside every permitted hitbox.":"Choose the target bone. Free movement returns to the nearest permitted point.");
        if(adaptive) {
            constexpr const char* regions[]={"Head and torso","Whole body"};int region=int(selectable_adaptive_region(int(focus.adaptive.region)));
            if(row_combo(movement,164,"Free region","##adaptive_region",region,regions,2)){focus.adaptive.region=selectable_adaptive_region(region);changed=true;}
        }else {
            const char* hint=!focus.free_focus?"Focus stays on the selected bone.":"Move freely along the head, body and pelvis line.";
            dl->PushClipRect(movement.Min+px(20,164),{movement.Max.x-px(20),movement.Min.y+px(198)},true);
            text(movement.Min+px(20,170),hint,true,12);dl->PopClipRect();
        }
        // Default/reset profiles also seed X2 before exposing its slider.
        sanitize_camera_settings(focus);
        if(focus.focus_hold_button==FocusHoldButton::LeftMouse) {
            changed|=slider(movement,210,"Left mouse speed","focus_speed",adaptive?focus.adaptive.speed:focus.focus_speed,1,adaptive?100.f:120.f,"%.0f");
        }else {
            ImRect x1=movement,x2=movement;x1.Max.x=movement.GetCenter().x+px(4);x2.Min.x=movement.GetCenter().x-px(4);
            ImGui::BeginDisabled(focus.focus_hold_button==FocusHoldButton::X2);
            changed|=slider(x1,210,"X1 speed","focus_speed",adaptive?focus.adaptive.speed:focus.focus_speed,1,adaptive?100.f:120.f,"%.0f");
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Your original saved speed. Used by X1 and left-mouse activation.");
            ImGui::EndDisabled();ImGui::BeginDisabled(focus.focus_hold_button==FocusHoldButton::X1);
            changed|=slider(x2,210,"X2 speed","focus_speed_x2",adaptive?focus.adaptive_speed_x2:focus.focus_speed_x2,1,adaptive?140.f:240.f,"%.0f");
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Independent faster option. Starts at roughly twice X1's response; the existing movement limits still apply.");
            ImGui::EndDisabled();
        }
        if(ultimate) {
            text(targets.Min+px(20,53),"Enemy players only",false,14);
            text(targets.Min+px(20,89),"Target types and team filter are fixed",true,12);
        }else {
        ui_runtime::push_font(font_semibold(14));
        const float target_column=(targets.GetWidth()-px(40))/3;
        auto target_type=[&](int column,const char* label,bool& enabled) {
            ImGui::SetCursorScreenPos(targets.Min+ImVec2(px(20)+target_column*float(column),px(51)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,enabled?0.f:px(1));
            ImGui::PushStyleColor(ImGuiCol_Button,enabled?ImGui::GetStyleColorVec4(ImGuiCol_Button):ImGui::ColorConvertU32ToFloat4(c_background));
            ImGui::PushStyleColor(ImGuiCol_Border,ImGui::ColorConvertU32ToFloat4(c_border_strong));
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(enabled?c_foreground:c_muted_foreground));
            if(ImGui::Button(label,{target_column-px(column==2?0.f:8.f),px(30)})){enabled=!enabled;changed=true;}
            ImGui::PopStyleColor(3);ImGui::PopStyleVar();
        };
        target_type(0,"Players",focus.focus_players);target_type(1,"Minions",focus.focus_minions);target_type(2,"Soul orbs",focus.focus_orbs);
        ui_runtime::pop_font();
        changed|=row_toggle(targets,91,"Enemies only","exclude_teammates",focus.exclude_teammates);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Filters this Camera Focus profile only.");
        }
        changed|=row_toggle(targets,127,"Visible only","visible_only",focus.visible_only);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",ultimate?"Only select visible points while the Assassinate profile is active.":"Only select visible focus points for this Camera Focus profile.");
        int minions=0,orbs=0;for(const auto& target:snap.focus_targets)if(target.kind==TargetKind::Minion)++minions;else if(target.kind==TargetKind::SoulOrb)++orbs;
        char available[96]{};if(ultimate) {
            const auto enemies=std::count_if(snap.players.begin(),snap.players.end(),[&](const auto& p){return enemy_teams(snap.local_team,p.team);});
            snprintf(available,sizeof(available),"%zu enemy players",size_t(enemies));
        }else snprintf(available,sizeof(available),"%zu players / %d minions / %d orbs",snap.players.size(),minions,orbs);
        text(targets.Min+px(20,163),available,true,12);
        constexpr const char* areas[]={"Crosshair angle","Target Zone"};int shape=int(focus.adaptive.area);
        if(row_combo(area,50,"Area","##focus_area",shape,areas,2)){focus.adaptive.area=static_cast<AdaptiveAreaMode>(shape);changed=true;}
        const bool zone=focus.adaptive.area==AdaptiveAreaMode::TargetZone;
        if(zone)changed|=slider(area,96,"Padding","focus_padding",focus.adaptive.padding_percent,0,100,"%.0f%% height");
        else changed|=slider(area,96,"Radius","focus_radius_degrees",focus.adaptive.radius_degrees,1,25,"%.1f deg");
        char retention[80]{};
        if(zone)snprintf(retention,sizeof(retention),"Retention padding: %.1f%% height",focus.adaptive.padding_percent*1.3f);
        else snprintf(retention,sizeof(retention),"Retention: %.1f deg",focus.adaptive.radius_degrees*1.3f);
        text(area.Min+px(20,163),retention,true,12);
        const float area_column=(area.GetWidth()-px(40))/2;
        const bool zone_area=focus.adaptive.area==AdaptiveAreaMode::TargetZone;
        text(area.Min+px(20,202),zone_area?"Show zone":"Show FOV",true,14);
        changed|=switch_toggle("show_focus_area",area.Min+ImVec2(px(20)+area_column-px(switch_w+12),px(196)),zone_area?&focus.adaptive.show_target_zone:&focus.adaptive.show_area);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",zone_area?"Preview the nearest eligible target. Gold marks the retained target; the faint outline is its retention zone.\nPadding controls activation. Free movement still uses the original hitboxes.":"Draw the focus area. Each area mode remembers its drawing setting.");
        text(area.Min+ImVec2(px(20)+area_column,px(202)),"Focus visuals",true,14);
        changed|=switch_toggle("focus_visuals",{area.Max.x-px(20+switch_w),area.Min.y+px(196)},&focus.focus_visuals);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Focus visuals: draw the selected point and free-movement geometry.");
        if(!ultimate) {
            changed|=row_toggle(prediction,49,"Prediction",adaptive?"adaptive_prediction":"prediction",adaptive?focus.adaptive.prediction:focus.prediction);
            char prediction_summary[160]{};
            if(adaptive) {
                snprintf(prediction_summary,sizeof(prediction_summary),"%s",!focus.adaptive.prediction?"Prediction disabled":scoped?"Assassinate / data-age compensation":snap.weapon.valid?"Automatic / live weapon speed":"Waiting for live weapon data");
            }else if(focus.auto_speed&&snap.weapon.valid)snprintf(prediction_summary,sizeof(prediction_summary),"Live bullet speed: %.0f units/s",snap.weapon.speed);
            else snprintf(prediction_summary,sizeof(prediction_summary),focus.auto_speed?"Waiting for live weapon speed":"Manual bullet speed: %.0f units/s",focus.projectile_speed);
            dl->PushClipRect(prediction.Min+px(20,80),{prediction.Max.x-px(20),prediction.Min.y+px(100)},true);
            text(prediction.Min+px(20,84),prediction_summary,true,12);dl->PopClipRect();
            ImGui::SetCursorScreenPos(prediction.Min+px(20,105));
            if(adaptive) {
                if(ImGui::Button("Advanced",px(105,30)))ImGui::OpenPopup("Adaptive advanced");
            }else if(ImGui::Button("Prediction settings",px(180,30)))ImGui::OpenPopup("Prediction settings");
            ImGui::SetNextWindowSizeConstraints(px(390,0),px(460,FLT_MAX));
            if(ImGui::BeginPopup("Prediction settings")) {
                changed|=ImGui::Checkbox("Prediction",&focus.prediction);
                changed|=ImGui::Checkbox("Auto weapon speed",&focus.auto_speed);
                if(focus.auto_speed) {
                    if(snap.weapon.valid) {
                        ImGui::Text("Live bullet speed: %.0f units/s",snap.weapon.speed);
                        ImGui::Text("Base %.0f / bonus %+.0f%%",snap.weapon.base_speed,snap.weapon.bonus_percent);
                        ImGui::Text("Movement inheritance: %.2f",snap.weapon.inheritance);
                    }else ImGui::TextWrapped("%s",snap.weapon.status.c_str());
                }else {
                    ImGui::SetNextItemWidth(px(340));
                    changed|=ImGui::SliderFloat("##projectile_speed",&focus.projectile_speed,1000.f,100000.f,"Bullet speed: %.0f units/s");
                    changed|=ImGui::Checkbox("Inherit shooter velocity",&focus.inherit_velocity);
                }
                ImGui::EndPopup();
            }
        }else {
            text(movement.Min+px(20,294),"Hitscan / no projectile travel lead",true,12);
            text(movement.Min+px(20,318),"Sample-age compensation is automatic",true,12);
            if(adaptive) {
                ImGui::SetCursorScreenPos(movement.Min+px(20,352));
                if(ImGui::Button("Advanced response",px(180,30)))ImGui::OpenPopup("Adaptive advanced");
            }
        }
        ImGui::SetNextWindowSizeConstraints(px(390,0),{px(460),ImGui::GetIO().DisplaySize.y-px(48)});
        if(ImGui::BeginPopup("Adaptive advanced")) {
            draw_adaptive_response_info(snap);
            ImGui::Text("X1 response: %.1f ms / X2: %.1f ms",adaptive_response_ms(focus.adaptive.speed),adaptive_response_ms(focus.adaptive_speed_x2));
            ImGui::Separator();ImGui::SetNextItemWidth(px(340));
            changed|=ImGui::SliderFloat("##adaptive_max_rate",&focus.adaptive.max_rate,30.f,720.f,"Requested step rate: %.0f deg/s");
            ImGui::SetNextItemWidth(px(340));changed|=ImGui::SliderFloat("##adaptive_max_acceleration",&focus.adaptive.max_acceleration,300.f,6000.f,"Acceleration: %.0f deg/s2");
            if(ultimate==app.ult_profile_active) {
                const auto& solution=app.adaptive.solution;ImGui::Separator();ImGui::TextWrapped("%s",app.adaptive.status.c_str());
                if(focus.adaptive.area==AdaptiveAreaMode::TargetZone)draw_zone_diagnostics(app.adaptive);
                ImGui::Text("Region: %s / motion: %s",region_name(solution.region),motion_source_name(solution.motion_source));
                ImGui::Text("Age: %.1f ms / flight: %.1f ms",solution.data_age_ms,solution.flight_time*1000);ImGui::TextWrapped("%s",solution.prediction_status);
            }
            ImGui::EndPopup();
        }
        const char* state=ultimate&&!app.settings.vindicta_ult_override?"Ultimate override disabled / enable it in Profile options"
            :ultimate&&!scoped?"Waiting for Vindicta's Assassinate scope"
            :!ultimate&&ult_active?"Vindicta Ultimate profile active / editing regular controls"
            :!focus.replay_focus?"Focus disabled":!app.makcu.ready()?"Waiting for mouse input"
            :app.target_lock.active()?app.target_lock.status.c_str()
            :adaptive?app.adaptive.status.c_str()
            :focus.visible_only&&!snap.visibility?"Waiting for map collision mesh":app.camera.occluded?"Focus point blocked"
            :app.camera.input_blocked?"Input failed / release the hold button to retry"
            :app.camera.lost?app.camera.selection_status:scoped?"Assassinate hitscan / projectile prediction off"
            :focus.prediction?(focus.auto_speed&&!snap.weapon.valid?"Prediction waiting for live weapon data":app.camera.prediction_active?"Prediction active":"Prediction waiting for valid motion / intercept"):"Prediction off";
        dl->PushClipRect(origin+px(0,522),body.Max,true);text(origin+px(0,526),state,true,12);dl->PopClipRect();
        ImGui::PopStyleVar();ImGui::PopID();
    } else if(page==shell_page::auto_click) {
        changed|=draw_auto_click_settings(static_cast<CameraFocusSettings&>(app.settings),app.settings.vindicta_ult,
            app.click_ultimate,app.ult_profile_active,app.ray_click,app.makcu,{origin,origin+body.GetSize()},alpha,app.settings.transparent_frame);
    } else if(page==shell_page::auto_parry) {
        changed|=draw_auto_parry_settings(app.settings.auto_parry,app.auto_parry,snap.parry,
            {origin,origin+body.GetSize()},alpha,app.settings.transparent_frame);
    } else if(page==shell_page::test) {
        changed|=draw_session_monitor(app.session_monitor,snap,{origin,origin+body.GetSize()},alpha,
            app.settings.transparent_frame,app.settings.show_monitor_hud,app.makcu.ready(),app.makcu.status(),
            app.reports_directory,app.settings_window.hwnd);
    } else {
        ImRect status(origin+px(0,60),{body.Max.x,origin.y+px(187)});card(status,"GAME CONNECTION",icons::id::workflow);        badge("connected",dl,status.Min+px(20,45),snap.pid?"Connected":"Waiting for game",snap.pid?badge_good:badge_warn);
        char timing[160]{};snprintf(timing,sizeof(timing),"%.0f Hz / %.0f FPS / %.3f ms reads / %zu players",app.target_hz,app.render_fps,snap.sample_us/1000,snap.players.size());
        text(status.Min+px(20,79),timing,true,14);
        text(status.Min+px(20,105),snap.status.c_str(),true,12);
        float split=body.GetWidth()*.52f;ImRect map(origin+px(0,203),origin+ImVec2(split,px(365)));
        ImRect input({map.Max.x+px(16),map.Min.y},{body.Max.x,map.Max.y});
        card(map,"MAP VISIBILITY",icons::id::eye);card(input,"INPUT DEVICE",icons::id::crosshair_simple);
        text(map.Min+px(20,47),snap.map_name.empty()?"Waiting for map":snap.map_name.c_str(),false,14);
        text(map.Min+px(20,76),snap.visibility_status.c_str(),true,12);
        char mesh[128]{};snprintf(mesh,sizeof(mesh),"%zu triangles / %.3f ms checks",snap.visibility?snap.visibility->triangle_count():0,snap.visibility_us/1000);
        text(map.Min+px(20,103),mesh,true,12);
        char projection_label[120]{};
        if(snap.projection.valid)snprintf(projection_label,sizeof(projection_label),"View %.1f x %.1f deg / aspect %.3f",snap.projection.horizontal_fov,snap.projection.vertical_fov,snap.projection.aspect);
        else snprintf(projection_label,sizeof(projection_label),"Waiting for camera projection");
        text(map.Min+px(20,130),projection_label,true,12);
        text(input.Min+px(20,47),app.makcu.status().c_str(),false,12);
        char targets[128]{};int minions=0,orbs=0;
        for(const auto& target:snap.focus_targets)if(target.kind==TargetKind::Minion)++minions;else if(target.kind==TargetKind::SoulOrb)++orbs;
        snprintf(targets,sizeof(targets),"%zu players / %d minions / %d orbs",snap.players.size(),minions,orbs);
        text(input.Min+px(20,76),targets,true,12);
        text(input.Min+px(20,103),snap.replay?"Replay mode":snap.practice?"Sandbox / bots":"Live match",true,12);
        text(input.Min+px(20,130),"Camera controls are on the Camera Focus tab",true,12);
        ImRect updates(origin+px(0,381),{body.Max.x,origin.y+px(532)});
        card(updates,"GAME DATA",icons::id::workflow);
        auto& updater=app.data_update;
        auto action=[&](const char* id,const char* label,ImVec2 at,float width,bool disabled=false){
            ImGui::BeginDisabled(disabled);auto state=ui_runtime::animation_state<stateful_button_state>(ImGui::GetID(id));
            stateful_button_update(*state,btn_idle,label,ImGui::GetIO().DeltaTime);
            bool clicked=stateful_button_draw(id,*state,btn_idle,at,px(width),false);ImGui::EndDisabled();return clicked&&!disabled;
        };
        auto buttons=updates.Min+px(20,44);
        if(action("update_all","Update all data",buttons,148,updater.running()))updater.start(L"all",app.settings_window.hwnd);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Refresh maps, dump schema and resolve a new reader profile.\nLoad sandbox, bots or replay with a nearby player for live validation.\nThe reader reloads after a successful update. Changed private layouts may need review.");
        if(action("install_maps","Install all maps",buttons+px(160,0),148,updater.running()))updater.start(L"maps",app.settings_window.hwnd);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Discover installed map VPKs, verify current meshes and export new/changed maps.\nNo administrator approval or overlay rebuild needed.");
        if(action("dump_schema","Dump schema",buttons+px(320,0),128,updater.running()))updater.start(L"schema",app.settings_window.hwnd);
        if(updater.running()) {
            if(action("cancel_update","Cancel",{updates.Max.x-px(94),buttons.y},74))updater.cancel();
        } else if(action("update_report","Report",{updates.Max.x-px(94),buttons.y},74))updater.open_report(app.settings_window.hwnd);
        dl->PushClipRect(updates.Min+px(20,80),updates.Max-px(20,9),true);
        text(updates.Min+px(20,98),updater.message.c_str(),true,12);
        if(updater.running()) {
            ImGui::SetCursorScreenPos(updates.Min+px(20,118));
            ImGui::ProgressBar(float(updater.progress)/100.f,{updates.GetWidth()-px(40),px(18)},updater.phase.c_str());
        } else text(updates.Min+px(20,122),"Maps and reader reload automatically. Reports include validation details.",true,12);
        dl->PopClipRect();
    }
    if(changed)save(app.settings);ImGui::PopStyleColor();ui_runtime::pop_font();
}
void draw_settings(Application& app,const Snapshot& snap) {
    using namespace solace;
    theme_tick(ImGui::GetIO().DeltaTime);
    PageContext data{app,snap};shell_extension extension;
    extension.content=overlay_page;extension.context=&data;extension.status=snap.status.c_str();
    extension.close=[](void* context){static_cast<PageContext*>(context)->app.hide_settings();};
    extension.active_page=&app.menu_page;
    extension.frame_opacity=app.settings.transparent_frame?app.settings.frame_opacity:1.f;
    extension.appearance=[](void* context) {
        auto& settings=static_cast<PageContext*>(context)->app.settings;
        ui_runtime::push_font(font_semibold(16));
        bool changed=ImGui::Checkbox("Transparent frame",&settings.transparent_frame);
        if(settings.transparent_frame) {
            ImGui::Text("Frame opacity  %.0f%%",settings.frame_opacity*100);
            changed|=range_slider("frame_opacity",ImGui::GetCursorScreenPos(),px(248),&settings.frame_opacity,.4f,.9f);
        }
        ImGui::TextDisabled("Sidebar, header, empty frame and enemy tags");
        ImGui::Separator();changed|=ImGui::Checkbox("Mid Boss HUD",&settings.show_midboss_hud);
        ImGui::Separator();changed|=ImGui::Checkbox("Glass cursor",&settings.glass);
        ui_runtime::pop_font();
        if(changed)save(settings);
    };
    menu_screen(1.f,&extension);
    ImRect viewport({0,0},ImGui::GetIO().DisplaySize);
    theme_reveal_draw(ImGui::GetForegroundDrawList(),viewport);
    glass::enabled()=app.settings.glass;
    if(glass::enabled()) {
        auto options=glass::settings();options.opacity=.9f;options.border_glow=.3f;options.specular_gain=.5f;
        glass::cursor(ImGui::GetForegroundDrawList(),viewport,options);
    }
    POINT pointer{};GetCursorPos(&pointer);ScreenToClient(app.settings_window.hwnd,&pointer);
    if(GetForegroundWindow()==app.settings_window.hwnd&&pointer.x>=0&&pointer.y>=0&&pointer.x<viewport.Max.x&&pointer.y<viewport.Max.y)
        SetCursor(glass::enabled()&&glass::cursor_live()?nullptr:LoadCursorW(nullptr,IDC_ARROW));
    if(app.settings.dark!=is_dark()){app.settings.dark=is_dark();save(app.settings);}
    ui_runtime::collect_animation_states();
}
HWND game_window(DWORD pid) {
    struct Find {DWORD pid;HWND hwnd;} find{pid,nullptr};
    EnumWindows([](HWND w,LPARAM l)->BOOL {
        auto& f=*reinterpret_cast<Find*>(l);DWORD p{};GetWindowThreadProcessId(w,&p);
        if(p==f.pid&&IsWindowVisible(w)&&GetWindow(w,GW_OWNER)==nullptr){f.hwnd=w;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&find));return find.hwnd;
}
DWORD game_process_id() {
    HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(list==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);DWORD pid{};
    if(Process32FirstW(list,&entry))do{if(!_wcsicmp(entry.szExeFile,L"deadlock.exe")){pid=entry.th32ProcessID;break;}}while(Process32NextW(list,&entry));
    CloseHandle(list);return pid;
}
bool capture(Surface& s,const std::filesystem::path& path,bool require_transparency=false) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Texture2D> buffer,stage;
    if(FAILED(s.renderer.swap_chain()->GetBuffer(0,IID_PPV_ARGS(&buffer))))return false;
    D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    if(FAILED(s.renderer.device()->CreateTexture2D(&desc,nullptr,&stage)))return false;
    s.renderer.context()->CopyResource(stage.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE data{};
    if(FAILED(s.renderer.context()->Map(stage.Get(),0,D3D11_MAP_READ,0,&data)))return false;
    std::vector<unsigned char> pixels(size_t(desc.Width)*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){
        auto src=static_cast<const unsigned char*>(data.pData)+y*data.RowPitch+x*4;auto dst=pixels.data()+(size_t(y)*desc.Width+x)*4;
        dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];
    }
    s.renderer.context()->Unmap(stage.Get(),0);
    if(require_transparency) {
        if(pixels[3]!=0)return false;
        bool content=false;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]>0){content=true;break;}if(!content)return false;
    }
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
       ||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))
       ||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))
       ||FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(desc.Width,desc.Height)))return false;
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA)return false;
    return SUCCEEDED(frame->WritePixels(desc.Height,desc.Width*4,static_cast<UINT>(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
}
int menu_previews(const std::filesystem::path& directory) {
    // Render the real menu into hidden surfaces. No game reader, input worker,
    // serial port, settings write or desktop interaction is used by this path.
    menu_preview_mode=true;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::filesystem::create_directories(directory);Application app;
    app.settings=Settings{};app.settings.glass=false;app.settings.transparent_frame=false;
    app.settings.free_focus=true;app.settings.free_focus_mode=FreeMovementMode::Adaptive;
    app.settings.auto_click=true;app.settings.auto_click_orbs_only=true;
    sanitize_camera_settings(app.settings);
    app.settings.vindicta_ult=assassinate_controls(static_cast<const CameraFocusSettings&>(app.settings));
    app.settings.skeletons=app.settings.hitboxes=app.settings.hidden_enemy_tags=true;
    app.ray_click.status="Preview / waiting for a claimable orb";
    Snapshot snapshot;snapshot.status="Menu preview / no game input";
    snapshot.hero.valid=true;snapshot.hero.id=3;snapshot.hero.name="Vindicta";snapshot.hero.token="hero_hornet";
    snapshot.hero.sniper_present=snapshot.hero.sniper_valid=true;snapshot.hero.ability_status="Assassinate idle";
    snapshot.projection={true,71.1f,43.8f,16.f/9};snapshot.weapon.valid=true;snapshot.weapon.speed=25984;
    bool ok=create_surface(app,app.settings_window,false);
    auto render=[&](solace::shell_page page,const wchar_t* name) {
        app.menu_page=page;app.page=int(page);app.page_time=1.f;
        for(int i=0;i<16&&ok;++i) {
            ok=begin_frame(app.settings_window);
            if(ok) {
                auto& io=ImGui::GetIO();io.DeltaTime=1.f/30;io.MousePos={-FLT_MAX,-FLT_MAX};
                for(auto& down:io.MouseDown)down=false;
                draw_settings(app,snapshot);render_frame(app.settings_window);
            }
        }
        if(ok)ok=capture(app.settings_window,directory/name);
    };
    if(ok) {
        render(solace::shell_page::auto_click,L"auto-click-orbs.png");
        render(solace::shell_page::camera,L"camera-focus.png");
        app.settings.x2_target_lock=true;
        render(solace::shell_page::camera,L"camera-x2-lock.png");
        app.settings.x2_target_lock=false;
        app.settings.free_focus=false;
        render(solace::shell_page::camera,L"camera-focus-fixed.png");
        app.settings.adaptive.area=AdaptiveAreaMode::TargetZone;
        render(solace::shell_page::camera,L"camera-focus-zone.png");
        app.settings.free_focus=true;app.settings.adaptive.area=AdaptiveAreaMode::CrosshairAngle;
        app.ult_profile_active=true;snapshot.hero.sniper_scoped=true;snapshot.hero.ability_status="Assassinate scoped";
        render(solace::shell_page::vindicta_ult,L"vindicta-ultimate.png");
        app.settings.vindicta_ult.x2_target_lock=true;
        render(solace::shell_page::vindicta_ult,L"vindicta-x2-lock.png");
        app.settings.vindicta_ult.x2_target_lock=false;
        app.settings.vindicta_ult.free_focus=false;
        render(solace::shell_page::vindicta_ult,L"vindicta-ultimate-fixed.png");
        app.ult_profile_active=false;snapshot.hero.sniper_scoped=false;
        render(solace::shell_page::health,L"health-bars.png");
        app.settings.healthbar_debug_box=true;
        render(solace::shell_page::health,L"health-bars-debug.png");
        render(solace::shell_page::auto_parry,L"auto-parry.png");
        app.settings.auto_click_orbs_only=false;app.ray_click.status="Preview / hold either side button";
        render(solace::shell_page::auto_click,L"auto-click-players.png");
    }
    app.settings_window.shutdown();CoUninitialize();return ok?0:2;
}
int render_test() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Application app;app.settings=Settings{};
    app.menu_page=solace::shell_page::health;app.settings.transparent_frame=false;
    bool ok=create_surface(app,app.settings_window,false)&&create_surface(app,app.bar_window,true);
    if(ok) {
        const auto bars_ex=GetWindowLongPtrW(app.bar_window.hwnd,GWL_EXSTYLE);
        const auto menu_ex=GetWindowLongPtrW(app.settings_window.hwnd,GWL_EXSTYLE);
        const LONG_PTR required=WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED;
        ok=(bars_ex&required)==required && (bars_ex&WS_EX_TRANSPARENT) && (bars_ex&WS_EX_NOACTIVATE)
            && (menu_ex&required)==required && !(menu_ex&(WS_EX_APPWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))
            && !(GetWindowLongPtrW(app.settings_window.hwnd,GWL_STYLE)&WS_CAPTION)
            && SendMessageW(app.bar_window.hwnd,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE
            && SendMessageW(app.settings_window.hwnd,WM_MOUSEACTIVATE,0,0)==MA_ACTIVATE
            && SendMessageW(app.bar_window.hwnd,WM_NCHITTEST,0,0)==HTTRANSPARENT;
        Snapshot s;s.status="Connected to validated client.dll";s.pid=6248;s.controllers=3;s.players.resize(2);
        s.hero.valid=true;s.hero.id=3;s.hero.name="Vindicta";s.hero.token="hero_hornet";s.hero.sniper_present=s.hero.sniper_valid=s.hero.sniper_scoped=true;s.hero.ability_status="Assassinate scoped";s.projection={true,71.1f,43.8f,16.f/9};
        // Two frames allow the dynamic font atlas to upload fonts first requested by widgets.
        ok=ok&&app.settings_window.panel_shader&&app.settings_window.glass_shader;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;ImGui::GetIO().MousePos={960.f+float(i),500.f};draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=solace::snapshot::backdrop_ready()&&solace::glass::cursor_live();
        if(ok)ok=capture(app.settings_window,executable_directory()/L"settings-preview.png",true);
        app.settings.skeletons=app.settings.hitboxes=true;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"marker-settings-preview.png",true);
        app.menu_page=solace::shell_page::camera;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"camera-settings-preview.png",true);
        app.settings.free_focus=true;app.settings.free_focus_mode=FreeMovementMode::Hitboxes;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"free-movement-v2-preview.png",true);
        app.settings.focus_hold_button=FocusHoldButton::LeftMouse;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"left-mouse-focus-preview.png",true);
        app.settings.focus_hold_button=FocusHoldButton::SideButtons;
        app.settings.free_focus=false;app.settings.free_focus_mode=FreeMovementMode::BoneLine;
        app.menu_page=solace::shell_page::connection;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"connection-settings-preview.png",true);
        app.settings.transparent_frame=true;app.menu_page=solace::shell_page::camera;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"transparent-camera-preview.png",true);
        // Exercise the hero popup without sending input to the desktop/game.
        for(int i=0;i<8&&ok;++i) {
            ImGui::GetIO().AddMousePosEvent(solace::px(549),solace::px(234));
            if(i==1||i==2)ImGui::GetIO().AddMouseButtonEvent(0,i==1);
            ok=begin_frame(app.settings_window);
            if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}
        }
        if(ok)ok=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)&&capture(app.settings_window,executable_directory()/L"hero-abilities-preview.png",true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);
        if(ok){ok=begin_frame(app.settings_window);if(ok){draw_settings(app,s);render_frame(app.settings_window);}}
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);
        // Open the area popup through ImGui input without touching the desktop mouse.
        for(int i=0;i<8&&ok;++i) {
            ImGui::GetIO().AddMousePosEvent(solace::px(846),solace::px(234));
            if(i==1||i==2)ImGui::GetIO().AddMouseButtonEvent(0,i==1);
            ok=begin_frame(app.settings_window);
            if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}
        }
        if(ok)ok=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)&&capture(app.settings_window,executable_directory()/L"focus-area-settings-preview.png",true);
        for(int mode=1;mode<3&&ok;++mode) {
            app.settings.adaptive.area=static_cast<AdaptiveAreaMode>(mode%2);
            for(int i=0;i<8&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
            if(ok)ok=capture(app.settings_window,executable_directory()/(mode==1?L"focus-box-settings-preview.png":L"focus-circle-settings-preview.png"),true);
        }
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);
        if(ok){ok=begin_frame(app.settings_window);if(ok){draw_settings(app,s);render_frame(app.settings_window);}}
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);
        app.menu_page=solace::shell_page::health;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"transparent-health-preview.png",true);
        app.menu_page=solace::shell_page::camera;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        // Exercise the header button without moving or clicking the desktop mouse.
        for(int i=0;i<8&&ok;++i){
            ImGui::GetIO().AddMousePosEvent(solace::px(1012),solace::px(56));
            if(i==1||i==2)ImGui::GetIO().AddMouseButtonEvent(0,i==1);
            ok=begin_frame(app.settings_window);
            if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}
        }
        if(ok)ok=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)
            &&capture(app.settings_window,executable_directory()/L"appearance-settings-preview.png",true);
        SetWindowPos(app.bar_window.hwnd,nullptr,0,0,320,160,SWP_NOACTIVATE|SWP_NOZORDER);
        for(int i=0;i<3&&ok;++i){ok=begin_frame(app.bar_window);if(ok){draw_vertical_bar(ImGui::GetBackgroundDrawList(),vertical_healthbar_rect({140,10,180,110},8),740,1000,Settings{},1);render_frame(app.bar_window);}}
        if(ok)ok=capture(app.bar_window,executable_directory()/L"bar-preview.png",true);
        if(ok){ok=begin_frame(app.bar_window);if(ok){Settings demo;draw_vertical_bar(ImGui::GetBackgroundDrawList(),vertical_healthbar_rect({150,10,190,90},8),740,1000,demo,1);for(int i=0;i<3;++i)draw_dot(ImGui::GetBackgroundDrawList(),{125,55.f+35.f*i},i,1);render_frame(app.bar_window);ok=capture(app.bar_window,executable_directory()/L"markers-preview.png",true);}}
        SetWindowPos(app.bar_window.hwnd,nullptr,0,0,640,360,SWP_NOACTIVATE|SWP_NOZORDER);
        Player shapes;
        for(auto model:std::array<ModelHitbox,3>{{{{-18,-26,-12},{18,26,12},0,-1,2,HitboxShape::Box},{{0,-30,0},{0,30,0},15,-1,3,HitboxShape::Capsule},{{0,0,0},{0,0,0},25,-1,1,HitboxShape::Sphere}}}) {
            const float x=shapes.hitboxes.empty()?-100.f:shapes.hitboxes.size()==1?0.f:100.f;
            Hitbox box;if(world_hitbox(model,{x,0,350,1,0,0,.258819f,.965926f},box)){box.visibility_known=true;box.visible=shapes.hitboxes.size()!=1;shapes.hitboxes.push_back(box);}
        }
        Matrix shape_projection{.5625f,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};
        for(int i=0;i<3&&ok;++i){ok=begin_frame(app.bar_window);if(ok){ok=draw_hitboxes(ImGui::GetBackgroundDrawList(),shapes,shape_projection,640,360,Settings{},1)==3;render_frame(app.bar_window);}}
        if(ok)ok=capture(app.bar_window,executable_directory()/L"hitboxes-preview.png",true);
        Snapshot areas;areas.matrix={.5625f,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};areas.players.resize(2);
        for(int i=0;i<2;++i){auto& target=areas.players[i];target.handle=uint32_t(i+1);target.dot_valid.fill(true);target.dots.fill({i==0?250.f:-650.f,0,i==0?1800.f:3200.f});}
        Settings area_demo;area_demo.adaptive.show_area=area_demo.adaptive.show_target_zone=true;area_demo.focus_visuals=false;area_demo.visible_only=false;
        for(int mode=0;mode<3&&ok;++mode) {
            area_demo.adaptive.area=static_cast<AdaptiveAreaMode>(mode%2);
            for(int i=0;i<3&&ok;++i){ok=begin_frame(app.bar_window);if(ok){auto draw=ImGui::GetBackgroundDrawList();draw_focus_area(draw,areas,area_demo,640,360,1,1);draw->AddLine({314,180},{326,180},IM_COL32_WHITE);draw->AddLine({320,174},{320,186},IM_COL32_WHITE);render_frame(app.bar_window);}}
            if(ok)ok=capture(app.bar_window,executable_directory()/(mode==0?L"center-fov-preview.png":mode==1?L"target-box-preview.png":L"target-circle-preview.png"),true);
        }
    }
    std::ofstream out(executable_directory()/L"renderer-test.json");out<<"{\"passed\":"<<(ok?"true":"false")<<",\"background_alpha_zero\":"<<(ok?"true":"false")<<"}\n";
    return ok?0:1;
}
int resource_test() {
    struct Usage {SIZE_T memory{};DWORD handles{},gdi{},user{};};
    auto usage=[] {Usage u;PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p));u.memory=p.PrivateUsage;GetProcessHandleCount(GetCurrentProcess(),&u.handles);u.gdi=GetGuiResources(GetCurrentProcess(),0);u.user=GetGuiResources(GetCurrentProcess(),1);return u;};
    bool ok=true;Usage before{},after{};int cycles{};
    for(int cycle=0;cycle<3&&ok;++cycle) {
        Application app;app.settings=Settings{};
        ok=create_surface(app,app.settings_window,false)&&create_surface(app,app.bar_window,true);
        Snapshot s;s.pid=1;s.local_team=2;s.players.resize(2);s.matrix={.5625f,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};
        for(int i=0;i<2;++i){s.players[i].handle=uint32_t(i+1);s.players[i].dot_valid.fill(true);s.players[i].dot_visible.fill(true);s.players[i].visible=true;s.players[i].dots.fill({i==0?250.f:-650.f,0,i==0?1800.f:3200.f});}
        app.settings.adaptive.show_area=app.settings.adaptive.show_target_zone=true;
        SetWindowPos(app.bar_window.hwnd,nullptr,0,0,640,360,SWP_NOACTIVATE|SWP_NOZORDER);
        for(int i=0;i<2200&&ok;++i) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            app.settings.glass=(i/50)%2==0;
            app.settings.transparent_frame=(i/100)%2==0;
            app.menu_page=static_cast<solace::shell_page>((i/50)%3);
            app.settings.adaptive.area=static_cast<AdaptiveAreaMode>((i/50)%2);
            if(i==0||(i>=200&&i%50==0))ShowWindow(app.settings_window.hwnd,(i/50)%2?SW_HIDE:SW_SHOWNOACTIVATE);
            ok=begin_frame(app.settings_window);
            if(ok){ImGui::GetIO().DeltaTime=1.f/240;ImGui::GetIO().MousePos={960,500};draw_settings(app,s);ok=end_frame(app.settings_window);}
            if(ok){ok=begin_frame(app.bar_window);if(ok){draw_focus_area(ImGui::GetBackgroundDrawList(),s,app.settings,640,360,1,1);ok=end_frame(app.bar_window);}}
            // Allow deferred Windows text/input and driver initialization to settle before measuring growth.
            if(i<200)Sleep(10);
            if(i==1199)before=usage();
        }
        after=usage();
        if(after.handles>before.handles+8||after.gdi>before.gdi+2||after.user>before.user+2||after.memory>before.memory+16*1024*1024)ok=false;
        app.settings_window.shutdown();app.bar_window.shutdown();++cycles;
    }
    std::ofstream out(executable_directory()/L"resource-test.json");
    out<<"{\"passed\":"<<(ok?"true":"false")<<",\"cycles\":"<<cycles<<",\"frames_per_cycle\":2200,\"warmup_frames\":1200,\"private_before\":"<<before.memory<<",\"private_after\":"<<after.memory<<",\"handles_before\":"<<before.handles<<",\"handles_after\":"<<after.handles<<",\"gdi_before\":"<<before.gdi<<",\"gdi_after\":"<<after.gdi<<",\"user_before\":"<<before.user<<",\"user_after\":"<<after.user<<"}\n";
    return ok?0:1;
}
int run(bool diagnostics,bool settings_at_start,bool shutdown_test=false,bool trace_frames=false,bool test_tab_at_start=false) {
    if(!shutdown_test) {
        if(!SetPriorityClass(GetCurrentProcess(),high_priority?HIGH_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS))
            std::cerr<<"Could not apply process priority: "<<GetLastError()<<"\n";
    }
    HANDLE single=CreateMutexW(nullptr,FALSE,shutdown_test?L"Local\\DeadlockHealthOverlayTestSingleton":L"Local\\DeadlockHealthOverlaySingleton");
    if(!single)return 1;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(single);return 0;}
    shutdown_complete=CreateEventW(nullptr,TRUE,FALSE,nullptr);SetConsoleCtrlHandler(console_handler,TRUE);
    SetConsoleTitleW(L"Deadlock Overlay - close console to exit");
    std::cout<<"Deadlock Overlay\nInsert: toggle menu. Drag menu header to move. Close this console to exit.\n";
    Application app;app.diagnostics=diagnostics;save(app.settings);
    if(test_tab_at_start)app.menu_page=solace::shell_page::test;
    app.data_update.initialize(executable_directory());
    if(!create_surface(app,app.settings_window,false)||!create_surface(app,app.bar_window,true)) {
        MessageBoxW(nullptr,L"Overlay window or Direct3D initialization failed.",L"Deadlock Overlay",MB_ICONERROR);CloseHandle(single);return 1;
    }
    if(!shutdown_test)app.makcu.start();
    // Wait before preparing render frames; steering is processed before GPU pacing.
    std::jthread reader([&](std::stop_token stop) {
        GameReader game;FramePacer sample_pacer;unsigned revision{};while(!stop.stop_requested()) {
            auto next_revision=app.data_revision.load();if(next_revision!=revision){game.refresh_data();revision=next_revision;}
            auto flags=app.read_options.load();auto s=game.sample({(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0,(flags&16)!=0,(flags&32)!=0,(flags&64)!=0,(flags&128)!=0,(flags&256)!=0,(flags&512)!=0});
            {
                std::lock_guard lock(app.mutex);
                // An active renderer update is a seqlock retry, not an invalid
                // measured camera. Keep the last coherent sample for at most
                // 50 ms; genuine invalid reads still publish immediately.
                const bool retry=s.projection_pending&&s.pid==app.snapshot.pid&&app.snapshot.time
                    &&motion_clock_ms()-app.snapshot.projection_ms<=50;
                if(!retry)app.snapshot=std::move(s);
            }
            sample_pacer.wait(app.sampling?app.sample_hz.load():5.0);
        }
    });
    std::jthread close_test;
    if(shutdown_test)close_test=std::jthread([] {Sleep(500);console_handler(CTRL_CLOSE_EVENT);});
    if(settings_at_start)app.open_settings();
    bool insert_down=false;uint64_t last_report{},next_display_check{},fps_epoch=GetTickCount64();
    uint64_t rendered_frames{};FramePacer render_pacer;GameFrames game_frames;
    if(trace_frames&&!shutdown_test)game_frames.start();
    HWND cached_game{};DWORD cached_pid{},observed_reader_pid{};uint64_t next_window_check{};
    double work_sum{},wait_sum{},submit_sum{},present_sum{},queue_wait_sum{},work_max{},present_max{};uint64_t timing_frames{};
    while(!app.quit&&!shutdown_requested.load()) {
        auto loop_start=clock_ms();app.bar_window.submit_ms=app.bar_window.present_ms=app.settings_window.submit_ms=app.settings_window.present_ms=0;
        app.data_update.poll();if(app.data_update.consume_finished())++app.data_revision;
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)app.quit=true;TranslateMessage(&message);DispatchMessageW(&message);}if(app.quit)break;
        Snapshot snapshot;{std::lock_guard lock(app.mutex);snapshot=app.snapshot;}
        if(snapshot.pid!=observed_reader_pid||GetTickCount64()>=next_window_check) {
            observed_reader_pid=snapshot.pid;
            // Keep maintenance controls reachable when a patch blocks the memory reader.
            cached_pid=snapshot.pid?snapshot.pid:game_process_id();cached_game=cached_pid?game_window(cached_pid):nullptr;next_window_check=GetTickCount64()+1000;
        }
        HWND game=cached_game;app.game_hwnd=game;
        game_frames.target(snapshot.pid);app.frame_status=trace_frames?game_frames.status():"Refresh pacing";
        if(GetTickCount64()>=next_display_check) {
            auto timing=display_timing(game);app.target_hz=timing.hz;app.display=timing.device;app.sample_hz=timing.hz;
            next_display_check=GetTickCount64()+1000;
        }
        HWND foreground=GetForegroundWindow();
        bool focused=render_active(game!=nullptr,game&&IsIconic(game),foreground==game,app.show_settings,foreground==app.settings_window.hwnd);
        bool key=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;
        if(key&&!insert_down&&focused){if(app.show_settings)app.hide_settings();else app.open_settings();}insert_down=key;
        foreground=GetForegroundWindow();
        focused=render_active(game!=nullptr,game&&IsIconic(game),foreground==game,app.show_settings,foreground==app.settings_window.hwnd);
        app.sync_input_backend();
        const auto& control=app.focus_profile(snapshot);
        const bool x2_down=focus_button_held(FocusHoldButton::X2);
        const auto activation=resolve_focus_activation(control,x2_down,app.target_lock.active());
        const bool adaptive_active=adaptive_mode(control);
        bool hitbox_focus=control.free_focus&&(control.free_focus_mode==FreeMovementMode::Hitboxes||adaptive_active)
            &&(control.replay_focus||control.focus_visuals||(adaptive_active&&control.adaptive.area==AdaptiveAreaMode::TargetZone&&control.adaptive.show_target_zone))
            &&(control.focus_players||control.focus_minions);
        const bool zone_geometry=control.adaptive.area==AdaptiveAreaMode::TargetZone&&(control.replay_focus||control.adaptive.show_target_zone);
        const bool visual_visibility_geometry=app.settings.hidden_enemy_tags||(app.settings.enabled&&app.settings.hide_visible_healthbars);
        app.read_options.store((app.settings.skeletons?1u:0u)|((control.focus_minions||control.auto_click)?2u:0u)|((control.focus_orbs||(control.auto_click&&control.auto_click_orbs_only))?4u:0u)|((app.settings.hitboxes||hitbox_focus||control.auto_click||visual_visibility_geometry||zone_geometry)?8u:0u)|((adaptive_active||app.ult_scope_active||control.x2_target_lock)?16u:0u)|(app.settings.show_midboss_hud?32u:0u)
            |(app.settings.auto_parry.enabled?64u:0u)|((app.settings.auto_parry.enabled&&app.settings.auto_parry.counterspell)?128u:0u)|(app.settings.hidden_enemy_tags?256u:0u)|((app.settings.enabled||app.settings.healthbar_debug_box)?512u:0u));
        const auto previous_moves=app.camera.moves+app.adaptive.moves+app.target_lock.moves;
        app.adaptive.prepare(snapshot,control);
        // Both input backends appear in Windows button state; do not let an injected
        // pulse become the activation key for left-mouse camera focus.
        const bool injected_hold=activation.button==FocusHoldButton::LeftMouse&&app.makcu.click_pulse_active();
        app.target_lock.update(app.makcu,snapshot,game,control,app.show_settings,app.ult_scope_active,control.replay_focus&&!injected_hold,clock_ms(),activation,x2_down);
        if(app.target_lock.active()) {
            app.camera.release();app.adaptive.release();
        }else if(adaptive_active) {
            app.camera.release();
            app.adaptive.update(app.makcu,snapshot,game,control,app.show_settings,app.ult_scope_active,control.replay_focus&&!injected_hold,clock_ms(),activation);
        }else {
            app.adaptive.release();
            app.camera.update(app.makcu,snapshot,game,control.replay_focus&&!injected_hold,control.debug_focus,control.exclude_teammates,app.show_settings,
                control.focus_bone,activation.speed,control.free_focus,control.prediction&&!app.ult_scope_active,control.projectile_speed,
                control.inherit_velocity,control.auto_speed,control.visible_only,{control.focus_players,control.focus_minions,control.focus_orbs,control.strict_enemy_players},
                focus_area_settings(control),clock_ms(),control.free_focus_mode,activation.button,&app.adaptive.zones(),app.ult_scope_active);
        }
        app.ray_click.update(app.makcu,snapshot,game,control.auto_click,app.show_settings,control.click_interval_ms,app.camera.moves+app.adaptive.moves+app.target_lock.moves!=previous_moves,control.auto_click_orbs_only,control.auto_click_orbs_hold_side);
        app.auto_parry.update(app.makcu,snapshot,game,app.settings.auto_parry,app.show_settings);
        app.sampling=focused;
        // Input and foreground checks run before GPU pacing. A saturated queue is
        // polled with a bounded wait, without dropping a prepared render frame.
        auto& pacing_surface=IsWindowVisible(app.bar_window.hwnd)?app.bar_window:app.settings_window;
        auto queue_wait_start=clock_ms();
        if(focused&&IsWindowVisible(pacing_surface.hwnd)&&!pacing_surface.renderer.wait_for_frame(4))continue;
        auto queue_wait_ms=clock_ms()-queue_wait_start;
        // Keep activation, prediction and the zone outline on the same snapshot.
        // The next loop picks up newer data after the bounded GPU wait.
        const auto& focus=control;
        const bool health_display_active=health_visuals_active(app.settings.health_visuals);
        const bool enemy_tags_active=health_display_active&&app.settings.hidden_enemy_tags;
        const bool health_features_active=health_display_active&&(app.settings.enabled||app.settings.skeletons||app.settings.hitboxes||app.settings.healthbar_debug_box||app.settings.hidden_enemy_tags);
        bool visible=focused&&(health_features_active||app.settings.show_midboss_hud||app.settings.show_monitor_hud||show_focus_area(focus)||(focus.focus_visuals&&(focus.free_focus||focus.replay_focus))
            ||(app.settings.auto_parry.enabled&&(app.settings.auto_parry.show_hud||app.settings.auto_parry.show_threat)))&&fresh(GetTickCount64(),snapshot.time);
        app.sampling=focused;app.bars=app.hidden_tags=0;int drawn_hitboxes=0;bool rendered=false,bar_raised=false;
        if(visible) {
            RECT r{};POINT origin{};if(!GetClientRect(game,&r)||!ClientToScreen(game,&origin)||r.right<=0||r.bottom<=0)visible=false;
            else {
                RECT old{};GetWindowRect(app.bar_window.hwnd,&old);
                if(old.left!=origin.x||old.top!=origin.y||old.right-old.left!=r.right||old.bottom-old.top!=r.bottom) {
                    SetWindowPos(app.bar_window.hwnd,HWND_TOPMOST,origin.x,origin.y,r.right,r.bottom,SWP_NOACTIVATE);
                    bar_raised=true;
                }
                if(!IsWindowVisible(app.bar_window.hwnd)){ShowWindow(app.bar_window.hwnd,SW_SHOWNOACTIVATE);bar_raised=true;}
                if(!begin_frame(app.bar_window)){app.quit=true;break;}
                auto draw=ImGui::GetBackgroundDrawList();float scale=float(GetDpiForWindow(game))/96;
                if(!app.target_lock.active())draw_focus_area(draw,snapshot,focus,float(r.right),float(r.bottom),adaptive_mode(focus)?app.adaptive.target():app.camera.target,scale,app.adaptive.zone(adaptive_mode(focus)?app.adaptive.target():app.camera.target),&app.zone_outline);
                if(health_display_active)for(const auto& player:snapshot.players) {
                    if(enemy_tags_active&&draw_hidden_enemy_tag(draw,snapshot,player,float(r.right),float(r.bottom),app.settings,scale))++app.hidden_tags;
                    if(!enemy_teams(snapshot.local_team,player.team))continue;
                    if(app.settings.skeletons)draw_skeleton(draw,player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale);
                    if(app.settings.hitboxes)drawn_hitboxes+=draw_hitboxes(draw,player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale);
                    if(app.settings.enabled||app.settings.healthbar_debug_box) {
                        BarRect rect,box;
                        if(player_healthbar_rect(player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,rect,&box)) {
                            if(app.settings.healthbar_debug_box&&box.right>box.left&&box.bottom>box.top)
                                draw->AddRect({box.left+.5f,box.top+.5f},{box.right-.5f,box.bottom-.5f},IM_COL32(98,180,255,230),0,0,1.f);
                            if(app.settings.enabled&&enemy_visual_visibility_allows(snapshot,player,app.settings.hide_visible_healthbars)){draw_vertical_bar(draw,rect,player.health,player.maximum,app.settings,scale);++app.bars;}
                        }
                    }
                }
                if(focus.focus_visuals&&app.target_lock.active())
                    drawn_hitboxes+=draw_adaptive_solution(draw,snapshot,app.target_lock.solution,float(r.right),float(r.bottom),scale);
                else if(focus.focus_visuals&&adaptive_mode(focus))
                    drawn_hitboxes+=draw_adaptive_solution(draw,snapshot,app.adaptive.solution,float(r.right),float(r.bottom),scale);
                else if(focus.focus_visuals&&focus.free_focus) {
                    for(const auto& player:snapshot.players) {
                        const bool hitboxes_drawn=health_display_active&&app.settings.hitboxes&&enemy_teams(snapshot.local_team,player.team);
                        drawn_hitboxes+=draw_focus_guide(draw,snapshot,player,focus,app.settings,float(r.right),float(r.bottom),scale,hitboxes_drawn);
                    }
                    for(const auto& object:snapshot.focus_targets)
                        drawn_hitboxes+=draw_focus_guide(draw,snapshot,object,focus,app.settings,float(r.right),float(r.bottom),scale);
                }
                if(focus.focus_visuals&&app.camera.engaged&&!app.camera.lost&&app.camera.point_valid) {
                    ScreenPoint target_point{};
                    if(project(snapshot.matrix,app.camera.aim_point,float(r.right),float(r.bottom),target_point))
                        draw->AddCircle({target_point.x,target_point.y},9*scale,IM_COL32(255,200,70,240),24,2*scale);
                }
                if(app.settings.show_monitor_hud)draw_session_hud(draw,app.session_monitor,float(r.right),scale);
                if(app.settings.show_midboss_hud)draw_midboss_hud(draw,snapshot.midboss,scale);
                if(app.settings.auto_parry.enabled) {
                    if(app.settings.auto_parry.show_hud)draw_auto_parry_hud(draw,app.auto_parry,snapshot.parry,scale);
                    if(app.settings.auto_parry.show_threat)draw_parry_threat(draw,snapshot,app.auto_parry.decision,float(r.right),float(r.bottom),scale);
                }
                if(!end_frame(app.bar_window)){app.quit=true;break;}
                rendered=true;
            }
        }
        if(!visible&&IsWindowVisible(app.bar_window.hwnd))ShowWindow(app.bar_window.hwnd,SW_HIDE);
        if(app.show_settings&&focused) {
            if(!IsWindowVisible(app.settings_window.hwnd))app.open_settings();
            if(GetForegroundWindow()==app.settings_window.hwnd&&GetCapture()!=app.settings_window.hwnd)SetCapture(app.settings_window.hwnd);
            // Raising/resizing the bar window must never put it above the menu.
            if(bar_raised)SetWindowPos(app.settings_window.hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(!begin_frame(app.settings_window)){app.quit=true;break;}draw_settings(app,snapshot);
            if(!end_frame(app.settings_window)){app.quit=true;break;}
            rendered=true;
        }else if(IsWindowVisible(app.settings_window.hwnd)) {
            if(GetCapture()==app.settings_window.hwnd)ReleaseCapture();
            app.settings_window.dragging=false;ShowWindow(app.settings_window.hwnd,SW_HIDE);
        }
        if(rendered&&(visible?app.bar_window.last_presented:app.settings_window.last_presented))++rendered_frames;
        uint64_t now=GetTickCount64();
        if(rendered){auto work=clock_ms()-loop_start;auto present=app.bar_window.present_ms+app.settings_window.present_ms;work_sum+=work;queue_wait_sum+=queue_wait_ms;work_max=std::max(work_max,work);submit_sum+=app.bar_window.submit_ms+app.settings_window.submit_ms;present_sum+=present;present_max=std::max(present_max,present);++timing_frames;}
        if(now-fps_epoch>=1000){app.render_fps=double(rendered_frames)*1000/double(now-fps_epoch);rendered_frames=0;fps_epoch=now;}
        app.session_monitor.observe(snapshot,now,app.render_fps,app.target_hz,app.makcu.queue_delay_ms(),
            focused,rendered,rendered?clock_ms()-loop_start:0.,app.bar_window.present_ms+app.settings_window.present_ms,queue_wait_ms);
        if(diagnostics&&now-last_report>=1000){report(snapshot,executable_directory()/L"session.json",app.bars,IsWindowVisible(app.bar_window.hwnd)!=FALSE,GetWindowLongPtrW(app.bar_window.hwnd,GWL_EXSTYLE),app.bar_window.hwnd,app.show_settings,app.target_hz,app.render_fps,drawn_hitboxes,app.hidden_tags);
            report_x2_lock(snapshot,focus,app.target_lock,activation,executable_directory()/L"camera-lock.json");
            std::ofstream tags_report(executable_directory()/L"enemy-tags.json");
            tags_report<<"{\"enabled\":"<<(app.settings.hidden_enemy_tags?"true":"false")<<",\"hold_only\":"<<(app.settings.health_visuals.hold_only?"true":"false")<<",\"hold_key\":"<<app.settings.health_visuals.key<<",\"health_features_active\":"<<(health_display_active?"true":"false")<<",\"hide_when_visible\":"<<(app.settings.hide_visible_enemy_tags?"true":"false")
                <<",\"active\":"<<(enemy_tags_active?"true":"false")<<",\"drawn\":"<<app.hidden_tags
                <<",\"visibility_ready\":"<<(snapshot.visibility&&snapshot.camera_valid?"true":"false")
                <<",\"transparent_frame\":"<<(app.settings.transparent_frame?"true":"false")<<",\"frame_opacity\":"<<(app.settings.transparent_frame?app.settings.frame_opacity:1.f)<<"}\n";
            std::ofstream parry_report(executable_directory()/L"auto-parry.json");write_parry_data(parry_report,snapshot.parry,app.auto_parry.decision,&app.auto_parry);parry_report<<'\n';
            std::ofstream boss_report(executable_directory()/L"midboss.json");const auto& boss=snapshot.midboss;
            boss_report<<"{\"enabled\":"<<(app.settings.show_midboss_hud?"true":"false")<<",\"valid\":"<<(boss.valid?"true":"false")
                <<",\"phase\":\""<<midboss_phase_name(boss.phase)<<"\",\"status\":\""<<escaped(boss.status)<<"\",\"game_clock\":"<<boss.game_clock
                <<",\"next_spawn\":"<<boss.next_spawn<<",\"remaining\":"<<boss.remaining<<",\"kills\":"<<boss.kills<<",\"paused\":"<<(boss.paused?"true":"false")<<",\"sampled\":"<<boss.sampled<<"}\n";
            app.session_monitor.write_latest(executable_directory()/L"session-monitor.json");
            visibility_report(snapshot,executable_directory()/L"visibility.json",focus.visible_only,adaptive_mode(focus)?app.adaptive.solution.occluded:app.camera.occluded);
            report_adaptive(snapshot,focus,app.adaptive,app.ult_scope_active,app.makcu.queue_delay_ms(),executable_directory()/L"adaptive.json");
            if(adaptive_mode(focus))report_adaptive(snapshot,focus,app.adaptive,app.ult_scope_active,app.makcu.queue_delay_ms(),executable_directory()/L"camera.json");
            std::ofstream click_report(executable_directory()/L"auto-click.json");
            click_report<<"{\"enabled\":"<<(focus.auto_click?"true":"false")<<",\"profile\":\""<<(app.ult_profile_active?"vindicta_ult":"regular")
                <<"\",\"status\":\""<<app.ray_click.status<<"\",\"orbs_only\":"<<(focus.auto_click_orbs_only?"true":"false")<<",\"hold_side\":"<<((!focus.auto_click_orbs_only||focus.auto_click_orbs_hold_side)?"true":"false")<<",\"interval_ms\":"<<focus.click_interval_ms<<",\"clicks_sent\":"<<app.makcu.clicks()<<",\"hit\":";
            write_crosshair_hit(click_report,app.ray_click.hit);click_report<<"}\n";
            std::ofstream makcu_report(executable_directory()/L"makcu.json");
            makcu_report<<"{\"backend\":\""<<app.makcu.backend()<<"\",\"connected\":"<<(app.makcu.hardware_ready()?"true":"false")
                <<",\"movement_ready\":"<<(app.makcu.ready()?"true":"false")
                <<",\"status\":\""<<escaped(app.makcu.status())<<"\",\"commands_sent\":"<<app.makcu.sent()
                <<",\"write_failures\":"<<app.makcu.failures()<<",\"superseded_commands\":"<<app.makcu.replaced()
                <<",\"clicks_sent\":"<<app.makcu.clicks()
                <<",\"motion_commands_sent\":"<<app.makcu.motion_sent()<<",\"windows_moves_sent\":"<<app.makcu.windows_moves()
                <<",\"windows_clicks_sent\":"<<app.makcu.windows_clicks()<<",\"windows_parries_sent\":"<<app.makcu.windows_parries()<<",\"windows_releases_sent\":"<<app.makcu.windows_releases()
                <<",\"windows_input_failures\":"<<app.makcu.windows_failures()<<",\"windows_last_error\":"<<app.makcu.windows_error()
                <<",\"backend_generation\":"<<app.makcu.backend_generation()<<",\"host_queue_delay_ms\":"<<app.makcu.queue_delay_ms()
                <<",\"hold_button\":\""<<focus_button_name(focus.focus_hold_button)<<"\",\"free_movement_available\":true,\"free_movement_mode\":\""<<free_movement_name(focus.free_focus,focus.free_focus_mode)<<"\"}\n";
            std::ofstream camera_report(executable_directory()/(adaptive_mode(focus)?L"camera-legacy.json":L"camera.json"));camera_report<<"{\"profile\":\""<<(app.ult_profile_active?"vindicta_ult":"regular")<<"\",\"ult_scope_active\":"<<(app.ult_scope_active?"true":"false")<<",\"ult_profile_enabled\":"<<(app.settings.vindicta_ult_override?"true":"false")<<",\"effective_speed\":"<<activation.speed<<",\"sniper_speed_active\":"<<(app.ult_profile_active?"true":"false")<<",\"sniper_focus_speed\":"<<app.settings.vindicta_ult.focus_speed<<",\"replay\":"<<(snapshot.replay?"true":"false")<<",\"practice\":"<<(snapshot.practice?"true":"false")<<",\"debug_enabled\":"<<(focus.debug_focus?"true":"false")<<",\"enabled\":"<<(focus.replay_focus?"true":"false")<<",\"active_button\":\""<<focus_button_name(activation.button)<<"\",\"x1_speed\":"<<focus.focus_speed<<",\"x2_speed\":"<<focus.focus_speed_x2<<",\"speed\":"<<activation.speed<<",\"exclude_teammates\":"<<(focus.exclude_teammates?"true":"false")<<",\"visible_only\":"<<(focus.visible_only?"true":"false")<<",\"focus_players\":"<<(focus.focus_players?"true":"false")<<",\"focus_minions\":"<<(focus.focus_minions?"true":"false")<<",\"focus_orbs\":"<<(focus.focus_orbs?"true":"false")<<",\"focus_visuals\":"<<(focus.focus_visuals?"true":"false")<<",\"bone\":"<<focus.focus_bone<<",\"target\":"<<app.camera.target<<",\"engaged\":"<<(app.camera.engaged?"true":"false")<<",\"lost\":"<<(app.camera.lost?"true":"false")<<",\"moves\":"<<app.camera.moves<<",\"hold_button\":\""<<focus_button_name(focus.focus_hold_button)<<"\",\"free_movement\":"<<(focus.free_focus?"true":"false")<<",\"free_movement_mode\":\""<<free_movement_name(focus.free_focus,focus.free_focus_mode)<<"\",\"hitbox_index\":"<<app.camera.hitbox_index<<",\"inside_hitbox\":"<<(app.camera.inside_hitbox?"true":"false")<<",\"line_position\":"<<app.camera.line_position<<",\"target_kind\":\""<<target_kind_name(app.camera.kind)<<"\""<<",\"prediction_enabled\":"<<(focus.prediction&&!app.ult_scope_active?"true":"false")<<",\"prediction_active\":"<<(app.camera.prediction_active?"true":"false")<<",\"projectile_speed\":"<<(app.ult_scope_active?0.f:focus.auto_speed?snapshot.weapon.speed:focus.projectile_speed)<<",\"auto_speed\":"<<(focus.auto_speed?"true":"false")<<",\"weapon_valid\":"<<(snapshot.weapon.valid?"true":"false")<<",\"weapon_handle\":"<<snapshot.weapon.handle<<",\"weapon_base_speed\":"<<snapshot.weapon.base_speed<<",\"weapon_bonus_percent\":"<<snapshot.weapon.bonus_percent<<",\"weapon_inheritance\":"<<snapshot.weapon.inheritance<<",\"weapon_status\":\""<<escaped(snapshot.weapon.status)<<"\",\"flight_time\":"<<app.camera.flight_time<<",\"local_velocity_valid\":"<<(snapshot.local_velocity_valid?"true":"false")<<",\"input_failures\":"<<app.camera.input_failures<<",\"focus_area_mode\":"<<int(focus.adaptive.area)<<",\"focus_radius_degrees\":"<<focus.adaptive.radius_degrees<<",\"focus_padding_percent\":"<<focus.adaptive.padding_percent<<",\"show_focus_area\":"<<(show_focus_area(focus)?"true":"false")<<"}\n";
            if(timing_frames){std::ofstream perf(executable_directory()/L"frame-performance.json");perf<<"{\"frames\":"<<timing_frames<<",\"priority_class\":"<<GetPriorityClass(GetCurrentProcess())<<",\"composition\":"<<(app.bar_window.renderer.composition()?"true":"false")<<",\"busy_bar_presents\":"<<app.bar_window.busy_presents<<",\"busy_menu_presents\":"<<app.settings_window.busy_presents<<",\"menu_open\":"<<(app.show_settings?"true":"false")<<",\"work_ms\":"<<work_sum/timing_frames<<",\"submit_ms\":"<<submit_sum/timing_frames<<",\"present_ms\":"<<present_sum/timing_frames<<",\"queue_wait_ms\":"<<queue_wait_sum/timing_frames<<",\"wait_ms\":"<<wait_sum/timing_frames<<",\"max_work_ms\":"<<work_max<<",\"max_present_ms\":"<<present_max<<",\"reader_ms\":"<<snapshot.sample_us/1000<<"}\n";}
            work_sum=wait_sum=submit_sum=present_sum=queue_wait_sum=work_max=present_max=0;timing_frames=0;
            std::ofstream trace(executable_directory()/L"frame-trace.json");trace<<"{\"status\":\""<<escaped(app.frame_status)<<"\",\"enabled\":"<<(trace_frames?"true":"false")<<",\"error\":"<<game_frames.error()<<",\"events\":"<<game_frames.count()<<",\"delivery_delay_ms\":"<<game_frames.delay_ms()<<"}\n";last_report=now;}
        auto wait_start=clock_ms();if(!focused||!game_frames.wait(app.target_hz))render_pacer.wait(focused?app.target_hz:20.0);if(rendered)wait_sum+=clock_ms()-wait_start;
    }
    app.makcu.stop();app.camera.release();app.adaptive.release();
    app.data_update.cancel();
    reader.request_stop();reader.join();game_frames.stop();save(app.settings);
    HWND menu_handle=app.settings_window.hwnd,bar_handle=app.bar_window.hwnd;
    app.settings_window.shutdown();app.bar_window.shutdown();CloseHandle(single);
    if(diagnostics){std::ofstream out(executable_directory()/L"shutdown.json");out<<"{\"reader_stopped\":true,\"menu_destroyed\":"<<(!IsWindow(menu_handle)?"true":"false")<<",\"bars_destroyed\":"<<(!IsWindow(bar_handle)?"true":"false")<<"}\n";}
    if(shutdown_complete)SetEvent(shutdown_complete);
    SetConsoleCtrlHandler(console_handler,FALSE);
    // Leave the event valid until process exit if a close handler is returning concurrently.
    return 0;
}
}
int main(int argc,char** argv) {
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--menu-preview") {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        return overlay::menu_previews(std::filesystem::path(argv[i+1]));
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--parry-probe") {
        std::filesystem::path profile,result=overlay::executable_directory()/L"parry-probe.json";unsigned duration=8000;
        for(int j=1;j+1<argc;++j) {
            if(std::string(argv[j])=="--reader-profile")profile=argv[j+1];
            if(std::string(argv[j])=="--parry-result")result=argv[j+1];
            if(std::string(argv[j])=="--probe-ms")try {duration=std::clamp(unsigned(std::stoul(argv[j+1])),1000u,30000u);}catch(...){return 2;}
        }
        // Read-only observation: no input worker, device handle or overlay window.
        overlay::GameReader game(profile);overlay::AutoParrySettings settings;settings.enabled=true;settings.observe_only=true;settings.counterspell=true;
        std::ofstream out(result);out<<"{\"read_only\":true,\"events\":[";bool first=true;std::string previous;unsigned threats{};overlay::Snapshot snapshot;
        const auto until=GetTickCount64()+duration;
        do {
            snapshot=game.sample({false,false,false,false,true,false,true,true});
            const auto decision=overlay::find_parry_threat(snapshot,settings,GetTickCount64());
            std::ostringstream signature;signature<<snapshot.parry.valid<<snapshot.parry.ready<<decision.valid<<decision.source;
            for(const auto& attack:snapshot.parry.melee)signature<<'/'<<attack.pawn<<'/'<<attack.state<<'/'<<attack.status;
            if(signature.str()!=previous) {
                if(!first)out<<',';first=false;overlay::write_parry_data(out,snapshot.parry,decision);previous=signature.str();
                if(decision.valid)++threats;
            }
            Sleep(4);
        }while(GetTickCount64()<until);
        out<<"],\"threat_events\":"<<threats<<",\"reader_status\":\""<<overlay::escaped(snapshot.status)<<"\",\"visibility\":\""<<overlay::escaped(snapshot.visibility_status)<<"\"}\n";
        std::cout<<"Parry observation: "<<threats<<" threat events; "<<snapshot.parry.status<<'\n';
        return out.good()&&snapshot.parry.valid?0:2;
    }
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--validate-profile") {
        auto candidate=std::filesystem::path(argv[i+1]);auto result=candidate.parent_path()/L"profile-live-validation.json";
        for(int j=1;j+1<argc;++j)if(std::string(argv[j])=="--profile-result")result=std::filesystem::path(argv[j+1]);
        overlay::GameReader game(candidate);overlay::Snapshot s;bool passed=false;
        for(int attempt=0;attempt<8;++attempt) {
            s=game.sample({true,true,true,true,true,false,true,false});
            bool live_geometry=std::any_of(s.players.begin(),s.players.end(),[](const auto& p){return p.dot_valid[0]&&p.dot_valid[1]&&p.dot_valid[2]&&!p.skeleton.empty()&&!p.hitboxes.empty();});
            bool root_geometry=std::any_of(s.players.begin(),s.players.end(),[](const auto& p){return p.motion.position_valid;});
            bool local=std::any_of(s.skipped_players.begin(),s.skipped_players.end(),[](const auto& p){return p.reason=="Local player";});
            passed=s.time&&s.projection.valid&&s.camera_valid&&s.controllers>0&&!s.missing_anchors&&live_geometry&&root_geometry
                &&(!local||s.hero.valid)&&(!local||s.replay||s.weapon.base_speed>=1000.f)
                &&(!s.local_alive||s.replay||s.parry.valid);
            if(passed)break;
            if(attempt<7)Sleep(200);
        }
        std::string message=passed?"Reader profile passed live validation":s.status;
        if(!passed&&s.status.starts_with("Connected"))message=s.players.empty()?"Load sandbox, bots or replay with a live remote player, then update again":s.missing_anchors?"Live bone reads failed; reader profile needs review":!s.hero.valid?"Live hero metadata did not validate":s.local_alive&&!s.parry.valid?s.parry.status:"Live hitbox or weapon data did not validate";
        std::ofstream out(result);out<<"{\"passed\":"<<(passed?"true":"false")<<",\"message\":\""<<overlay::escaped(message)<<"\",\"pid\":"<<s.pid<<",\"reader_profile\":\""<<overlay::escaped(s.reader_profile)<<"\",\"controllers\":"<<s.controllers<<",\"players\":"<<s.players.size()<<",\"missing_anchors\":"<<s.missing_anchors<<",\"hero_valid\":"<<(s.hero.valid?"true":"false")<<",\"weapon_valid\":"<<(s.weapon.valid?"true":"false")<<",\"weapon_base_speed\":"<<s.weapon.base_speed<<",\"weapon_status\":\""<<overlay::escaped(s.weapon.status)<<"\"}\n";
        overlay::report(s,result.parent_path()/L"profile-probe.json");
        std::ofstream parry(result.parent_path()/L"profile-parry.json");overlay::write_parry_data(parry,s.parry);parry<<'\n';
        return passed&&out.good()?0:2;
    }
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--update-data") {
        std::string mode=argv[i+1];if(mode!="maps"&&mode!="schema"&&mode!="all")return 2;
        overlay::DataUpdate updater;updater.initialize(overlay::executable_directory());
        std::wstring wide(mode.begin(),mode.end());if(!updater.start(wide.c_str(),nullptr)){std::cerr<<updater.message<<'\n';return 1;}
        std::string last;while(updater.running()) {
            updater.poll();if(updater.message!=last){std::cout<<updater.message<<'\n';last=updater.message;}
            Sleep(100);
        }
        return static_cast<int>(updater.exit_code);
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--makcu-probe") {
        overlay::MakcuInput device;device.start();auto deadline=GetTickCount64()+4000;
        while(!device.hardware_ready()&&GetTickCount64()<deadline)Sleep(10);
        bool connected=device.hardware_ready();
        // Exercise several heartbeat replies, including prompt consumption, without moving.
        if(connected){Sleep(3500);connected=device.hardware_ready();}
        auto status=device.status();device.stop();
        std::ofstream out(overlay::executable_directory()/L"makcu-probe.json");
        out<<"{\"connected\":"<<(connected?"true":"false")<<",\"status\":\""<<overlay::escaped(status)
            <<"\",\"movement_commands_sent\":"<<device.sent()<<"}\n";
        return connected?0:2;
    }
    bool probe=false,diagnostics=false,settings=false,render_test=false,shutdown_test=false,benchmark=false,frame_probe=false,trace_frames=false,resource_test=false,extended=false,test_tab=false;
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--self-test")return overlay::self_test();if(arg=="--visibility-test")return overlay::visibility_test();if(arg=="--render-test")render_test=true;if(arg=="--shutdown-test")shutdown_test=true;if(arg=="--probe")probe=true;if(arg=="--diagnostics")diagnostics=true;if(arg=="--settings")settings=true;}
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--benchmark")benchmark=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--frame-trace-probe")frame_probe=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--game-frames")trace_frames=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--resource-test")resource_test=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--extended-read")extended=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--legacy-renderer")overlay::prefer_composition=false;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--normal-priority")overlay::high_priority=false;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--test-tab"){test_tab=true;settings=true;}
    try {
        if(frame_probe){overlay::GameReader game;auto s=game.sample();overlay::GameFrames frames;frames.target(s.pid);frames.start();Sleep(3000);
            std::ofstream out(overlay::executable_directory()/L"frame-trace-probe.json");out<<"{\"pid\":"<<s.pid<<",\"status\":\""<<overlay::escaped(frames.status())<<"\",\"error\":"<<frames.error()<<",\"events\":"<<frames.count()<<",\"delivery_delay_ms\":"<<frames.delay_ms()<<"}\n";frames.stop();return 0;}
        if(benchmark) {
            overlay::GameReader game;overlay::Snapshot s;
            for(int i=0;i<8;++i){s=game.sample({extended,extended,extended,extended});if(s.time&&s.controllers)break;Sleep(200);}
            double total_us{};uint64_t total_reads{};int samples{};
            for(int i=0;i<203;++i){s=game.sample({extended,extended,extended,extended});if(i>=3&&s.time&&s.controllers){total_us+=s.sample_us;total_reads+=s.read_calls;++samples;}}
            auto timing=overlay::display_timing(overlay::game_window(s.pid));overlay::FramePacer pacer;
            auto start=std::chrono::steady_clock::now();for(int i=0;i<120;++i)pacer.wait(timing.hz);
            double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::ofstream out(overlay::executable_directory()/L"performance-test.json");
            out<<"{\"extended_read\":"<<(extended?"true":"false")<<",\"target_hz\":"<<timing.hz<<",\"pacing_hz\":"<<120/seconds<<",\"samples\":"<<samples<<",\"average_sample_us\":"<<(samples?total_us/samples:0)<<",\"average_read_calls\":"<<(samples?double(total_reads)/samples:0)<<"}\n";
            return samples?0:2;
        }
        if(probe){
            overlay::GameReader game;overlay::Snapshot s;
            for(int i=0;i<8;++i){s=game.sample({true,true,true,true,true,false,false,false,true});if(s.time&&s.controllers)break;Sleep(200);}
            // Read-only observation warms the displacement window and compares
            // the schema velocity candidate. This command never opens MAKCU.
            if(s.time&&s.controllers)for(int i=0;i<40;++i){Sleep(16);s=game.sample({true,true,true,true,true,false,false,false,true});}
            overlay::report(s,overlay::executable_directory()/L"probe.json");
            overlay::visibility_report(s,overlay::executable_directory()/L"visibility-probe.json",true,false);
            RECT bounds{};GetClientRect(overlay::game_window(s.pid),&bounds);
            overlay::report_hitbox_focus(s,float(bounds.right),float(bounds.bottom),overlay::executable_directory()/L"hitbox-focus-probe.json");
            overlay::report_ray_click(s,float(bounds.right),float(bounds.bottom),overlay::executable_directory()/L"ray-click-probe.json");
            overlay::report_adaptive_geometry(s,overlay::executable_directory()/L"adaptive-probe.json");
            return s.time&&s.controllers?0:2;
        }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(resource_test)return overlay::resource_test();if(render_test)return overlay::render_test();return overlay::run(diagnostics||shutdown_test,settings,shutdown_test,trace_frames,test_tab);
    } catch(const std::exception& e) {std::ofstream out(overlay::executable_directory()/L"error.log",std::ios::app);out<<e.what()<<'\n';return 1;}
}



