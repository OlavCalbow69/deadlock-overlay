// Focused regression checks for the external merge. No game or input device is opened.
#include "overlay/adaptive_focus.h"
#include "overlay/external_data.h"
#include "overlay/external_settings.h"
#include "overlay/preset_store.h"
#include "overlay/assassinate_data.h"
#include "overlay/parry_settings.h"
#include "overlay/render_limit.h"
#include "overlay/x2_target_lock.h"
#include "overlay/world_data.h"
#include <iostream>
#include <limits>
using namespace overlay;
static unsigned checked{},failed{};
static void check(bool condition,const char* what){++checked;if(!condition){++failed;std::cerr<<what<<'\n';}}
static bool close_to(float a,float b,float e=.001f){return std::abs(a-b)<e;}
struct PresetFixture:CameraFocusSettings {ExternalSettings extras;CameraFocusSettings vindicta_ult;AutoParrySettings auto_parry;VindictaAutoSelectSettings vindicta_auto_select;};
int main(int argc,char** argv) {
    ExternalSettings removed;std::istringstream old_value("1");check(!read_external_setting(removed,"extra_active_reload",old_value),"Retired Active Reload preferences are ignored");check(!read_external_setting(removed,"extra_macro_enabled",old_value),"Retired script preferences are ignored");check(!read_external_setting(removed,"extra_boxes",old_value),"Retired imported visual preferences are ignored");
    ExternalSettings settings;check(!settings.crosshair&&!settings.spectators,"Imported features must start disabled");
    settings.render_cap=144;settings.crosshair_color={.1f,.2f,.3f,.4f};
    std::ostringstream saved;write_external_settings(saved,settings);ExternalSettings roundtrip;
    std::istringstream source(saved.str());std::string row;while(std::getline(source,row)){auto at=row.find('=');std::istringstream value(row.substr(at+1));check(read_external_setting(roundtrip,row.substr(0,at),value),"External setting must reload");}
    check(close_to(roundtrip.render_cap,144)&&close_to(roundtrip.crosshair_color.w,.4f),"External settings round trip");
    roundtrip.crosshair_size=std::numeric_limits<float>::quiet_NaN();roundtrip.render_cap=-4;sanitize_external(roundtrip);check(std::isfinite(roundtrip.crosshair_size)&&roundtrip.render_cap==30,"Invalid numeric settings are sanitized");
    check(!settings.world&&!settings.minimap&&!settings.hero_icons&&!settings.player_distance,"New world and icon displays are opt-in");
    settings.minimap=true;settings.minimap_corner=1;settings.hero_icons=true;settings.world_range=80;
    std::ostringstream world_saved;write_external_settings(world_saved,settings);ExternalSettings world_loaded;std::istringstream world_input(world_saved.str());
    while(std::getline(world_input,row)){auto at=row.find('=');std::istringstream value(row.substr(at+1));read_external_setting(world_loaded,row.substr(0,at),value);}
    check(world_loaded.minimap&&!world_loaded.world&&world_loaded.hero_icons&&world_loaded.minimap_corner==1&&world_loaded.world_range==80,"Minimap-only and icon preferences survive restart");
    ScreenPoint uv;Vec3 map_min{-12000,-10000,-500},map_max{14000,18000,500};
    check(minimap_uv({-12000,18000,0},map_min,map_max,uv)&&close_to(uv.x,0)&&close_to(uv.y,0),"Map northwest corner aligns with the overview");
    check(minimap_uv({14000,-10000,0},map_min,map_max,uv)&&close_to(uv.x,1)&&close_to(uv.y,1),"Map southeast corner aligns with the overview");
    check(minimap_uv({1000,4000,100},map_min,map_max,uv)&&close_to(uv.x,.5f)&&close_to(uv.y,.5f),"Map center is independent of height and camera zoom");
    check(!minimap_uv({},map_min,map_min,uv)&&!minimap_uv({NAN,0,0},map_min,map_max,uv),"Invalid map bounds / positions are rejected");
    check(close_to(world_distance_meters({0,0,0},{1000,0,0}),25.4f),"Distances use Source world-unit meters");
    check(!GameAssetCatalog::safe_asset_path("../outside.png")&&!GameAssetCatalog::safe_asset_path("C:/outside.png")&&GameAssetCatalog::safe_asset_path("heroes/baba_sm_psd.png"),"Only local catalog PNG paths can be opened");
    BreakableHistory history;WorldObject crate;crate.kind=WorldKind::Breakable;crate.position={10,20,30};crate.subclass=1;crate.health=1;
    history.begin(1,"caldera",100);history.observe(crate,100,180);std::vector<WorldObject> ghosts;
    history.begin(1,"caldera",101);history.append_missing(ghosts,101);check(ghosts.empty(),"Disappearance alone never authorizes a destroyed-object timer");
    crate.health=0;crate.availability=WorldAvailability::Waiting;
    history.begin(1,"caldera",102);auto observed=history.observe(crate,102,180);check(observed.remaining<0,"One dead read does not start an estimate");
    history.begin(1,"caldera",103);observed=history.observe(crate,103,180);check(close_to(observed.remaining,180)&&!observed.authoritative,"Confirmed observed destruction is explicitly estimated");
    history.begin(1,"caldera",113);history.append_missing(ghosts,113);check(ghosts.size()==1&&close_to(ghosts[0].remaining,170),"Destroyed static objects retain their position and game-clock countdown");
    crate.health=1;crate.availability=WorldAvailability::Alive;crate.handle=123;history.begin(1,"caldera",120);observed=history.observe(crate,120,180);
    check(observed.remaining<0&&observed.availability==WorldAvailability::Alive,"A respawned full identity clears the countdown at the same static location");
    history.begin(1,"other",121);ghosts.clear();history.append_missing(ghosts,121);check(ghosts.empty(),"Map changes clear static-object history");
    BreakableHistory unknown;unknown.begin(2,"caldera",100);crate.availability=WorldAvailability::Waiting;unknown.observe(crate,100,180);unknown.begin(2,"caldera",101);observed=unknown.observe(crate,101,180);check(observed.remaining<0,"Joining while a box is already dead never invents a destruction time");
    CameraFocusSettings regular,ultimate;regular.tracking.enabled=true;regular.tracking.joint=10;regular.tracking.activation_key=VK_F7;regular.adaptive.area=AdaptiveAreaMode::PixelSquare;regular.adaptive.pixel_radius=170;
    ultimate.tracking.enabled=true;ultimate.tracking.joint=0;ultimate.tracking.toggle=true;std::ostringstream profiles;write_camera_settings(profiles,regular);write_camera_settings(profiles,ultimate,"vindicta_ult_");
    CameraFocusSettings read_regular,read_ultimate;std::istringstream input(profiles.str());while(std::getline(input,row)){auto at=row.find('=');auto key=row.substr(0,at);std::istringstream value(row.substr(at+1));if(key.starts_with("vindicta_ult_"))read_camera_setting(read_ultimate,key.substr(13),value);else read_camera_setting(read_regular,key,value);}
    check(read_regular.tracking.joint==10&&read_regular.tracking.activation_key==VK_F7&&read_ultimate.tracking.joint==0&&read_ultimate.tracking.toggle,"Regular and ultimate preferences remain independent");
    check(read_regular.adaptive.area==AdaptiveAreaMode::PixelSquare&&close_to(read_regular.adaptive.pixel_radius,170),"Pixel area settings persist");
    CameraFocusSettings migration;migration.free_focus=true;migration.free_focus_mode=FreeMovementMode::Hitboxes;sanitize_camera_settings(migration);check(adaptive_mode(migration)&&migration.adaptive.region==AdaptiveRegion::WholeBody,"Retired V2 preference still migrates correctly");
    check(int(FreeMovementMode::BoneLine)==0&&int(FreeMovementMode::Hitboxes)==1&&int(FreeMovementMode::Adaptive)==2,"Existing movement enum values are preserved");
    for(auto invalid:{"../escape","bad.ini","CON","LPT1"," trail","trail ","a/b",""})check(!valid_preset_name(invalid),"Reject unsafe preset names");
    check(valid_preset_name("Replay - 1")&&valid_preset_name("NUL_safe"),"Accept ordinary preset names");
    PresetFixture preset;preset.replay_focus=preset.auto_click=preset.vindicta_ult.replay_focus=preset.vindicta_ult.auto_click=preset.auto_parry.enabled=true;
    preset.vindicta_auto_select.enabled=true;preset.extras.crosshair=true;disable_preset_automation(preset);
    check(!preset.replay_focus&&!preset.auto_click&&!preset.vindicta_ult.replay_focus&&!preset.vindicta_ult.auto_click&&!preset.auto_parry.enabled&&!preset.vindicta_auto_select.enabled&&preset.extras.crosshair,"Preset loading disables all automation while keeping visual preferences");
    if(argc>1){std::filesystem::path scratch=argv[1];std::filesystem::create_directories(scratch);check(remember_preset(scratch,"Replay - 1")&&active_preset(scratch)=="Replay - 1","Remember named preset atomically");check(!remember_preset(scratch,"../outside")&&active_preset(scratch)=="Replay - 1","Invalid preset does not replace remembered selection");check(remember_preset(scratch,"")&&active_preset(scratch).empty(),"Clear remembered preset");}
    for(auto prefix:{std::string{},std::string("baba_"),std::string("rat_king_"),std::string("solomon_"),std::string("violet_")}){std::vector<std::string> names{"pet_arm_upper_l","pet_head"};for(auto name:joint_names)names.push_back(prefix+name);auto map=external_joint_map(names,prefix,2,4,5);check(map[4]==6&&map[15]==17&&map[0]==2,"Named joint lookup stays within selected rig");}
    Matrix matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};focus_pixel_width=1000;focus_pixel_height=1000;focus_reference_offset={};AdaptiveSettings area;area.area=AdaptiveAreaMode::PixelCircle;area.pixel_radius=50;
    check(adaptive_area_accepts(matrix,{9,0,100},area,0)&&!adaptive_area_accepts(matrix,{11,0,100},area,0)&&adaptive_area_accepts(matrix,{11,0,100},area,0,true),"Pixel circle acquisition and retention");area.area=AdaptiveAreaMode::PixelSquare;check(adaptive_area_accepts(matrix,{9,9,100},area,0),"Pixel square includes its corners");focus_reference_offset={.1f,0};check(adaptive_area_accepts(matrix,{20,0,100},area,0),"Pixel area follows framing offset");focus_reference_offset={};
    Snapshot snapshot;snapshot.matrix=matrix;snapshot.projection=projection_info(matrix);FocusTarget target;target.handle=1;Hitbox sphere;sphere.shape=HitboxShape::Sphere;sphere.a=sphere.b={0,0,100};sphere.radius=10;sphere.region=AnatomicalRegion::Torso;sphere.source_id=1;target.hitboxes.push_back(sphere);target.preferred_joint=0;target.joints[0]={50,0,100};target.joints_valid=1;
    auto point=adaptive_closest(snapshot,target,area,0,{},false);check(point.valid&&point.inside&&point.error.distance==0,"Adaptive preference cannot pull the crosshair out of a valid region");
    sphere.a=sphere.b={25,0,100};Hitbox other=sphere;other.a=other.b={-25,0,100};target.hitboxes={sphere,other};point=adaptive_closest(snapshot,target,area,0,{},false);check(point.valid&&!point.inside,"Separate hitboxes preserve the gap between them");
    TrackingMotion motion;motion.enabled=true;motion.settings.ignore_x=true;motion.settings.max_counts=900;motion.error=100;motion.sample=1;motion.last=double(GetTickCount64()-16);long dx=100,dy=100;check(motion.apply(dx,dy)&&dx==0&&dy<=20,"Axis filter and rate limit bound movement");dx=10;dy=10;check(!motion.apply(dx,dy),"Same snapshot cannot advance response twice");motion.clear();check(motion.vx==0&&motion.vy==0&&motion.rx==0&&motion.ry==0,"Controller cancellation clears fractional and velocity state");
    mark_owned_key(VK_LSHIFT,UINT64_MAX);check(owned_input_until[VK_SHIFT]==UINT64_MAX&&owned_input_until[VK_RSHIFT]==UINT64_MAX,"Owned modifier cannot activate a generic modifier binding");mark_owned_key(VK_SHIFT,0);
    X2LockSession lock;lock.buttons(true,true);check(lock.state()==X2LockSession::State::Acquire,"Physical X2 press arms one lock acquisition");
    lock.lose("Lost");lock.buttons(false,true,true);lock.buttons(true,true);check(lock.active()&&lock.state()==X2LockSession::State::Spent,"Owned suppression cannot release and rearm a consumed X2 hold");
    lock.buttons(false,true);lock.buttons(true,true);check(lock.state()==X2LockSession::State::Acquire,"Confirmed physical release rearms X2 after suppression");
    X2LockSession injected;injected.buttons(false,true,true);injected.buttons(true,true);check(injected.state()==X2LockSession::State::Spent,"Injected-only X2 cannot create a lock acquisition");
    CalibrationBindings bindings;bindings.ultimate=calibration_binding("R");calibration_keys::Node root;root.children["config"].children["bindings"].children["R"].value="custom_alias";root.children["config"].children["bindings"].children["T"].value="+in_ability4";apply_binding_overrides(bindings,root);check(bindings.ultimate.vk=='T',"Exact console binding overrides take priority over aliases");
    check(makcu_shot_command(calibration_binding("MOUSE5"))=="km.click(5,1,35)\r\n"&&makcu_shot_command(calibration_binding("R"))=="km.press(21,35,0)\r\n","Bound shots use V4 button IDs and scheduled keyboard pulses");
    area.region=AdaptiveRegion::SelectedBone;target.preferred_joint=4;target.joints[4]={25,0,100};target.joints_valid|=1u<<4;target.hitboxes[1].source_id=2;
    check(adaptive_permits(target,target.hitboxes[0],area,0)&&!adaptive_permits(target,target.hitboxes[1],area,0),"Fixed named-joint zones use that joint's current hitbox");
    for(double cap:{60.,100.}){RenderLimit limiter;unsigned painted{};for(int i=0;i<240;++i)painted+=limiter.due(i*1000./240.,cap,240);check(painted==unsigned(cap),"Render cap limits paintings without reducing input iterations");}
    std::cout<<"{\"checks\":"<<checked<<",\"failures\":"<<failed<<",\"opens_game_or_input\":false}\n";return failed?1:0;
}
