// Focused pure calculations. Never opens the game, a COM port, or an input worker.
#include "overlay/vindicta_auto_select.h"
#include "overlay/preset_store.h"
#include "overlay/external_settings.h"
#include "overlay/camera_profile.h"
#include "overlay/parry_settings.h"
#include <sstream>
#include <iostream>
using namespace overlay;
unsigned checked{},failed{};
void check(bool condition,const char* label){++checked;if(!condition){++failed;std::cerr<<label<<'\n';}}
bool close_to(float a,float b,float epsilon=.002f){return std::abs(a-b)<epsilon;}
AssassinateReadState offense(){AssassinateReadState a;a.valid=a.cast_allowed=true;a.damage=183;a.low_bonus=400;a.low_threshold=50;a.head_bonus=20;a.minimum_power=.5f;a.full_charge=1;a.range=39370;a.sampled=100;return a;}
Hitbox sphere(Vec3 center,float radius,AnatomicalRegion region,int group,uint64_t id){Hitbox b;b.a=b.b=center;b.radius=radius;b.region=region;b.group=group;b.source_id=id;b.shape=HitboxShape::Sphere;return b;}
struct PresetFixture:CameraFocusSettings {ExternalSettings extras;CameraFocusSettings vindicta_ult;AutoParrySettings auto_parry;VindictaAutoSelectSettings vindicta_auto_select;};
int main() {
    auto a=offense();AssassinateDefense d;d.sampled=100;
    check(close_to(assassinate_damage(a,d,400,1000,.6f,false),466.4f),"60% ring is 80% full damage");
    check(close_to(assassinate_damage(a,d,300,1000,.3f,false),378.95f),"30% ring is 65% full damage");
    check(close_to(assassinate_damage(a,d,500,1000,1,false),183),"Exactly half health has no low-health bonus");
    check(close_to(assassinate_damage(a,d,490,1000,1,true),699.6f),"Head applies to both damage components");
    check(close_to(assassinate_required_charge(a,d,200,1000,false),0),"Already lethal at minimum power has no extra delay");
    auto required=assassinate_required_charge(a,d,400,1000,false);
    check(required>.37f&&required<.38f&&assassinate_damage(a,d,400,1000,required,false)>=400,"Earliest lethal charge is solved");
    check(!std::isfinite(assassinate_required_charge(a,d,900,2000,false)),"Unkillable at full charge is excluded");
    check(!assassinate_health_eligible(490,1000)&&assassinate_health_eligible(489,1000)&&!assassinate_health_eligible(0,1000),"Strict 49% health gate");
    check(!assassinate_health_eligible(1,0)&&!assassinate_health_eligible(1001,1000),"Invalid health is excluded");
    check(assassinate_acquirable(.6f)&&!assassinate_acquirable(.601f),"60% is acquisition eligibility");
    check(assassinate_head_hit(1)&&assassinate_head_hit(19)&&!assassinate_head_hit(2),"Real hit groups authorize head bonus");
    check(assassinate_cache_tick_usable(100,108)&&!assassinate_cache_tick_usable(100,117)&&!assassinate_cache_tick_usable(100,98),"Bounded restored prediction clock is handled without accepting old cache ticks");
    d.resistance=30;a.piercing=20;
    check(close_to(assassinate_damage(a,d,400,1000,1,false),583*.76f),"Piercing multiplies positive resistance");
    d.resistance=-20;check(close_to(assassinate_damage(a,d,400,1000,1,false),583*1.2f),"Piercing does not alter negative resistance");
    d={};d.sampled=100;d.barrier=100;d.all_reduction=20;
    check(close_to(assassinate_damage(a,d,400,1000,1,false),(583-100)*.8f),"Barrier precedes final all-damage reduction");
    d.protected_state=true;check(assassinate_damage(a,d,400,1000,1,false)==0,"Immunity and death prevention veto firing");
    d={};d.sampled=100;a=offense();
    check(close_to(assassinate_ring(a,10.6f,10,0),.6f)&&assassinate_ring(a,12,10,0)==1&&assassinate_ring(a,10,0,0)==0,"Scope timing uses ring fill and valid start");
    AssassinateScopeContinuity scope;scope.reset(10);
    check(scope.observe(false,0,100)==AssassinateScopeObservation::Waiting,"One predicted zero cannot discard scope ownership");
    check(scope.observe(true,10.02f,120)==AssassinateScopeObservation::Same&&close_to(scope.started,10.02f),"Server start-time correction preserves scope ownership");
    check(scope.observe(false,0,200)==AssassinateScopeObservation::Waiting&&scope.observe(false,0,299)==AssassinateScopeObservation::Waiting
        &&scope.observe(false,0,300)==AssassinateScopeObservation::Closed,"Scope exit requires a stable fresh closure");
    scope.reset(10);check(scope.observe(true,11,400)==AssassinateScopeObservation::Changed,"A different manual scope is not treated as ours");
    auto mesh=std::make_shared<VisibilityMesh>();mesh->assign({{{-10,-10,-100},{10,-10,-100},{0,10,-100}}});
    Snapshot s;s.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};s.projection=projection_info(s.matrix);s.camera_valid=true;s.visibility=mesh;s.local_team=2;s.assassinate=a;s.time=100;
    s.hero.sniper_scoped=true;s.hero.scope_start=10;s.helpers.game_time=10.3f;s.helpers.sniper_ready=true;
    Player p;p.handle=12;p.team=3;p.health=400;p.maximum=1000;p.assassinate_defense=d;
    p.hitboxes={sphere({0,0,300},18,AnatomicalRegion::Torso,2,1),sphere({0,25,300},6,AnatomicalRegion::Head,1,2)};
    auto sol=auto_select_solution(s,p,6,true);check(sol.valid&&sol.head&&sol.required<required,"Head can be chosen to reduce charge");
    p.health=200;sol=auto_select_solution(s,p,6,true);check(sol.valid&&sol.inside&&!sol.head,"Nearest already-lethal torso does not pull to the head");
    p.health=400;s.players={p};auto hit=crosshair_hit(s,1000,1000,{500,500});
    check(!auto_select_shot_lethal(s,p,hit,100),"A head estimate cannot authorize a nonlethal torso shot");
    p.health=350;s.players={p};hit=crosshair_hit(s,1000,1000,{500,500});
    check(auto_select_shot_lethal(s,p,hit,100),"External damage permits firing immediately at 30% ring");
    check(!auto_select_shot_lethal(s,p,hit,126),"Stale damage and geometry cannot authorize firing");
    auto friendly=p;friendly.handle=15;friendly.team=2;friendly.hitboxes={sphere({0,0,150},20,AnatomicalRegion::Torso,2,3)};s.players.push_back(friendly);
    check(!auto_select_shot_lethal(s,p,crosshair_hit(s,1000,1000,{500,500}),100),"A friendly in front blocks the automatic shot");
    s.players={p};p.hitboxes={sphere({0,0,300},18,AnatomicalRegion::Torso,2,1)};p.health=480;
    s.assassinate.damage=150;s.assassinate.low_bonus=350;
    check(!auto_select_solution(s,p,6,true).valid&&auto_select_solution(s,p,6,false).valid,"Retained targets may require more than 60% after healing");
    p.health=490;check(!auto_select_solution(s,p,6,false).valid,"Healing to 49% releases a retained target");
    p.health=200;p.hitboxes[0].region=AnatomicalRegion::Limb;check(!auto_select_solution(s,p,6,true).valid,"Arms and legs never enter the permitted region");
    p.hitboxes[0].region=AnatomicalRegion::Unknown;check(!auto_select_solution(s,p,6,true).valid,"Unknown model regions are excluded");
    p.hitboxes[0].region=AnatomicalRegion::Torso;p.hitboxes[0].a=p.hitboxes[0].b={90,0,300};
    check(!auto_select_solution(s,p,6,true).valid&&auto_select_solution(s,p,25,true).valid,"Acquisition uses configurable angular geometry");
    auto before=auto_select_solution(s,p,25,true);s.matrix[0]=2.3f;s.matrix[5]=2;
    auto after=auto_select_solution(s,p,25,true);check(before.valid&&after.valid&&close_to(before.angle,after.angle),"Zoom and aspect do not change angular eligibility");
    auto wall=std::make_shared<VisibilityMesh>();wall->assign({{{-1000,-1000,100},{1000,-1000,100},{1000,1000,100}},{{-1000,-1000,100},{1000,1000,100},{-1000,1000,100}}});s.visibility=wall;
    check(!auto_select_solution(s,p,25,true).valid,"Covered target is ineligible");
    AutoSelectSolution first,second;first.valid=second.valid=true;first.required=.2f;second.required=.3f;first.angle=5;second.angle=0;
    check(auto_select_better(first,second),"Least charge wins before aim distance");second.required=.2f;check(auto_select_better(second,first),"Aim distance breaks charge ties");
    first.angle=second.angle=0;first.health=50;second.health=80;check(auto_select_better(first,second),"Lowest health breaks exact geometric ties");
    VindictaAutoSelectSettings settings;check(!settings.enabled&&settings.angle==6&&settings.show_area,"New feature starts disabled with independent area defaults");
    settings.enabled=true;settings.angle=14;settings.speed=112;std::ostringstream output;write_auto_select_settings(output,settings);
    VindictaAutoSelectSettings restored;std::istringstream rows(output.str());std::string row;
    while(std::getline(rows,row)){auto at=row.find('=');std::istringstream value(row.substr(at+1));read_auto_select_setting(restored,row.substr(0,at),value);}
    check(restored.enabled&&restored.angle==14&&restored.speed==112,"Independent Auto Select settings round trip");
    PresetFixture preset;preset.vindicta_auto_select.enabled=true;disable_preset_automation(preset);check(!preset.vindicta_auto_select.enabled,"Named presets disable Auto Select");
    settings.angle=NAN;settings.speed=10000;sanitize_auto_select(settings);check(settings.angle==6&&settings.speed==140,"Invalid settings are bounded");
    MakcuClickPending request;check(!request.sequence,"Ordinary click requests have no autonomous lease");request.sequence=42;check(request.sequence==42,"Autonomous shots carry the existing owner's token");
    std::cout<<"{\"checks\":"<<checked<<",\"failures\":"<<failed<<",\"game_or_input_opened\":false}\n";return failed?1:0;
}
