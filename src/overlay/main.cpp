#include "game_reader.h"
#include "replay_camera.h"
#include "frame_pacing.h"
#include "game_frames.h"
#include "data_update.h"
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
struct Settings {
    bool enabled=true, numbers=true,glass=true,dark=true,exclude_teammates=false;
    bool transparent_frame=true;float frame_opacity=.72f;
    bool head_dot=false,body_dot=false,pelvis_dot=false;
    bool skeletons=false,hitboxes=false;
    ImVec4 skeleton_visible{.2f,.62f,1.f,1.f},skeleton_hidden{.68f,.32f,1.f,1.f};
    bool focus_players=true,focus_minions=false,focus_orbs=false;
    bool replay_focus=true,debug_focus=true;int focus_bone=1;
    float focus_speed=12.f,projectile_speed=30000.f;
    bool sniper_speed_override=true;float sniper_focus_speed=12.f;
    bool free_focus=false,prediction=false,inherit_velocity=false,auto_speed=true;
    FreeMovementMode free_focus_mode{FreeMovementMode::BoneLine};
    bool visible_only=true;
    bool focus_visuals=true;
    FocusArea focus_area{};
    std::array<bool,3> show_focus_area{};
    float width=80,height=6,gap=8;
    ImVec4 green{0.0f,0.74f,0.42f,1}, amber{0.98f,0.61f,0,1}, red{0.93f,0.2f,0.23f,1};
};
void save(const Settings& s,const std::filesystem::path& path=config_path()) {
    std::ofstream out(path);out<<"enabled="<<s.enabled<<"\nnumbers="<<s.numbers<<"\nglass="<<s.glass<<"\ndark="<<s.dark
        <<"\nexclude_teammates="<<s.exclude_teammates<<"\nhead_dot="<<s.head_dot<<"\nbody_dot="<<s.body_dot<<"\npelvis_dot="<<s.pelvis_dot<<"\ndebug_focus="<<s.debug_focus<<"\nreplay_focus="<<s.replay_focus<<"\nfocus_speed="<<s.focus_speed<<"\nfocus_bone="<<s.focus_bone<<"\nauto_speed="<<s.auto_speed<<"\nfree_focus="<<s.free_focus<<"\nprediction="<<s.prediction<<"\ninherit_velocity="<<s.inherit_velocity<<"\nprojectile_speed="<<s.projectile_speed<<"\nwidth="<<s.width<<"\nheight="<<s.height<<"\ngap="<<s.gap<<'\n';
    const ImVec4* colors[]={&s.green,&s.amber,&s.red};const char* names[]={"green","amber","red"};
    out<<"visible_only="<<s.visible_only<<'\n';
    out<<"focus_visuals="<<s.focus_visuals<<'\n';
    out<<"sniper_speed_override="<<s.sniper_speed_override<<"\nsniper_focus_speed="<<s.sniper_focus_speed<<'\n';
    out<<"focus_area_mode="<<int(s.focus_area.mode)<<"\nfocus_fov_percent="<<s.focus_area.center_percent<<"\nfocus_radius_meters="<<s.focus_area.radius_meters<<'\n';
    for(int i=0;i<3;++i)out<<"show_focus_area_"<<i<<'='<<s.show_focus_area[i]<<'\n';
    out<<"transparent_frame="<<s.transparent_frame<<"\nframe_opacity="<<s.frame_opacity<<'\n';
    out<<"skeletons="<<s.skeletons<<"\nfocus_players="<<s.focus_players<<"\nfocus_minions="<<s.focus_minions<<"\nfocus_orbs="<<s.focus_orbs<<'\n';
    out<<"hitboxes="<<s.hitboxes<<'\n';
    out<<"free_focus_mode="<<int(s.free_focus_mode)<<'\n';
    out<<"skeleton_visible="<<s.skeleton_visible.x<<' '<<s.skeleton_visible.y<<' '<<s.skeleton_visible.z<<'\n';
    out<<"skeleton_hidden="<<s.skeleton_hidden.x<<' '<<s.skeleton_hidden.y<<' '<<s.skeleton_hidden.z<<'\n';
    for(int i=0;i<3;++i) out<<names[i]<<'='<<colors[i]->x<<' '<<colors[i]->y<<' '<<colors[i]->z<<'\n';
}
Settings load(const std::filesystem::path& path=config_path()) {
    Settings s;std::ifstream in(path); std::string line;bool loaded_sniper_speed=false;
    while(std::getline(in,line)) {
        auto at=line.find('=');if(at==std::string::npos) continue;
        auto key=line.substr(0,at),value=line.substr(at+1);std::istringstream data(value);
        if(key=="enabled") {int n{};if(data>>n)s.enabled=n!=0;}
        else if(key=="exclude_teammates") {int n{};if(data>>n)s.exclude_teammates=n!=0;}
        else if(key=="numbers") {int n{};if(data>>n)s.numbers=n!=0;}
        else if(key=="glass") {int n{};if(data>>n)s.glass=n!=0;}
        else if(key=="transparent_frame") {int n{};if(data>>n)s.transparent_frame=n!=0;}
        else if(key=="dark") {int n{};if(data>>n)s.dark=n!=0;}

        else if(key=="head_dot") {int n{};if(data>>n)s.head_dot=n!=0;}
        else if(key=="body_dot") {int n{};if(data>>n)s.body_dot=n!=0;}
        else if(key=="pelvis_dot") {int n{};if(data>>n)s.pelvis_dot=n!=0;}
        else if(key=="replay_focus") {int n{};if(data>>n)s.replay_focus=n!=0;}
        else if(key=="debug_focus") {int n{};if(data>>n)s.debug_focus=n!=0;}
        else if(key=="auto_speed") {int n{};if(data>>n)s.auto_speed=n!=0;}
        else if(key=="visible_only") {int n{};if(data>>n)s.visible_only=n!=0;}
        else if(key=="focus_visuals") {int n{};if(data>>n)s.focus_visuals=n!=0;}
        else if(key=="sniper_speed_override") {int n{};if(data>>n)s.sniper_speed_override=n!=0;}
        else if(key=="sniper_focus_speed") {float speed{};if(data>>speed&&std::isfinite(speed)){s.sniper_focus_speed=std::clamp(speed,1.f,120.f);loaded_sniper_speed=true;}}
        else if(key=="focus_area_mode") {int n{};if(data>>n)s.focus_area.mode=static_cast<FocusAreaMode>(std::clamp(n,0,2));}
        else if(key=="show_focus_area_0"||key=="show_focus_area_1"||key=="show_focus_area_2") {int n{};if(data>>n)s.show_focus_area[key.back()-'0']=n!=0;}
        else if(key=="skeletons") {int n{};if(data>>n)s.skeletons=n!=0;}
        else if(key=="hitboxes") {int n{};if(data>>n)s.hitboxes=n!=0;}
        else if(key=="focus_players") {int n{};if(data>>n)s.focus_players=n!=0;}
        else if(key=="focus_minions") {int n{};if(data>>n)s.focus_minions=n!=0;}
        else if(key=="focus_orbs") {int n{};if(data>>n)s.focus_orbs=n!=0;}
        else if(key=="free_focus") {int n{};if(data>>n)s.free_focus=n!=0;}
        else if(key=="free_focus_mode") {int n{};if(data>>n)s.free_focus_mode=static_cast<FreeMovementMode>(std::clamp(n,0,1));}
        else if(key=="prediction") {int n{};if(data>>n)s.prediction=n!=0;}
        else if(key=="inherit_velocity") {int n{};if(data>>n)s.inherit_velocity=n!=0;}
        else if(key=="focus_bone") {int n{};if(data>>n)s.focus_bone=std::clamp(n,0,2);}
        else {
            float* target= key=="width"?&s.width:key=="height"?&s.height:key=="gap"?&s.gap:key=="focus_speed"?&s.focus_speed:key=="projectile_speed"?&s.projectile_speed:key=="frame_opacity"?&s.frame_opacity:key=="focus_fov_percent"?&s.focus_area.center_percent:key=="focus_radius_meters"?&s.focus_area.radius_meters:nullptr;
            if(target) {float v{};if(data>>v && std::isfinite(v))*target=v;}
            ImVec4* color=key=="green"?&s.green:key=="amber"?&s.amber:key=="red"?&s.red:key=="skeleton_visible"?&s.skeleton_visible:key=="skeleton_hidden"?&s.skeleton_hidden:nullptr;
            if(color) {float r{},g{},b{};if(data>>r>>g>>b && std::isfinite(r)&&std::isfinite(g)&&std::isfinite(b)) *color={std::clamp(r,0.f,1.f),std::clamp(g,0.f,1.f),std::clamp(b,0.f,1.f),1};}
        }
    }
    s.width=std::clamp(s.width,40.f,200.f);s.height=std::clamp(s.height,3.f,16.f);s.gap=std::clamp(s.gap,2.f,40.f);s.focus_speed=std::clamp(s.focus_speed,1.f,120.f);s.projectile_speed=std::clamp(s.projectile_speed,1000.f,100000.f);s.frame_opacity=std::clamp(s.frame_opacity,.4f,.9f);s.focus_area=valid_focus_area(s.focus_area);
    if(!loaded_sniper_speed)s.sniper_focus_speed=s.focus_speed;return s;
}
bool show_focus_area(const Settings& settings){return settings.show_focus_area[int(valid_focus_area(settings.focus_area).mode)];}
std::string escaped(const std::string& input) {
    std::string result;for(char c:input) {if(c=='"'||c=='\\')result+='\\';if(c=='\n'){result+="\\n";continue;}if(static_cast<unsigned char>(c)>=32)result+=c;}return result;
}
void visibility_report(const Snapshot& s,const std::filesystem::path& path,bool enabled,bool occluded){
    std::ofstream out(path);out<<"{\"enabled\":"<<(enabled?"true":"false")<<",\"map\":\""<<escaped(s.map_name)<<"\",\"level\":\""<<escaped(s.level_name)<<"\",\"map_source\":\""<<escaped(s.map_source)<<"\",\"status\":\""<<escaped(s.visibility_status)<<"\",\"triangles\":"<<(s.visibility?s.visibility->triangle_count():0)<<",\"ray_ms\":"<<s.visibility_us/1000<<",\"camera_valid\":"<<(s.camera_valid?"true":"false")<<",\"camera_position\":["<<s.camera_position.x<<','<<s.camera_position.y<<','<<s.camera_position.z<<"],\"focus_occluded\":"<<(occluded?"true":"false")<<",\"players\":[";
    bool first=true;for(const auto& p:s.players){if(!first)out<<',';first=false;out<<"{\"handle\":"<<p.handle<<",\"visible\":"<<(p.visible?"true":"false")<<",\"bones\":[";for(int i=0;i<3;++i){if(i)out<<',';out<<(p.dot_visible[i]?"true":"false");}out<<"]}";}out<<"]}\n";
}
void report(const Snapshot& s,const std::filesystem::path& path,int bars=-1,bool visible=false,LONG_PTR style=0,HWND overlay_window=nullptr,bool settings_visible=false,double target_hz=0,double render_fps=0,int drawn_hitboxes=0) {
    DWORD foreground_pid{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    RECT bounds{};if(overlay_window)GetWindowRect(overlay_window,&bounds);
    std::ofstream out(path);out<<"{\n\"pid\":"<<s.pid<<",\"status\":\""<<escaped(s.status)<<"\",\"sample_time\":"<<s.time
        <<",\"map\":\""<<escaped(s.map_name)<<"\",\"level\":\""<<escaped(s.level_name)<<"\",\"map_source\":\""<<escaped(s.map_source)<<"\",\"visibility_status\":\""<<escaped(s.visibility_status)<<"\",\"triangles\":"<<(s.visibility?s.visibility->triangle_count():0)
        <<",\"controllers\":"<<s.controllers<<",\"local_team\":"<<unsigned(s.local_team)<<",\"invalid_handles\":"<<s.invalid_handles<<",\"missing_anchors\":"<<s.missing_anchors
        <<",\"drawn_bars\":"<<bars<<",\"overlay_visible\":"<<(visible?"true":"false")<<",\"overlay_extended_style\":"<<style
        <<",\"drawn_hitboxes\":"<<drawn_hitboxes
        <<",\"foreground_pid\":"<<foreground_pid<<",\"settings_visible\":"<<(settings_visible?"true":"false")
        <<",\"practice\":"<<(s.practice?"true":"false")<<",\"match_mode\":"<<s.match_mode<<",\"game_mode\":"<<s.game_mode<<",\"replay\":"<<(s.replay?"true":"false")<<",\"target_hz\":"<<target_hz<<",\"render_fps\":"<<render_fps<<",\"sample_us\":"<<s.sample_us<<",\"sample_reads\":"<<s.read_calls
        <<",\"hero\":{\"valid\":"<<(s.hero.valid?"true":"false")<<",\"id\":"<<s.hero.id<<",\"name\":\""<<escaped(s.hero.name)<<"\",\"token\":\""<<escaped(s.hero.token)<<"\",\"sniper_present\":"<<(s.hero.sniper_present?"true":"false")<<",\"sniper_valid\":"<<(s.hero.sniper_valid?"true":"false")<<",\"sniper_scoped\":"<<(s.hero.sniper_scoped?"true":"false")<<",\"scope_start\":"<<s.hero.scope_start<<",\"ability_status\":\""<<escaped(s.hero.ability_status)<<"\"}"
        <<",\"projection\":{\"source\":\"final_render_view\",\"valid\":"<<(s.projection.valid?"true":"false")<<",\"horizontal_fov\":"<<s.projection.horizontal_fov<<",\"vertical_fov\":"<<s.projection.vertical_fov<<",\"aspect\":"<<s.projection.aspect<<"}"
        <<",\"overlay_rect\":["<<bounds.left<<','<<bounds.top<<','<<bounds.right<<','<<bounds.bottom<<"],\"players\":[";
    bool first=true;for(const auto& p:s.players) {if(!first)out<<',';first=false;
        out<<"{\"handle\":"<<p.handle<<",\"team\":"<<unsigned(p.team)<<",\"health\":"<<p.health<<",\"maximum\":"<<p.maximum<<",\"anchor\":\""<<p.anchor
            <<"\",\"world\":["<<p.head.x<<','<<p.head.y<<','<<p.head.z<<"],\"dots\":[";
        for(size_t i=0;i<3;++i){if(i)out<<',';if(p.dot_valid[i])out<<'['<<p.dots[i].x<<','<<p.dots[i].y<<','<<p.dots[i].z<<']';else out<<"null";}
        size_t visible_segments=0,unknown_segments=0;for(const auto& segment:p.skeleton){if(segment.visible)++visible_segments;if(!segment.visibility_known)++unknown_segments;}
        out<<"],\"skeleton_segments\":"<<p.skeleton.size()<<",\"skeleton_visible\":"<<visible_segments<<",\"skeleton_unknown\":"<<unknown_segments
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
        out<<"],\"hitboxes\":"<<target.hitboxes.size()<<",\"velocity_valid\":"<<(target.velocity_valid?"true":"false")<<",\"velocity\":["<<target.velocity.x<<','<<target.velocity.y<<','<<target.velocity.z<<"]}";
    }
    out<<"],\"matrix\":[";for(size_t i=0;i<s.matrix.size();++i){if(i)out<<',';out<<s.matrix[i];}out<<"]}\n";
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
    auto horizontal=bar_rect({100,100},80,6,8,1);check(horizontal.left==60&&horizontal.right==140&&horizontal.top==86&&horizontal.bottom==92);
    auto scaled=bar_rect({100,100},80,6,8,2);check(scaled.left==20&&scaled.right==180&&scaled.top==72&&scaled.bottom==84);
    auto half=bar_fill(horizontal,.5f);check(half.left==60&&half.right==100);
    check(bar_fill(horizontal,0).right==horizontal.left);check(bar_fill(horizontal,2).right==horizontal.right);
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
    Snapshot choices;choices.matrix=identity;choices.local_team=2;choices.players.resize(1);
    choices.players[0].handle=10;choices.players[0].team=3;choices.players[0].dot_valid.fill(true);choices.players[0].dots=body_line;
    FocusTarget minion;minion.handle=20;minion.kind=TargetKind::Minion;minion.team=3;minion.dot_valid.fill(true);minion.dots.fill({.1f,0,0});
    FocusTarget orb=minion;orb.handle=30;orb.kind=TargetKind::SoulOrb;orb.team=2;orb.dots.fill({0,0,0});choices.focus_targets={minion,orb};
    auto pick=[&](FocusOptions options,bool enemies=false,bool free_move=false,bool visible=false){return select_focus_target(choices,options,enemies,1,free_move,visible,1000,1000,{500,500});};
    check(pick({true,false,false})==&choices.players[0]);check(pick({true,true,false})==&choices.focus_targets[0]);
    check(pick({true,true,true},true)==&choices.focus_targets[1]);check(!pick({false,false,false}));check(!pick({true,true,true},false,false,true));
    choices.focus_targets[0].team=2;check(!pick({false,true,false},true));choices.replay=true;check(pick({false,true,false},true)==&choices.focus_targets[0]);choices.replay=false;
    choices.players[0].dots={Vec3{0,1,0},Vec3{0,.6f,0},Vec3{0,-.5f,0}};
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
    // Area geometry: metric sizes, depth scaling, boundaries and camera-plane clipping.
    FocusArea center_area{},box_area{FocusAreaMode::TargetBox3D,40,10},circle_area{FocusAreaMode::TargetCircle2D,40,10};
    check(std::abs(focus_radius_units(box_area)*meters_per_world_unit-10)<.00001f);
    auto invalid_area=valid_focus_area({static_cast<FocusAreaMode>(99),NAN,INFINITY});
    check(invalid_area==center_area);
    check(valid_focus_area({FocusAreaMode::CenterFov,-3,100}).center_percent==1&&valid_focus_area({FocusAreaMode::CenterFov,-3,100}).radius_meters==50);
    check(center_fov_radius(center_area,1920,1080)==432&&center_fov_radius(center_area,1080,1920)==432);
    check(focus_area_contains(center_area,identity,{},ScreenPoint{900,500},1000,1000,{500,500}));
    check(!focus_area_contains(center_area,identity,{},ScreenPoint{901,500},1000,1000,{500,500}));
    check(!focus_area_contains(center_area,identity,{},ScreenPoint{500,500},NAN,1000,{500,500}));
    Matrix camera_matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};Vec3 ray_origin{},ray_direction{};
    check(focus_camera_ray(camera_matrix,ray_origin,ray_direction)&&ray_direction.z==1&&dot(ray_origin,ray_origin)==0);
    check(focus_camera_ray(perspective,ray_origin,ray_direction)&&ray_origin.x==10&&ray_origin.y==20&&ray_origin.z==30);
    Matrix rotated_camera{0,1,0,-20,0,0,1,-30,1,0,0,0,1,0,0,-10};
    check(focus_camera_ray(rotated_camera,ray_origin,ray_direction)&&ray_origin.x==10&&ray_origin.y==20&&ray_origin.z==30&&ray_direction.x==1);
    check(!focus_camera_ray(identity,ray_origin,ray_direction));
    check(focus_ray_box({0,0,0},{0,0,1},{1,0,10},1));
    check(!focus_ray_box({0,0,0},{0,0,1},{1.01f,0,10},1));
    check(focus_ray_box({0,0,0},{0,0,1},{0,0,0},1));
    check(!focus_ray_box({0,0,0},{0,0,1},{0,0,-10},1));
    check(!focus_ray_box({0,0,0},{0,0,0},{0,0,10},1));
    check(!focus_ray_box({NAN,0,0},{0,0,1},{0,0,10},1));
    float near_radius{},far_radius{};auto metric_radius=focus_radius_units(circle_area);
    check(target_circle_radius(camera_matrix,{0,0,1000},metric_radius,1000,1000,near_radius));
    check(target_circle_radius(camera_matrix,{0,0,2000},metric_radius,1000,1000,far_radius)&&std::abs(near_radius-2*far_radius)<.0001f);
    Matrix scaled_matrix=camera_matrix;for(auto& value:scaled_matrix)value*=2;
    float scaled_radius{};check(target_circle_radius(scaled_matrix,{0,0,1000},metric_radius,1000,1000,scaled_radius)&&std::abs(scaled_radius-near_radius)<.0001f);
    check(!target_circle_radius(camera_matrix,{0,0,-1000},metric_radius,1000,1000,scaled_radius));
    auto contains=[&](FocusArea area,Vec3 bone){ScreenPoint aim{};return focus_project(camera_matrix,bone,1000,1000,aim)&&focus_area_contains(area,camera_matrix,bone,aim,1000,1000,{500,500});};
    check(contains(box_area,{metric_radius,0,1000}));check(!contains(box_area,{metric_radius+1,0,1000}));
    check(contains(circle_area,{metric_radius-1,0,1000}));check(!contains(circle_area,{metric_radius+1,0,1000}));
    check(contains(box_area,{metric_radius*.9f,metric_radius*.9f,1000})&&!contains(circle_area,{metric_radius*.9f,metric_radius*.9f,1000}));
    check(!contains(box_area,{0,0,-10}));
    auto corners=focus_box_corners({10,20,30},4);check(corners[0].x==6&&corners[0].y==16&&corners[0].z==26&&corners[7].x==14&&corners[7].y==24&&corners[7].z==34);
    ScreenPoint edge_a{},edge_b{};
    check(focus_project_segment(camera_matrix,{-20,0,10},{20,0,10},1000,1000,edge_a,edge_b)&&edge_a.x==0&&edge_b.x==1000);
    check(focus_project_segment(camera_matrix,{0,0,-10},{0,0,10},1000,1000,edge_a,edge_b)&&edge_a.x==500&&edge_b.x==500);
    check(!focus_project_segment(camera_matrix,{0,0,-20},{0,0,-10},1000,1000,edge_a,edge_b));
    check(!focus_project_segment(camera_matrix,{20,0,10},{30,0,10},1000,1000,edge_a,edge_b));
    check(focus_project(camera_matrix,{2000,0,1000},1000,1000,edge_a)&&edge_a.x==1500);
    check(!focus_project(camera_matrix,{0,0,-1000},1000,1000,edge_a));
    // Distance first, lower raw HP on a distance tie, and uniform random exact ties.
    Snapshot overlap;overlap.matrix=camera_matrix;overlap.time=100;
    auto make_target=[](uint32_t handle,Vec3 at,int health){Player p;p.handle=handle;p.health=health;p.maximum=1000;p.dot_valid.fill(true);p.dots.fill(at);return p;};
    overlap.players={make_target(100,{100,0,1000},500),make_target(200,{-100,0,1000},100)};
    std::mt19937 test_random(1928);
    auto area_pick=[&](FocusArea area=FocusArea{}){return select_focus_target(overlap,{true,false,false},false,1,false,false,1000,1000,{500,500},area,&test_random);};
    check(area_pick()->handle==200);overlap.players[0].health=10;check(area_pick()->handle==100);
    overlap.players[0].dots.fill({101,0,1000});check(area_pick()->handle==200);
    overlap.players[0].dots.fill({100,0,1000});overlap.players[0].health=100;
    int first_wins{},second_wins{};for(int i=0;i<256;++i){auto result=area_pick();if(result&&result->handle==100)++first_wins;else if(result&&result->handle==200)++second_wins;}
    check(first_wins>90&&first_wins<166&&first_wins+second_wins==256);
    overlap.players[0].maximum=0;check(area_pick()->handle==200);overlap.players[0].maximum=1000;
    overlap.players[0].dots.fill({0,0,-1000});check(area_pick(box_area)->handle==200);
    overlap.players[0].dots.fill({1000,0,5000});overlap.players[1].dots.fill({0,0,-1000});
    check(area_pick(center_area)->handle==100&&!area_pick(box_area)&&!area_pick(circle_area));
    overlap.players[0].dots.fill({metric_radius*.9f,metric_radius*.9f,1000});
    check(area_pick(box_area)->handle==100&&!area_pick(circle_area));
    // Held selection retries after an empty acquisition, remains stable on ties,
    // and stops/reacquires when the region or full entity handle becomes invalid.
    ReplayCamera area_camera;
    auto choose_area=[&](FocusArea area=FocusArea{FocusAreaMode::TargetBox3D,40,10}){return area_camera.choose(overlap,{true,false,false},false,1,false,false,area,1000,1000,{500,500},double(overlap.time),&test_random);};
    overlap.players[0].dots.fill({1000,0,5000});check(!choose_area()&&area_camera.lost);
    overlap.time=101;overlap.players[0].dots.fill({100,0,1000});check(choose_area()->handle==100&&!area_camera.lost);
    overlap.players[1].dots.fill({-100,0,1000});
    for(int i=0;i<128;++i){++overlap.time;check(choose_area()->handle==100);}
    ++overlap.time;overlap.players[0].dots.fill({1000,0,5000});check(choose_area()->handle==200);
    ++overlap.time;overlap.players[1].dots.fill({1000,0,5000});check(!choose_area()&&area_camera.target==0);
    ++overlap.time;overlap.players[1].handle=0x100c8;overlap.players[1].dots.fill({100,0,1000});check(choose_area()->handle==0x100c8);
    area_camera.release();check(!area_camera.engaged&&!area_camera.target&&!area_camera.point_valid);
    ++overlap.time;check(choose_area()->handle==0x100c8);++overlap.time;check(choose_area({FocusAreaMode::TargetBox3D,40,.1f})==nullptr);
    Settings area_settings;area_settings.focus_area=circle_area;area_settings.focus_area.radius_meters=7.25f;area_settings.show_focus_area={true,false,true};area_settings.focus_visuals=false;
    auto area_settings_path=executable_directory()/L"focus-settings-test.ini";save(area_settings,area_settings_path);auto restored=load(area_settings_path);
    check(restored.focus_area==area_settings.focus_area&&restored.show_focus_area==area_settings.show_focus_area&&!restored.focus_visuals);
    {std::ofstream bad(area_settings_path);bad<<"focus_area_mode=99\nfocus_fov_percent=-5\nfocus_radius_meters=999\n";}
    restored=load(area_settings_path);check(restored.focus_area.mode==FocusAreaMode::TargetCircle2D&&restored.focus_area.center_percent==1&&restored.focus_area.radius_meters==50);
    Settings hero_settings;hero_settings.focus_speed=17;hero_settings.sniper_focus_speed=42;hero_settings.sniper_speed_override=false;
    save(hero_settings,area_settings_path);restored=load(area_settings_path);
    check(restored.focus_speed==17&&restored.sniper_focus_speed==42&&!restored.sniper_speed_override);
    {std::ofstream legacy(area_settings_path);legacy<<"focus_speed=19\n";}
    restored=load(area_settings_path);check(restored.sniper_focus_speed==19&&restored.sniper_speed_override);
    {std::ofstream bad(area_settings_path);bad<<"sniper_focus_speed=999\n";}
    check(load(area_settings_path).sniper_focus_speed==120);
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
        if(!overlay&&context){solace::glass::cursor_shutdown();solace::rounded_panel::shutdown();solace::snapshot::shutdown();solace::fonts.reset();solace::ui_runtime::clear_animation_states();}
        if(context) {ImGui::SetCurrentContext(context);ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);context=nullptr;}
        if(hwnd){DestroyWindow(hwnd);hwnd=nullptr;}
    }
    ~Surface() {shutdown();}
};
struct Application {
    DataUpdate data_update;
    std::atomic<unsigned> data_revision{};
    MakcuInput makcu;
    ReplayCamera camera;
    Surface settings_window,bar_window;
    Settings settings=load();
    bool quit{},show_settings{},diagnostics{};
    solace::shell_page menu_page{solace::shell_page::camera};
    int page{-1},bars{};float page_time{1.f};
    HWND game_hwnd{};
    double target_hz{60},render_fps{};std::string display,frame_status="Refresh pacing";
    std::mutex mutex; Snapshot snapshot;
    std::atomic_bool sampling{true};
    std::atomic<double> sample_hz{60};
    std::atomic<unsigned> read_options{};
    std::unordered_map<uint32_t,float> displayed;
    void open_settings() {
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
        show_settings=false;save(settings);
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
    case WM_KEYDOWN:if(!s->overlay&&w==VK_ESCAPE){s->app->hide_settings();return 0;}break;
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
void draw_bar(ImDrawList* draw,ScreenPoint p,int health,int maximum,float fill,const Settings& s,float scale) {
    auto rect=bar_rect(p,s.width,s.height,s.gap,scale);ImVec2 a{rect.left,rect.top},b{rect.right,rect.bottom};
    draw->AddRectFilled({a.x-1,a.y-1},{b.x+1,b.y+1},IM_COL32(8,9,10,245),3*scale);
    draw->AddRectFilled(a,b,IM_COL32(28,28,28,230),2*scale);
    auto ratio=health_fraction(health,maximum);auto color=ratio>0.6f?s.green:ratio>0.3f?s.amber:s.red;
    fill=std::clamp(fill,0.f,1.f);
    auto filled=bar_fill(rect,fill);
    if(fill>0)draw->AddRectFilled({filled.left,filled.top},{filled.right,filled.bottom},ImGui::ColorConvertFloat4ToU32(color),2*scale);
    if(s.numbers){auto text=std::to_string(health);auto font=ImGui::GetFont();float size=15*scale;auto extent=font->CalcTextSizeA(size,FLT_MAX,0,text.c_str());ImVec2 pos{(a.x+b.x)/2-extent.x/2,a.y-extent.y-3*scale};
        draw->AddText(font,size,{pos.x+1,pos.y+1},IM_COL32(0,0,0,240),text.c_str());draw->AddText(font,size,pos,IM_COL32(242,242,242,255),text.c_str());}
}
void draw_dot(ImDrawList* draw,ScreenPoint p,int index,float scale) {
    constexpr ImU32 colors[]={IM_COL32(245,245,245,255),IM_COL32(56,205,239,255),IM_COL32(255,181,61,255)};
    draw->AddCircleFilled({p.x,p.y},5*scale,IM_COL32(8,9,10,240),16);
    draw->AddCircleFilled({p.x,p.y},3.5f*scale,colors[index],16);
}
void draw_skeleton(ImDrawList* draw,const Player& player,const Matrix& matrix,float width,float height,const Settings& settings,float scale) {
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
void draw_focus_area(ImDrawList* draw,const Snapshot& snapshot,const Settings& settings,float width,float height,uint32_t selected,float scale) {
    if(!show_focus_area(settings)||width<=0||height<=0)return;
    const auto area=valid_focus_area(settings.focus_area);ScreenPoint center{width*.5f,height*.5f};
    if(area.mode==FocusAreaMode::CenterFov) {
        draw->AddCircle({center.x,center.y},center_fov_radius(area,width,height),IM_COL32(145,210,240,200),96,1.5f*scale);
        return;
    }
    int bone=std::clamp(settings.focus_bone,0,2);
    FocusOptions options{settings.focus_players,settings.focus_minions,settings.focus_orbs};
    auto target_area=[&](const FocusTarget& target) {
        if(!focus_candidate(target,snapshot,options,settings.exclude_teammates)||!target.dot_valid[bone])return;
        // Rendering uses cached visibility; it does not add per-frame map traces.
        bool visible=target.dot_visible[bone];
        if(settings.free_focus)visible=settings.free_focus_mode==FreeMovementMode::Hitboxes&&target.kind!=TargetKind::SoulOrb
            ?std::any_of(target.hitboxes.begin(),target.hitboxes.end(),[](const auto& box){return box.visibility_known&&box.visible;}):target.visible;
        if(settings.visible_only&&!visible)return;
        ScreenPoint anchor;if(!focus_project(snapshot.matrix,target.dots[bone],width,height,anchor))return;
        auto color=target.handle==selected?IM_COL32(255,200,70,220):IM_COL32(125,195,235,140);
        if(area.mode==FocusAreaMode::TargetCircle2D) {
            float radius{};if(!target_circle_radius(snapshot.matrix,target.dots[bone],focus_radius_units(area),width,height,radius))return;
            float farthest=std::hypot(std::max(std::abs(anchor.x),std::abs(anchor.x-width)),std::max(std::abs(anchor.y),std::abs(anchor.y-height)));
            float nearest=std::hypot(std::max({-anchor.x,anchor.x-width,0.f}),std::max({-anchor.y,anchor.y-height,0.f}));
            if(radius>farthest+2*scale||radius<nearest-2*scale)return;
            draw->AddCircle({anchor.x,anchor.y},radius,color,64,1.5f*scale);
        }else {
            auto corners=focus_box_corners(target.dots[bone],focus_radius_units(area));
            for(int i=0;i<8;++i)for(int bit=1;bit<=4;bit*=2)if(!(i&bit)) {
                ScreenPoint a{},b{};
                if(focus_project_segment(snapshot.matrix,corners[i],corners[i|bit],width,height,a,b))draw->AddLine({a.x,a.y},{b.x,b.y},color,1.5f*scale);
            }
        }
    };
    for(const auto& player:snapshot.players)target_area(player);
    for(const auto& target:snapshot.focus_targets)target_area(target);
}
struct PageContext { Application& app; const Snapshot& snapshot; };
void overlay_page(solace::shell_page page,const ImRect& body,float alpha,void* context) {
    auto& data=*static_cast<PageContext*>(context);auto& app=data.app;const auto& snap=data.snapshot;
    using namespace solace;
    if(app.page!=int(page)){app.page=int(page);app.page_time=0;}
    app.page_time+=ImGui::GetIO().DeltaTime;
    float reveal=mo::EASE_OUT(std::clamp(app.page_time/.22f,0.f,1.f));alpha*=reveal;
    ImVec2 origin=body.Min+px(0.f,8.f*(1-reveal));auto dl=ImGui::GetWindowDrawList();
    auto text=[&](ImVec2 at,const char* label,bool muted=false,float size=16.f){draw_text(dl,font_semibold(size),at,mo::with_alpha(muted?c_muted_foreground:c_foreground,alpha),label);};
    draw_text_tracked(dl,font_semibold(24),origin,mo::with_alpha(c_foreground,alpha),page==shell_page::camera?"Camera Focus":page==shell_page::health?"Health bars":"Connection",px(-.4f));
    auto card=[&](ImRect box,const char* title,icons::id icon){
        rounded_panel::draw(dl,box.Min,box.Max,mo::with_alpha(c_card,(app.settings.transparent_frame?1.f:.72f)*alpha),px(16));
        dl->AddRect(box.Min+px(.5f,.5f),box.Max-px(.5f,.5f),mo::with_alpha(c_border,alpha),px(16),px(1));
        icons::draw(icon,dl,box.Min+px(20,20),px(16),mo::with_alpha(c_muted_foreground,alpha));
        draw_text_tracked(dl,font_semibold(12),box.Min+px(46,21),mo::with_alpha(c_muted_foreground,alpha),title,px(.8f));
    };
    bool changed=false;
    ui_runtime::push_font(font_semibold(16));ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(c_foreground));
    if(page==shell_page::health) {
        float left=body.GetWidth()*.58f,gap=px(16),right=body.GetWidth()-left-gap;
        ImRect controls(origin+px(0,70),origin+ImVec2(left,px(494)));
        ImRect preview({controls.Max.x+gap,controls.Min.y},{body.Max.x,controls.Min.y+px(218)});
        ImRect effects({preview.Min.x,preview.Max.y+gap},{body.Max.x,controls.Max.y});
        card(controls,"DISPLAY",icons::id::eye);card(preview,"LIVE PREVIEW",icons::id::target);card(effects,"MARKERS & EFFECTS",icons::id::sparkles);
        auto toggle=[&](const char* id,ImVec2 pos,const char* title,bool& value,float width){
            text(pos,title);
            changed|=switch_toggle(id,{pos.x+width-px(switch_w),pos.y+px(3)},&value);
        };
        auto pos=controls.Min+px(20,56);float inner=left-px(40);
        toggle("draw_health",pos,"Health bars",app.settings.enabled,inner);
        toggle("show_numbers",pos+px(0,32),"Health numbers",app.settings.numbers,inner);
        toggle("exclude_teammates",pos+px(0,64),"Enemies only",app.settings.exclude_teammates,inner);
        toggle("skeletons",pos+px(0,96),"Skeletons",app.settings.skeletons,inner);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Bone segments: visible blue, blocked purple.\nSkeletons show both states; Visible only still gates health bars and focus.\nGray means no collision mesh / camera data.");
        toggle("hitboxes",pos+px(0,128),"Hitboxes",app.settings.hitboxes,inner);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Model hitboxes follow animated bones: boxes, spheres and capsules.\nVisible/blocked colors below also apply here; gray means visibility is unknown.\nLike Skeletons, both visibility states are shown.");
        toggle("visible_only",pos+px(0,160),"Visible only",app.settings.visible_only,inner);
        auto slider=[&](const char* id,const char* title,float& value,float low,float high,float y){
            auto at=controls.Min+px(20,y);text(at,title);auto number=std::to_string(int(value))+" px";
            draw_text(dl,font_semibold(14),{controls.Max.x-px(20)-text_width(font_semibold(14),number.c_str()),at.y},mo::with_alpha(c_foreground,alpha),number.c_str());
            changed|=range_slider(id,at+px(0,19),inner,&value,low,high);
        };
        slider("width","Width",app.settings.width,40,200,260);slider("height","Height",app.settings.height,3,16,304);slider("gap","Head gap",app.settings.gap,2,40,348);
        text(controls.Min+px(20,395),"Health colors",true,12);
        ImGui::SetCursorScreenPos({controls.Max.x-px(138),controls.Min.y+px(390)});
        changed|=ImGui::ColorEdit3("##healthy",&app.settings.green.x,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);ImGui::SameLine();
        changed|=ImGui::ColorEdit3("##low",&app.settings.amber.x,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);ImGui::SameLine();
        changed|=ImGui::ColorEdit3("##critical",&app.settings.red.x,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
        ImVec2 centre{preview.GetCenter().x,preview.Min.y+px(100)};
        dl->PushClipRect(preview.Min+px(8,44),{preview.Max.x-px(8),preview.Min.y+px(178)},true);
        auto silhouette=mo::with_alpha(c_muted_foreground,.22f*alpha);
        dl->AddCircleFilled(centre,px(15),silhouette,40);
        dl->AddRectFilled(centre+px(-27,24),centre+px(27,65),silhouette,px(16));
        bool live=!snap.players.empty()&&snap.players.front().maximum>0;
        int health=live?snap.players.front().health:740,maximum=live?snap.players.front().maximum:1000;
        if(app.settings.skeletons) {
            auto color=ImGui::ColorConvertFloat4ToU32(app.settings.skeleton_visible);
            static const std::vector<std::string> names={"pelvis","spine_2","neck","head","head_end","clavicle_L","arm_upper_L","arm_lower_L","hand_L","clavicle_R","arm_upper_R","arm_lower_R","hand_R","leg_upper_L","leg_lower_L","ankle_L","leg_upper_R","leg_lower_R","ankle_R"};
            static const auto edges=skeleton_edges(names,{-1,0,1,2,3,1,5,6,7,1,9,10,11,0,13,14,0,16,17});
            constexpr std::array<ImVec2,19> joints={{{0,61},{0,38},{0,18},{0,0},{0,-12},{-16,28},{-26,30},{-35,50},{-38,69},{16,28},{26,30},{35,50},{38,69},{-13,64},{-19,78},{-20,90},{13,64},{19,78},{20,90}}};
            for(auto [a,b]:edges)dl->AddLine(centre+px(joints[a].x,joints[a].y),centre+px(joints[b].x,joints[b].y),color,px(1.5f));
        }
        if(app.settings.hitboxes) {
            Player example;
            for(const auto& model:std::array<ModelHitbox,4>{{{{0,-5,0},{0,3,0},10,-1,1,HitboxShape::Capsule},{{0,29,0},{0,51,0},17,-1,2,HitboxShape::Capsule},{{-24,30,0},{-34,61,0},6,-1,4,HitboxShape::Capsule},{{24,30,0},{34,61,0},6,-1,5,HitboxShape::Capsule}}}) {
                Hitbox box;if(world_hitbox(model,{0,0,0,1,0,0,0,1},box)){box.visibility_known=box.visible=true;example.hitboxes.push_back(box);}
            }
            Matrix projection{1.f/100,0,0,0,0,-1.f/100,0,0,0,0,0,0,0,0,0,1};
            draw_hitboxes(dl,example,projection,px(200),px(200),app.settings,ui_runtime::scale,centre-px(100,100));
        }
        if(app.settings.enabled)draw_bar(dl,{centre.x,centre.y-px(22)},health,maximum,health_fraction(health,maximum),app.settings,ui_runtime::scale);
        if(app.settings.head_dot)draw_dot(dl,{centre.x,centre.y},0,ui_runtime::scale);
        if(app.settings.body_dot)draw_dot(dl,{centre.x,centre.y+px(38)},1,ui_runtime::scale);
        if(app.settings.pelvis_dot)draw_dot(dl,{centre.x,centre.y+px(61)},2,ui_runtime::scale);
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(preview.Min+px(20,186));
        constexpr auto color_flags=ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel;
        changed|=ImGui::ColorEdit3("##skeleton_visible",&app.settings.skeleton_visible.x,color_flags);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Visible skeleton / hitbox color");
        ImGui::SameLine();ImGui::TextUnformatted("Visible");ImGui::SameLine();
        changed|=ImGui::ColorEdit3("##skeleton_hidden",&app.settings.skeleton_hidden.x,color_flags);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Blocked skeleton / hitbox color");
        ImGui::SameLine();ImGui::TextUnformatted("Blocked");
        toggle("head_dot",effects.Min+px(20,44),"Head dot",app.settings.head_dot,right-px(40));
        toggle("body_dot",effects.Min+px(20,80),"Body dot",app.settings.body_dot,right-px(40));
        toggle("pelvis_dot",effects.Min+px(20,116),"Pelvis dot",app.settings.pelvis_dot,right-px(40));
        toggle("glass_cursor",effects.Min+px(20,152),"Glass cursor",app.settings.glass,right-px(40));
        ImGui::SetCursorScreenPos(origin+px(0,509));
        auto action=[&](const char* id,const char* label,float width){auto state=ui_runtime::animation_state<stateful_button_state>(ImGui::GetID(id));stateful_button_update(*state,btn_idle,label,ImGui::GetIO().DeltaTime);return stateful_button_draw(id,*state,btn_idle,ImGui::GetCursorScreenPos(),px(width),false);};
        if(action("defaults","Reset defaults",146)){app.settings=Settings{};set_dark(true);changed=true;}
        ImGui::SetCursorScreenPos({body.Max.x-px(138),origin.y+px(509)});if(action("close","Close menu",138))app.hide_settings();
    } else if(page==shell_page::camera) {
        ImRect status(origin+px(0,60),{body.Max.x,origin.y+px(210)});card(status,"FOCUS INPUT",icons::id::crosshair_simple);
        badge("input_ready",dl,status.Min+px(20,51),app.makcu.ready()?"Makcu ready":"Waiting for input",app.makcu.ready()?badge_good:badge_warn);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",app.makcu.status().c_str());
        ImGui::SetCursorScreenPos(status.Min+px(180,47));
        if(ImGui::Button("Hero / abilities",px(140,30)))ImGui::OpenPopup("Hero ability settings");
        ImGui::SetNextWindowSizeConstraints(px(370,0),px(440,FLT_MAX));
        if(ImGui::BeginPopup("Hero ability settings")) {
            ImGui::Text("Detected hero: %s",snap.hero.name.c_str());
            if(snap.hero.valid)ImGui::Text("Hero ID: %d",snap.hero.id);
            ImGui::TextWrapped("%s",snap.hero.ability_status.c_str());
            ImGui::Separator();
            changed|=ImGui::Checkbox("Vindicta sniper speed override",&app.settings.sniper_speed_override);
            ImGui::BeginDisabled(!app.settings.sniper_speed_override);
            ImGui::SetNextItemWidth(px(330));
            changed|=ImGui::SliderFloat("##sniper_focus_speed",&app.settings.sniper_focus_speed,1.f,120.f,"Sniper focus speed: %.0f");
            ImGui::EndDisabled();
            ImGui::TextWrapped("Uses this camera-focus speed while Vindicta is scoped with Assassinate. Unscoping or changing hero restores your regular focus speed.");
            ImGui::Separator();
            if(snap.projection.valid) {
                ImGui::Text("Live view: %.1f x %.1f degrees",snap.projection.horizontal_fov,snap.projection.vertical_fov);
                ImGui::Text("Render aspect ratio: %.3f",snap.projection.aspect);
            }else ImGui::TextUnformatted("Waiting for the game's render view");
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos({status.Max.x-px(305),status.Min.y+px(47)});
        if(ImGui::Button("Focus area",px(120,30)))ImGui::OpenPopup("Focus area settings");
        text({status.Max.x-px(165),status.Min.y+px(53)},"Show FOV",true,14);
        changed|=switch_toggle("show_focus_area",{status.Max.x-px(65),status.Min.y+px(54)},&app.settings.show_focus_area[int(app.settings.focus_area.mode)]);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Draw the current focus area, independently of Focus visuals.\nEach mode remembers its own drawing setting.");
        ImGui::SetNextWindowSizeConstraints(px(370,0),px(440,FLT_MAX));
        if(ImGui::BeginPopup("Focus area settings")) {
            constexpr const char* modes[]={"Center FOV","Target box (3D)","Target circle (2D)"};
            int mode=int(app.settings.focus_area.mode);
            ImGui::TextUnformatted("Camera focus area");ImGui::Separator();
            ImGui::SetNextItemWidth(px(330));
            if(ImGui::Combo("##focus_area_mode",&mode,modes,3)){app.settings.focus_area.mode=static_cast<FocusAreaMode>(mode);changed=true;}
            ImGui::SetNextItemWidth(px(330));
            if(mode==0) {
                changed|=ImGui::SliderFloat("##focus_fov_percent",&app.settings.focus_area.center_percent,1.f,80.f,"Radius: %.1f%%");
                ImGui::TextWrapped("Radius as a percentage of the shorter screen dimension.");
            }else {
                changed|=ImGui::SliderFloat("##focus_radius_meters",&app.settings.focus_area.radius_meters,.1f,50.f,"Radius: %.1f m");
                if(mode==1)ImGui::TextWrapped("World-aligned box around the selected bone. The crosshair ray must intersect it.");
                else ImGui::TextWrapped("Screen circle around the selected bone. Its meter radius is projected at the target's depth.");
                if(mode==1)ImGui::Text("Full box size: %.1f m per side",app.settings.focus_area.radius_meters*2);
            }
            changed|=ImGui::Checkbox("Draw this area",&app.settings.show_focus_area[mode]);
            ImGui::Separator();
            ImGui::TextWrapped("Closest to the crosshair wins. Equal distances prefer lower health, then a random target. A valid selection stays locked while held.");
            ImGui::EndPopup();
        }
        char hero_label[200]{};
        snprintf(hero_label,sizeof(hero_label),"Hero: %s%s",snap.hero.name.c_str(),vindicta(snap.hero)?(snap.hero.sniper_valid?(snap.hero.sniper_scoped?"  /  Assassinate scoped":"  /  Assassinate idle"):"  /  Waiting for sniper state"):"");
        text(status.Min+px(20,88),hero_label,true,14);
        char area_hint[160]{};
        if(app.settings.focus_area.mode==FocusAreaMode::CenterFov)snprintf(area_hint,sizeof(area_hint),"Hold either side button. Center FOV radius: %.1f%% of the shorter screen dimension.",app.settings.focus_area.center_percent);
        else snprintf(area_hint,sizeof(area_hint),"Hold either side button. %s around selected bone: %.1f m radius.",app.settings.focus_area.mode==FocusAreaMode::TargetBox3D?"3D box":"2D circle",app.settings.focus_area.radius_meters);
        text(status.Min+px(20,119),area_hint,true,12);
        auto camera_pos=origin+px(20,236);text(camera_pos,"Camera focus");
        changed|=switch_toggle("replay_focus",{camera_pos.x+px(150),camera_pos.y+px(3)},&app.settings.replay_focus);
        text(origin+px(285,236),"Focus visuals",true,14);
        changed|=switch_toggle("focus_visuals",origin+px(435,239),&app.settings.focus_visuals);
        constexpr const char* bones[]={"Head","Body","Pelvis"};
        ImGui::SetCursorScreenPos({body.Max.x-px(130),camera_pos.y-px(4)});
        if(ImGui::Button(bones[app.settings.focus_bone],px(110,30))){app.settings.focus_bone=(app.settings.focus_bone+1)%3;changed=true;}
        text(origin+px(20,275),"Sandbox & bots",true,14);
        changed|=switch_toggle("debug_focus",origin+px(170,278),&app.settings.debug_focus);
        text(origin+px(285,275),"Free movement",true,14);
        constexpr const char* movement_modes[]={"Off","V1: Bone line","V2: Full hitboxes"};
        int movement=app.settings.free_focus?int(app.settings.free_focus_mode)+1:0;
        ImGui::SetCursorScreenPos({body.Max.x-px(225),origin.y+px(269)});ImGui::SetNextItemWidth(px(205));
        if(ImGui::Combo("##free_focus_mode",&movement,movement_modes,3)) {
            app.settings.free_focus=movement!=0;
            if(movement)app.settings.free_focus_mode=static_cast<FreeMovementMode>(movement-1);
            changed=true;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("V1: nearest point on head -> body -> pelvis.\nV2: move freely inside any animated hitbox, including arms and legs.\nOutside the volume, correct toward its nearest projected edge.\nV2 reads hitboxes even with Hitboxes drawing off; soul orbs use their center.");
        text(origin+px(20,317),"Focus speed",true,14);
        auto scoped_speed=sniper_speed_active(snap.hero,app.settings.sniper_speed_override);
        auto speed_label=std::to_string(int(hero_focus_speed(snap.hero,app.settings.sniper_speed_override,app.settings.focus_speed,app.settings.sniper_focus_speed)))+(scoped_speed?" (sniper)":"");
        text({body.Max.x-px(scoped_speed?130.f:45.f),origin.y+px(317)},speed_label.c_str(),false,14);
        changed|=range_slider("focus_speed",origin+px(20,341),body.GetWidth()-px(40),&app.settings.focus_speed,1.f,120.f);
        text(origin+px(20,379),"Prediction",true,14);
        changed|=switch_toggle("prediction",origin+px(170,382),&app.settings.prediction);
        text(origin+px(285,379),"Auto weapon speed",true,14);
        changed|=switch_toggle("auto_speed",{body.Max.x-px(70),origin.y+px(382)},&app.settings.auto_speed);
        text(origin+px(20,421),app.settings.auto_speed?"Live primary bullet speed (units/s)":"Manual projectile speed (units/s)",true,12);
        auto projectile_label=app.settings.auto_speed?(snap.weapon.valid?std::to_string(int(snap.weapon.speed)):"--"):std::to_string(int(app.settings.projectile_speed));
        text({body.Max.x-px(80),origin.y+px(421)},projectile_label.c_str(),false,14);
        if(app.settings.auto_speed){
            char detail[160]{};
            if(snap.weapon.valid)snprintf(detail,sizeof(detail),"Base %.0f  /  Bonus %+.0f%%  /  Movement inheritance %.2f",snap.weapon.base_speed,snap.weapon.bonus_percent,snap.weapon.inheritance);
            else snprintf(detail,sizeof(detail),"%s",snap.weapon.status.c_str());
            text(origin+px(20,453),detail,true,12);
        }else{
            changed|=range_slider("projectile_speed",origin+px(20,445),body.GetWidth()-px(40),&app.settings.projectile_speed,1000.f,100000.f);
            text(origin+px(20,480),"Inherit shooter velocity",true,12);
            changed|=switch_toggle("inherit_velocity",origin+px(200,480),&app.settings.inherit_velocity);
        }
        if(app.settings.auto_speed)text(origin+px(20,480),!app.settings.free_focus?"Hold either side button to focus the selected bone":app.settings.free_focus_mode==FreeMovementMode::Hitboxes?"V2: hold side button; move freely inside target hitboxes":"V1: hold side button; move freely along the connected bones",true,12);
        ImGui::SetCursorScreenPos({body.Max.x-px(130),origin.y+px(478)});
        if(ImGui::Button("Target types",px(110,30)))ImGui::OpenPopup("Focus targets");
        if(ImGui::BeginPopup("Focus targets")) {
            changed|=ImGui::Checkbox("Players",&app.settings.focus_players);
            changed|=ImGui::Checkbox("Minions",&app.settings.focus_minions);
            changed|=ImGui::Checkbox("Soul orbs",&app.settings.focus_orbs);
            ImGui::Separator();
            changed|=ImGui::Checkbox("Enemies only",&app.settings.exclude_teammates);
            changed|=ImGui::Checkbox("Visible only",&app.settings.visible_only);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("These filters also apply to health bars and markers.");
            int minions=0,orbs=0;for(const auto& target:snap.focus_targets)if(target.kind==TargetKind::Minion)++minions;else if(target.kind==TargetKind::SoulOrb)++orbs;
            ImGui::Separator();ImGui::Text("Available: %zu players / %d minions / %d orbs",snap.players.size(),minions,orbs);
            ImGui::TextUnformatted("Hold a side button and enter a valid focus area.");
            ImGui::EndPopup();
        }
        text(origin+px(20,517),
             !app.settings.replay_focus ? "Focus disabled"
             : app.settings.visible_only && !snap.visibility ? "Waiting for map collision mesh"
             : app.camera.occluded ? "Focus point blocked"
             : app.camera.input_blocked ? "Input failed; release the side button to retry"
             : app.settings.free_focus && app.settings.free_focus_mode==FreeMovementMode::Hitboxes && app.settings.focus_players && std::none_of(snap.players.begin(),snap.players.end(),[](const auto& player){return !player.hitboxes.empty();}) ? "V2 waiting for player hitboxes"
             : app.camera.lost ? "Hold and move into a valid focus area"
             : app.settings.prediction
                ? (app.settings.auto_speed && !snap.weapon.valid
                    ? "Prediction waiting for live weapon data"
                    : app.camera.prediction_active
                        ? "Prediction active"
                        : "Prediction waiting for valid motion / intercept")
                : "Prediction off",
             true, 12);
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
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Install new/changed maps and create a fresh schema + SDK dump.\nDeadlock must be open. Windows asks for administrator approval.\nA new binary patch may still require a reader profile review.");
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
        } else text(updates.Min+px(20,122),"Maps reload automatically. Reports include schema compatibility checks.",true,12);
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
        ImGui::TextDisabled("Sidebar, header and empty frame");
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
        app.settings.head_dot=app.settings.body_dot=app.settings.pelvis_dot=true;
        app.settings.skeletons=app.settings.hitboxes=true;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"marker-settings-preview.png",true);
        app.menu_page=solace::shell_page::camera;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"camera-settings-preview.png",true);
        app.settings.free_focus=true;app.settings.free_focus_mode=FreeMovementMode::Hitboxes;
        for(int i=0;i<20&&ok;++i){ok=begin_frame(app.settings_window);if(ok){ImGui::GetIO().DeltaTime=1.f/60;draw_settings(app,s);render_frame(app.settings_window);}}
        if(ok)ok=capture(app.settings_window,executable_directory()/L"free-movement-v2-preview.png",true);
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
            app.settings.focus_area.mode=static_cast<FocusAreaMode>(mode);
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
        for(int i=0;i<3&&ok;++i){ok=begin_frame(app.bar_window);if(ok){draw_bar(ImGui::GetBackgroundDrawList(),{160,110},740,1000,.74f,Settings{},1);render_frame(app.bar_window);}}
        if(ok)ok=capture(app.bar_window,executable_directory()/L"bar-preview.png",true);
        if(ok){ok=begin_frame(app.bar_window);if(ok){Settings demo;draw_bar(ImGui::GetBackgroundDrawList(),{170,90},740,1000,.74f,demo,1);for(int i=0;i<3;++i)draw_dot(ImGui::GetBackgroundDrawList(),{125,55.f+35.f*i},i,1);render_frame(app.bar_window);ok=capture(app.bar_window,executable_directory()/L"markers-preview.png",true);}}
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
        Settings area_demo;area_demo.show_focus_area.fill(true);area_demo.focus_visuals=false;area_demo.visible_only=false;
        for(int mode=0;mode<3&&ok;++mode) {
            area_demo.focus_area.mode=static_cast<FocusAreaMode>(mode);
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
        app.settings.show_focus_area.fill(true);
        SetWindowPos(app.bar_window.hwnd,nullptr,0,0,640,360,SWP_NOACTIVATE|SWP_NOZORDER);
        for(int i=0;i<2200&&ok;++i) {
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            app.settings.glass=(i/50)%2==0;
            app.settings.transparent_frame=(i/100)%2==0;
            app.menu_page=static_cast<solace::shell_page>((i/50)%3);
            app.settings.head_dot=app.settings.body_dot=app.settings.pelvis_dot=true;
            app.settings.focus_area.mode=static_cast<FocusAreaMode>((i/50)%3);
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
int run(bool diagnostics,bool settings_at_start,bool shutdown_test=false,bool trace_frames=false) {
    if(!shutdown_test) {
        if(!SetPriorityClass(GetCurrentProcess(),high_priority?HIGH_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS))
            std::cerr<<"Could not apply process priority: "<<GetLastError()<<"\n";
    }
    HANDLE single=CreateMutexW(nullptr,FALSE,shutdown_test?L"Local\\DeadlockHealthOverlayTestSingleton":L"Local\\DeadlockHealthOverlaySingleton");
    if(!single)return 1;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(single);return 0;}
    shutdown_complete=CreateEventW(nullptr,TRUE,FALSE,nullptr);SetConsoleCtrlHandler(console_handler,TRUE);
    SetConsoleTitleW(L"Deadlock Overlay - close console to exit");
    std::cout<<"Deadlock Overlay\nInsert: toggle menu. Drag menu header to move. Close this console to exit.\n";
    Application app;app.diagnostics=diagnostics;
    app.data_update.initialize(executable_directory());
    if(!create_surface(app,app.settings_window,false)||!create_surface(app,app.bar_window,true)) {
        MessageBoxW(nullptr,L"Overlay window or Direct3D initialization failed.",L"Deadlock Overlay",MB_ICONERROR);CloseHandle(single);return 1;
    }
    if(!shutdown_test)app.makcu.start();
    // Wait before preparing render frames; steering is processed before GPU pacing.
    std::jthread reader([&](std::stop_token stop) {
        GameReader game;FramePacer sample_pacer;unsigned revision{};while(!stop.stop_requested()) {
            auto next_revision=app.data_revision.load();if(next_revision!=revision){game.refresh_data();revision=next_revision;}
            auto flags=app.read_options.load();auto s=game.sample({(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0});{std::lock_guard lock(app.mutex);app.snapshot=std::move(s);}
            sample_pacer.wait(app.sampling?app.sample_hz.load():5.0);
        }
    });
    std::jthread close_test;
    if(shutdown_test)close_test=std::jthread([] {Sleep(500);console_handler(CTRL_CLOSE_EVENT);});
    if(settings_at_start)app.open_settings();
    bool insert_down=false;uint64_t last_report{},next_display_check{},fps_epoch=GetTickCount64();DWORD last_pid{};
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
        bool hitbox_focus=app.settings.free_focus&&app.settings.free_focus_mode==FreeMovementMode::Hitboxes
            &&(app.settings.replay_focus||app.settings.focus_visuals)&&(app.settings.focus_players||app.settings.focus_minions);
        app.read_options.store((app.settings.skeletons?1u:0u)|(app.settings.focus_minions?2u:0u)|(app.settings.focus_orbs?4u:0u)|((app.settings.hitboxes||hitbox_focus)?8u:0u));
        auto focus_speed=hero_focus_speed(snapshot.hero,app.settings.sniper_speed_override,app.settings.focus_speed,app.settings.sniper_focus_speed);
        app.camera.update(app.makcu,snapshot,game,app.settings.replay_focus,app.settings.debug_focus,app.settings.exclude_teammates,app.show_settings,app.settings.focus_bone,focus_speed,app.settings.free_focus,app.settings.prediction,app.settings.projectile_speed,app.settings.inherit_velocity,app.settings.auto_speed,app.settings.visible_only,{app.settings.focus_players,app.settings.focus_minions,app.settings.focus_orbs},app.settings.focus_area,clock_ms(),app.settings.free_focus_mode);
        app.sampling=focused;
        // Input and foreground checks run before GPU pacing. A saturated queue is
        // polled with a bounded wait, without dropping a prepared render frame.
        auto& pacing_surface=IsWindowVisible(app.bar_window.hwnd)?app.bar_window:app.settings_window;
        auto queue_wait_start=clock_ms();
        if(focused&&IsWindowVisible(pacing_surface.hwnd)&&!pacing_surface.renderer.wait_for_frame(4))continue;
        auto queue_wait_ms=clock_ms()-queue_wait_start;
        {std::lock_guard lock(app.mutex);snapshot=app.snapshot;}
        bool visible=focused&&(app.settings.enabled||app.settings.skeletons||app.settings.hitboxes||app.settings.head_dot||app.settings.body_dot||app.settings.pelvis_dot||show_focus_area(app.settings)||(app.settings.focus_visuals&&(app.settings.free_focus||app.settings.replay_focus)))&&fresh(GetTickCount64(),snapshot.time);
        if(last_pid!=snapshot.pid){app.displayed.clear();last_pid=snapshot.pid;}
        app.sampling=focused;app.bars=0;int drawn_hitboxes=0;bool rendered=false,bar_raised=false;
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
                draw_focus_area(draw,snapshot,app.settings,float(r.right),float(r.bottom),app.camera.target,scale);
                for(const auto& player:snapshot.players) {
                    if(!include_player(app.settings.exclude_teammates,snapshot.local_team,player.team))continue;
                    if(app.settings.skeletons)draw_skeleton(draw,player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale);
                    if(app.settings.hitboxes)drawn_hitboxes+=draw_hitboxes(draw,player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale);
                    bool free_visuals=app.settings.focus_visuals&&app.settings.free_focus&&app.settings.focus_players;
                    bool hitbox_visuals=free_visuals&&app.settings.free_focus_mode==FreeMovementMode::Hitboxes;
                    if(hitbox_visuals&&!app.settings.hitboxes)drawn_hitboxes+=draw_hitboxes(draw,player,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale,{},app.settings.visible_only);
                    if(app.settings.visible_only&&!player.visible)continue;
                    if(free_visuals&&!hitbox_visuals){
                        for(int i=0;i<2;++i){ScreenPoint a,b;
                            if(player.dot_valid[i]&&player.dot_valid[i+1]&&(!app.settings.visible_only||(player.dot_visible[i]&&player.dot_visible[i+1]))&&project(snapshot.matrix,player.dots[i],float(r.right),float(r.bottom),a)&&project(snapshot.matrix,player.dots[i+1],float(r.right),float(r.bottom),b))
                                draw->AddLine({a.x,a.y},{b.x,b.y},IM_COL32(100,210,230,180),2*scale);
                        }
                    }
                    ScreenPoint p;auto anchor=player.head;
                    if(app.settings.enabled&&project(snapshot.matrix,anchor,float(r.right),float(r.bottom),p)) {
                        float ratio=health_fraction(player.health,player.maximum);
                        auto [item,added]=app.displayed.try_emplace(player.handle,ratio);float& fill=item->second;
                        if(ratio>=fill)fill=ratio;else fill=std::max(ratio,fill-ImGui::GetIO().DeltaTime*2.5f);
                        draw_bar(draw,p,player.health,player.maximum,fill,app.settings,scale);++app.bars;
                    }
                    const bool dots[]={app.settings.head_dot,app.settings.body_dot,app.settings.pelvis_dot};
                    for(int i=0;i<3;++i)if((dots[i]||(free_visuals&&!hitbox_visuals))&&player.dot_valid[i]&&(!app.settings.visible_only||player.dot_visible[i])&&project(snapshot.matrix,player.dots[i],float(r.right),float(r.bottom),p))draw_dot(draw,p,i,scale);
                }
                if(app.settings.focus_visuals&&app.settings.free_focus&&app.settings.focus_minions)for(const auto& object:snapshot.focus_targets) {
                    if(object.kind!=TargetKind::Minion||!include_player(app.settings.exclude_teammates,snapshot.local_team,object.team))continue;
                    if(app.settings.free_focus_mode==FreeMovementMode::Hitboxes){drawn_hitboxes+=draw_hitboxes(draw,object,snapshot.matrix,float(r.right),float(r.bottom),app.settings,scale,{},app.settings.visible_only);continue;}
                    for(int i=0;i<2;++i) {
                        ScreenPoint a{},b{};
                        if(object.dot_valid[i]&&object.dot_valid[i+1]&&(!app.settings.visible_only||(object.dot_visible[i]&&object.dot_visible[i+1]))
                            &&project(snapshot.matrix,object.dots[i],float(r.right),float(r.bottom),a)&&project(snapshot.matrix,object.dots[i+1],float(r.right),float(r.bottom),b))
                            draw->AddLine({a.x,a.y},{b.x,b.y},IM_COL32(100,210,230,180),2*scale);
                    }
                }
                if(app.settings.focus_visuals&&app.camera.engaged&&!app.camera.lost&&app.camera.point_valid) {
                    ScreenPoint target_point{};
                    if(project(snapshot.matrix,app.camera.aim_point,float(r.right),float(r.bottom),target_point))
                        draw->AddCircle({target_point.x,target_point.y},9*scale,IM_COL32(255,200,70,240),24,2*scale);
                }
                // Bound animation state and discard values for despawned entities.
                for(auto it=app.displayed.begin();it!=app.displayed.end();) {
                    bool exists=false;for(const auto& p:snapshot.players)if(p.handle==it->first){exists=true;break;}
                    if(!exists)it=app.displayed.erase(it);else ++it;
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
        if(diagnostics&&now-last_report>=1000){report(snapshot,executable_directory()/L"session.json",app.bars,IsWindowVisible(app.bar_window.hwnd)!=FALSE,GetWindowLongPtrW(app.bar_window.hwnd,GWL_EXSTYLE),app.bar_window.hwnd,app.show_settings,app.target_hz,app.render_fps,drawn_hitboxes);
            visibility_report(snapshot,executable_directory()/L"visibility.json",app.settings.visible_only,app.camera.occluded);
            std::ofstream makcu_report(executable_directory()/L"makcu.json");
            makcu_report<<"{\"backend\":\"makcu\",\"connected\":"<<(app.makcu.ready()?"true":"false")
                <<",\"status\":\""<<escaped(app.makcu.status())<<"\",\"commands_sent\":"<<app.makcu.sent()
                <<",\"write_failures\":"<<app.makcu.failures()<<",\"superseded_commands\":"<<app.makcu.replaced()
                <<",\"free_movement_available\":true,\"free_movement_mode\":\""<<free_movement_name(app.settings.free_focus,app.settings.free_focus_mode)<<"\"}\n";
            std::ofstream camera_report(executable_directory()/L"camera.json");camera_report<<"{\"effective_speed\":"<<focus_speed<<",\"sniper_speed_active\":"<<(sniper_speed_active(snapshot.hero,app.settings.sniper_speed_override)?"true":"false")<<",\"sniper_focus_speed\":"<<app.settings.sniper_focus_speed<<",\"replay\":"<<(snapshot.replay?"true":"false")<<",\"practice\":"<<(snapshot.practice?"true":"false")<<",\"debug_enabled\":"<<(app.settings.debug_focus?"true":"false")<<",\"enabled\":"<<(app.settings.replay_focus?"true":"false")<<",\"speed\":"<<app.settings.focus_speed<<",\"bone\":"<<app.settings.focus_bone<<",\"target\":"<<app.camera.target<<",\"engaged\":"<<(app.camera.engaged?"true":"false")<<",\"lost\":"<<(app.camera.lost?"true":"false")<<",\"moves\":"<<app.camera.moves<<",\"free_movement\":"<<(app.settings.free_focus?"true":"false")<<",\"free_movement_mode\":\""<<free_movement_name(app.settings.free_focus,app.settings.free_focus_mode)<<"\",\"hitbox_index\":"<<app.camera.hitbox_index<<",\"inside_hitbox\":"<<(app.camera.inside_hitbox?"true":"false")<<",\"line_position\":"<<app.camera.line_position<<",\"target_kind\":\""<<target_kind_name(app.camera.kind)<<"\""<<",\"prediction_enabled\":"<<(app.settings.prediction?"true":"false")<<",\"prediction_active\":"<<(app.camera.prediction_active?"true":"false")<<",\"projectile_speed\":"<<(app.settings.auto_speed?snapshot.weapon.speed:app.settings.projectile_speed)<<",\"auto_speed\":"<<(app.settings.auto_speed?"true":"false")<<",\"weapon_valid\":"<<(snapshot.weapon.valid?"true":"false")<<",\"weapon_handle\":"<<snapshot.weapon.handle<<",\"weapon_base_speed\":"<<snapshot.weapon.base_speed<<",\"weapon_bonus_percent\":"<<snapshot.weapon.bonus_percent<<",\"weapon_inheritance\":"<<snapshot.weapon.inheritance<<",\"weapon_status\":\""<<escaped(snapshot.weapon.status)<<"\",\"flight_time\":"<<app.camera.flight_time<<",\"local_velocity_valid\":"<<(snapshot.local_velocity_valid?"true":"false")<<",\"input_failures\":"<<app.camera.input_failures<<",\"focus_area_mode\":"<<int(app.settings.focus_area.mode)<<",\"focus_fov_percent\":"<<app.settings.focus_area.center_percent<<",\"focus_radius_meters\":"<<app.settings.focus_area.radius_meters<<",\"show_focus_area\":"<<(show_focus_area(app.settings)?"true":"false")<<"}\n";
            if(timing_frames){std::ofstream perf(executable_directory()/L"frame-performance.json");perf<<"{\"frames\":"<<timing_frames<<",\"priority_class\":"<<GetPriorityClass(GetCurrentProcess())<<",\"composition\":"<<(app.bar_window.renderer.composition()?"true":"false")<<",\"busy_bar_presents\":"<<app.bar_window.busy_presents<<",\"busy_menu_presents\":"<<app.settings_window.busy_presents<<",\"menu_open\":"<<(app.show_settings?"true":"false")<<",\"work_ms\":"<<work_sum/timing_frames<<",\"submit_ms\":"<<submit_sum/timing_frames<<",\"present_ms\":"<<present_sum/timing_frames<<",\"queue_wait_ms\":"<<queue_wait_sum/timing_frames<<",\"wait_ms\":"<<wait_sum/timing_frames<<",\"max_work_ms\":"<<work_max<<",\"max_present_ms\":"<<present_max<<",\"reader_ms\":"<<snapshot.sample_us/1000<<"}\n";}
            work_sum=wait_sum=submit_sum=present_sum=queue_wait_sum=work_max=present_max=0;timing_frames=0;
            std::ofstream trace(executable_directory()/L"frame-trace.json");trace<<"{\"status\":\""<<escaped(app.frame_status)<<"\",\"enabled\":"<<(trace_frames?"true":"false")<<",\"error\":"<<game_frames.error()<<",\"events\":"<<game_frames.count()<<",\"delivery_delay_ms\":"<<game_frames.delay_ms()<<"}\n";last_report=now;}
        auto wait_start=clock_ms();if(!focused||!game_frames.wait(app.target_hz))render_pacer.wait(focused?app.target_hz:20.0);if(rendered)wait_sum+=clock_ms()-wait_start;
    }
    app.makcu.stop();app.camera.release();
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
        while(!device.ready()&&GetTickCount64()<deadline)Sleep(10);
        bool connected=device.ready();
        // Exercise several heartbeat replies, including prompt consumption, without moving.
        if(connected){Sleep(3500);connected=device.ready();}
        auto status=device.status();device.stop();
        std::ofstream out(overlay::executable_directory()/L"makcu-probe.json");
        out<<"{\"connected\":"<<(connected?"true":"false")<<",\"status\":\""<<overlay::escaped(status)
            <<"\",\"movement_commands_sent\":"<<device.sent()<<"}\n";
        return connected?0:2;
    }
    bool probe=false,diagnostics=false,settings=false,render_test=false,shutdown_test=false,benchmark=false,frame_probe=false,trace_frames=false,resource_test=false,extended=false;
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--self-test")return overlay::self_test();if(arg=="--visibility-test")return overlay::visibility_test();if(arg=="--render-test")render_test=true;if(arg=="--shutdown-test")shutdown_test=true;if(arg=="--probe")probe=true;if(arg=="--diagnostics")diagnostics=true;if(arg=="--settings")settings=true;}
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--benchmark")benchmark=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--frame-trace-probe")frame_probe=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--game-frames")trace_frames=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--resource-test")resource_test=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--extended-read")extended=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--legacy-renderer")overlay::prefer_composition=false;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--normal-priority")overlay::high_priority=false;
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
            for(int i=0;i<8;++i){s=game.sample({true,true,true,true});if(s.time&&s.controllers)break;Sleep(200);}
            overlay::report(s,overlay::executable_directory()/L"probe.json");
            overlay::visibility_report(s,overlay::executable_directory()/L"visibility-probe.json",true,false);
            RECT bounds{};GetClientRect(overlay::game_window(s.pid),&bounds);
            overlay::report_hitbox_focus(s,float(bounds.right),float(bounds.bottom),overlay::executable_directory()/L"hitbox-focus-probe.json");
            return s.time&&s.controllers?0:2;
        }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(resource_test)return overlay::resource_test();if(render_test)return overlay::render_test();return overlay::run(diagnostics||shutdown_test,settings,shutdown_test,trace_frames);
    } catch(const std::exception& e) {std::ofstream out(overlay::executable_directory()/L"error.log",std::ios::app);out<<e.what()<<'\n';return 1;}
}



