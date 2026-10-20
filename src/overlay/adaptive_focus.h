#pragma once
#include "replay_camera.h"
#include "camera_profile.h"
#include "adaptive_response.h"
#include "adaptive_target_zone.h"
#include <bit>

namespace overlay {
inline bool adaptive_mode(const CameraFocusSettings& s){return s.free_focus&&s.free_focus_mode==FreeMovementMode::Adaptive;}
struct AdaptivePoint {
    Vec3 world{};AngularError error{};uint64_t source_id{};AnatomicalRegion region{};
    int index{-1};bool valid{},inside{},geometry{};float distance{};
};
// Normalize the projection's focal lengths so GJK minimizes tan(angular error),
// rather than pixels. This preserves the actual camera origin and perspective.
inline Matrix angular_projection(Matrix matrix) {
    for(int row:{0,4,12}) {
        const float length=std::sqrt(matrix[row]*matrix[row]+matrix[row+1]*matrix[row+1]+matrix[row+2]*matrix[row+2]);
        for(int i=0;i<4;++i)matrix[row+i]/=length;
    }
    return matrix;
}
inline AdaptivePoint adaptive_closest(const Snapshot& s,const FocusTarget& target,const AdaptiveSettings& settings,
    int bone,Vec3 lead,bool visible_only,uint64_t previous_id=0,bool inset=true) {
    CameraBasis basis;if(!camera_basis(s.matrix,basis)||!finite(lead))return {};
    auto allowed=[&](Vec3 point) {
        if(!visibility_allowed(visible_only,s.visibility,s.camera_position,s.camera_valid,sub(point,lead)))return false;
        return dot(lead,lead)<1e-10f||visibility_allowed(visible_only,s.visibility,s.camera_position,s.camera_valid,point);
    };
    if(target.kind==TargetKind::SoulOrb) {
        if(!target.dot_valid[1])return {};
        Vec3 point=add(target.dots[1],lead);auto error=point_error(basis,point);
        return {point,error,0,AnatomicalRegion::Unknown,-1,error.distance<90&&allowed(point),false,true,error.distance};
    }
    struct Candidate {AdaptivePoint point;Vec3 center;};
    std::array<Candidate,512> candidates{};size_t count{};bool geometry=false;
    const auto matrix=angular_projection(s.matrix);hitbox_focus::Projection projection(matrix,2,2,{1,1});
    for(size_t i=0;i<target.hitboxes.size()&&count<candidates.size();++i) {
        const auto& box=target.hitboxes[i];if(!adaptive_permits(target,box,settings,bone))continue;geometry=true;
        double time{};bool inside=hitbox_focus::intersect(box,lead,basis.origin,basis.forward,time);Vec3 world{};
        if(inside)world=add(basis.origin,mul(basis.forward,float(time)));
        else {hitbox_focus::Point point;if(!hitbox_focus::closest(projection,box,lead,point,1e-6))continue;world=point.world;}
        auto error=point_error(basis,world);if(!std::isfinite(error.distance)||error.distance>=90)continue;
        candidates[count++]={{world,error,box.source_id,box.region,int(i),false,inside,true},add(hitbox_center(box),lead)};
    }
    AdaptivePoint best,previous,preferred;best.geometry=geometry;
    auto consider=[&](AdaptivePoint point) {
        point.distance=point.error.distance;
        if(!best.valid||point.distance<best.distance)best=point;
        if(point.source_id==previous_id&&(!previous.valid||point.distance<previous.distance))previous=point;
        if(point.region==preferred_region(bone)&&(!preferred.valid||point.distance<preferred.distance))preferred=point;
    };
    auto stable=[&]() {
        const float nearest=best.distance;
        if(previous.valid&&previous.distance<=nearest+.1f)best=previous;
        else if(preferred.valid&&preferred.distance<=nearest+.1f)best=preferred;
        best.distance=nearest;return best;
    };
    // Any permitted visible intersection must remain free, even if another
    // candidate (or the previous preferred bone) lies just outside the union.
    for(size_t i=0;i<count;++i)if(candidates[i].point.inside&&allowed(candidates[i].point.world)) {
        auto point=candidates[i].point;point.valid=true;point.error={};
        consider(point);
    }
    if(best.valid)return stable();
    std::sort(candidates.begin(),candidates.begin()+count,[](const auto& a,const auto& b){return a.point.error.distance<b.point.error.distance;});
    for(size_t j=0;j<count;++j) {
        const auto& candidate=candidates[j];
        if(best.valid&&candidate.point.error.distance>best.distance+.1f)break;
        auto point=candidate.point;
        point.inside=false;
        auto attempt=[&](Vec3 world) {
            if(inset)world=add(world,mul(sub(candidate.center,world),.05f));
            auto error=point_error(basis,world);
            if((point.valid&&error.distance>=point.error.distance)||(best.valid&&error.distance>best.distance+.1f)||!allowed(world))return;
            point.world=world;point.error=error;point.valid=true;
        };
        attempt(candidate.point.world);
        if(!point.valid&&visible_only) {
            attempt(candidate.center);
            // Search exposed parts of this exact volume when its nearest edge is
            // covered. Every sampled point is inset and visibility checked in
            // both current and predicted space; gaps between volumes stay gaps.
            constexpr std::array<std::array<double,2>,8> directions={{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}};
            for(const auto& direction:directions) {
                hitbox_focus::Point support;
                if(projection.support(target.hitboxes[size_t(point.index)],lead,direction[0],direction[1],support))attempt(support.world);
            }
        }
        if(point.valid)consider(point);
    }
    return stable();
}
struct AdaptiveFocusSolution {
    uint32_t target{};TargetKind kind{};uint64_t source_id{};
    Vec3 current_point{},aim_point{},lead{};AngularError error{};
    ScreenPoint pixel_error{},requested_pixels{};long requested_x{},requested_y{};
    AnatomicalRegion region{};MotionSource motion_source{};
    float acquisition_angle{},flight_time{},data_age_ms{};
    bool valid{},inside{},prediction_active{},retained{},occluded{};
    const char* prediction_status{"Prediction off"};
    std::array<Hitbox,512> boxes{};size_t box_count{};
};
inline AdaptiveFocusSolution build_adaptive_solution(const Snapshot& s,const FocusTarget& target,const CameraFocusSettings& settings,bool sniper,double now,double queue_ms,uint64_t previous_hitbox=0) {
        AdaptiveFocusSolution result;result.target=target.handle;result.kind=target.kind;
        result.motion_source=target.motion.source;
        auto base=adaptive_closest(s,target,settings.adaptive,settings.focus_bone,{},settings.visible_only,previous_hitbox,false);
        if(!base.valid)return result;
        result.acquisition_angle=base.distance;
        auto point=base;Vec3 lead{};
        const auto& motion=target.motion;const double age=now-motion.measured_ms;
        const bool valid_motion=motion.velocity_valid&&age>=0&&age<=50;
        result.prediction_status=settings.adaptive.prediction?"Prediction waiting for motion":"Prediction off";
        if(settings.adaptive.prediction&&valid_motion) {
            const bool projectile_valid=sniper||s.weapon.valid;
            const float inheritance=sniper?0:s.weapon.inheritance;
            const bool local_valid=inheritance==0||s.local_motion.velocity_valid;
            if(projectile_valid&&local_valid) {
                result.data_age_ms=float(age+std::clamp(queue_ms,0.,20.));
                const Vec3 age_lead=mul(motion.velocity,result.data_age_ms/1000.f);
                const Vec3 relative=sub(motion.velocity,mul(s.local_motion.velocity,inheritance));
                lead=age_lead;
                CameraBasis basis;camera_basis(s.matrix,basis);
                bool solved=true,blocked=false;
                for(int iteration=0;iteration<3;++iteration) {
                    auto candidate=adaptive_closest(s,target,settings.adaptive,settings.focus_bone,lead,settings.visible_only,previous_hitbox);
                    if(!candidate.valid){solved=false;blocked=true;break;}point=candidate;
                    float flight{};
                    if(!sniper&&!intercept(basis.origin,add(sub(point.world,lead),age_lead),relative,s.weapon.speed,flight)){solved=false;break;}
                    result.flight_time=flight;Vec3 next=add(age_lead,mul(relative,flight));
                    if(dot(sub(next,lead),sub(next,lead))<.001f){lead=next;break;}lead=next;
                }
                if(solved) {
                    point=adaptive_closest(s,target,settings.adaptive,settings.focus_bone,lead,settings.visible_only,previous_hitbox);
                    result.prediction_active=point.valid;
                    result.prediction_status=sniper?"Hitscan / data-age compensation":"Automatic prediction active";
                }else if(blocked){point={};result.prediction_status="Predicted point blocked";}
                else {point=base;result.prediction_status="No valid intercept / current geometry";}
            }else result.prediction_status=projectile_valid?"Local motion unavailable / current geometry":"Projectile data unavailable / current geometry";
        }
        if(!result.prediction_active) {
            if(point.valid){lead={};result.flight_time=0;result.data_age_ms=0;point=adaptive_closest(s,target,settings.adaptive,settings.focus_bone,{},settings.visible_only,previous_hitbox);}
        }
        result.valid=point.valid;result.inside=point.inside;result.aim_point=point.world;result.current_point=sub(point.world,lead);
        result.lead=lead;result.source_id=point.source_id;result.region=point.region;result.error=point.error;
        if(result.valid)for(const auto& box:target.hitboxes)if(adaptive_permits(target,box,settings.adaptive,settings.focus_bone)&&result.box_count<result.boxes.size())result.boxes[result.box_count++]=box;
        return result;
    }
class AdaptiveFocus {
    uint32_t target_{};uint64_t hitbox_{},signature_{};
    double last_sample_{},last_motion_{},occluded_since_{},acquire_after_{};
    AdaptiveMouseResponse response_;bool input_blocked_{};
    FocusHoldButton response_button_{FocusHoldButton::SideButtons};float response_speed_{};
    AdaptiveTargetZones zones_;
    struct ZoneCandidate {uint32_t target{};AdaptivePoint point;};
    std::vector<ZoneCandidate> zone_candidates_;
    std::vector<AdaptiveZoneIssue> zone_issues_;
    double prepared_ms_{-1};uint64_t prepared_signature_{};uint32_t preview_target_{};
    bool zone_active_{};const char* zone_status_{"Target Zone inactive"};
    static bool candidate_allowed(const FocusTarget& p,const Snapshot& s,const CameraFocusSettings& settings) {
        return focus_candidate(p,s,{settings.focus_players,settings.focus_minions,settings.focus_orbs,settings.strict_enemy_players},settings.exclude_teammates)
            &&(p.kind==TargetKind::SoulOrb||p.maximum<=0||p.health>0);
    }
    void clear_motion(){response_.clear();last_motion_=0;}
    void lose(double now){target_=0;hitbox_=0;occluded_since_=0;clear_motion();solution={};acquire_after_=now+100;}
    static uint64_t signature(const CameraFocusSettings& s,int hero,bool sniper) {
        uint64_t hash=1469598103934665603ULL;
        for(uint32_t v:{uint32_t(hero),uint32_t(sniper),uint32_t(s.focus_bone),uint32_t(s.adaptive.region),
            std::bit_cast<uint32_t>(s.adaptive.radius_degrees),std::bit_cast<uint32_t>(s.adaptive.speed),
            uint32_t(s.adaptive.area),std::bit_cast<uint32_t>(s.adaptive.padding_percent),
            std::bit_cast<uint32_t>(s.adaptive.max_rate),std::bit_cast<uint32_t>(s.adaptive.max_acceleration),
            uint32_t(s.adaptive.prediction),uint32_t(s.visible_only),uint32_t(s.exclude_teammates),
            uint32_t(s.focus_players),uint32_t(s.focus_minions),uint32_t(s.focus_orbs),uint32_t(s.focus_hold_button),uint32_t(s.strict_enemy_players),
            uint32_t(s.free_focus),uint32_t(s.free_focus_mode)})hash=(hash^v)*1099511628211ULL;
        return hash;
    }

public:
    AdaptiveFocusSolution solution{};
    std::string status{"Adaptive inactive"};uint64_t moves{},input_failures{};
    uint32_t target()const{return target_;}
    const char* zone_status()const{return zone_status_;}
    std::span<const AdaptiveZoneIssue> zone_issues()const{return zone_issues_;}
    uint32_t preview_target()const{return preview_target_;}
    const AdaptiveTargetZones& zones()const{return zones_;}
    const AdaptiveTargetZone* zone(uint32_t retained_target=0)const {
        if(!zone_active_)return nullptr;
        const auto selected_id=retained_target?retained_target:target_;
        const auto* selected=selected_id?zones_.find(selected_id):nullptr;
        return selected&&selected->valid?selected:zones_.find(preview_target_);
    }
    // Prepare without requiring a held button, so the opt-in
    // preview can show where acquisition is possible before assistance starts.
    void prepare(const Snapshot& s,const CameraFocusSettings& settings) {
        zone_active_=settings.adaptive.area==AdaptiveAreaMode::TargetZone;
        if(!zone_active_){preview_target_=0;zone_candidates_.clear();zone_issues_.clear();prepared_ms_=-1;zone_status_="Target Zone inactive";return;}
        const auto area=focus_area_settings(settings);
        const bool rebuilt=zones_.prepare(s,area,settings.focus_bone);
        const auto key=signature(settings,s.hero.id,false);
        if(!rebuilt&&prepared_ms_==s.projection_ms&&prepared_signature_==key)return;
        prepared_ms_=s.projection_ms;prepared_signature_=key;preview_target_=0;zone_candidates_.clear();zone_issues_.clear();
        zone_status_="Waiting for eligible target geometry";
        double best=std::numeric_limits<double>::max();int lowest=INT_MAX;unsigned ties{};
        auto consider=[&](const FocusTarget& p) {
            if(!candidate_allowed(p,s,settings))return;
            const auto* zone=zones_.find(p.handle);
            if(!zone||!zone->valid) {
                zone_status_=zone?zone->reason:"Target geometry unavailable";zone_issues_.push_back({p.handle,p.kind,zone_status_});return;
            }
            auto closest=[&](bool visible_only) {
                if(adaptive_mode(settings))return adaptive_closest(s,p,area,settings.focus_bone,{},visible_only,0,false);
                AdaptivePoint result;float line{};bool inside{};ScreenPoint screen;CameraBasis basis;
                if(!focus_point(s.matrix,p,settings.focus_bone,settings.free_focus,2,2,{1,1},{},result.world,line,settings.free_focus_mode,nullptr,&inside)
                    ||!focus_project(s.matrix,result.world,2,2,screen)||!camera_basis(s.matrix,basis))return result;
                result.error=point_error(basis,result.world);result.distance=inside?0.f:result.error.distance;
                result.inside=inside;result.geometry=true;
                result.valid=std::isfinite(result.distance)&&result.distance<90&&visibility_allowed(visible_only,s.visibility,s.camera_position,s.camera_valid,result.world);
                return result;
            };
            auto point=closest(false);
            if(!point.valid)return;
            if(settings.visible_only&&!closest(true).valid) {
                zone_status_="No permitted visible point";return;
            }
            const int health=p.maximum>0&&p.health>=0?p.health:INT_MAX;
            zone_candidates_.push_back({p.handle,point});
            const double distance=std::round(double(point.distance)*1000.);
            if(distance<best||(distance==best&&health<lowest)){best=distance;lowest=health;preview_target_=p.handle;ties=1;}
            else if(distance==best&&health==lowest&&std::uniform_int_distribution<unsigned>(1,++ties)(focus_random())==1)preview_target_=p.handle;
        };
        for(const auto& p:s.players)consider(p);for(const auto& p:s.focus_targets)consider(p);
        if(preview_target_)zone_status_="Target Zone ready";
    }
    void release(const char* reason="Adaptive inactive") {
        target_=0;hitbox_=signature_=0;last_sample_=occluded_since_=acquire_after_=0;
        input_blocked_=false;clear_motion();solution={};status=reason;
    }
    const FocusTarget* choose(const Snapshot& s,const CameraFocusSettings& settings,double now,AdaptivePoint& base) {
        prepare(s,settings);
        const FocusTarget* selected=nullptr;
        if(target_) {
            for(const auto& p:s.players)if(p.handle==target_){selected=&p;break;}
            if(!selected)for(const auto& p:s.focus_targets)if(p.handle==target_){selected=&p;break;}
            if(!selected||!candidate_allowed(*selected,s,settings)) {lose(now);status="Target lost / selecting next target";return nullptr;}
        }
        if(selected) {
            if(zone_active_) {
                const auto* zone=zones_.find(selected->handle);
                if(!zone||!zone->valid){lose(now);status=zone?zone->reason:"Target geometry unavailable";return nullptr;}
                if(!zone->retention_hit){lose(now);status="Outside Target Zone retention / selecting next target";return nullptr;}
            }
            base=adaptive_closest(s,*selected,settings.adaptive,settings.focus_bone,{},false,hitbox_,false);
            if(!base.geometry){clear_motion();solution={};status="Permitted hitbox region unavailable";return nullptr;}
            if(!base.valid||(!zone_active_&&base.distance>settings.adaptive.radius_degrees*1.3f)){lose(now);status="Outside retention area";return nullptr;}
        }else {
            if(now<acquire_after_){status="Waiting to select next target";return nullptr;}
            double best=std::numeric_limits<double>::max();int lowest=INT_MAX;unsigned ties{};bool geometry_seen=false;
            auto consider=[&](const FocusTarget& candidate) {
                if(!candidate_allowed(candidate,s,settings))return;
                AdaptivePoint point;
                if(zone_active_) {
                    const auto* zone=zones_.find(candidate.handle);
                    geometry_seen|=zone&&zone->valid;if(!zone||!zone->valid||!zone->activation_hit)return;
                    auto cached=std::find_if(zone_candidates_.begin(),zone_candidates_.end(),[&](const auto& item){return item.target==candidate.handle;});
                    if(cached==zone_candidates_.end())return;point=cached->point;
                }else {
                    point=adaptive_closest(s,candidate,settings.adaptive,settings.focus_bone,{},false,0,false);
                    geometry_seen|=point.geometry;if(!point.valid||point.distance>settings.adaptive.radius_degrees)return;
                }
                const double distance=std::round(double(point.distance)*1000.);const int health=candidate.maximum>0&&candidate.health>=0?candidate.health:INT_MAX;
                if(settings.visible_only&&!zone_active_) {
                    auto visible=adaptive_closest(s,candidate,settings.adaptive,settings.focus_bone,{},true,0,false);
                    if(!visible.valid)return;
                    visible.distance=point.distance;point=visible;
                }
                if(distance<best||(distance==best&&health<lowest)){best=distance;lowest=health;selected=&candidate;base=point;ties=1;}
                else if(distance==best&&health==lowest&&std::uniform_int_distribution<unsigned>(1,++ties)(focus_random())==1){selected=&candidate;base=point;}
            };
            for(const auto& p:s.players)consider(p);for(const auto& p:s.focus_targets)consider(p);
            if(!selected){solution={};status=zone_active_?(geometry_seen?"Hold and enter a Target Zone":zone_status_)
                :geometry_seen?"Hold and enter the angular acquisition area":"Waiting for permitted hitboxes";return nullptr;}
            target_=selected->handle;hitbox_=base.source_id;clear_motion();
        }
        return selected;
    }
    void update(MakcuInput& input,const Snapshot& s,HWND game,const CameraFocusSettings& settings,
        bool menu,bool sniper,bool enabled,double now,const FocusActivation& activation) {
        prepare(s,settings);
        if(response_button_!=activation.button||response_speed_!=activation.adaptive_speed) {
            input.cancel_motion();clear_motion();response_button_=activation.button;response_speed_=activation.adaptive_speed;
        }
        const char* gate=!enabled?"Focus disabled":menu?"Paused / menu open":!input.ready()?"Waiting for mouse input"
            :!game||IsIconic(game)||GetForegroundWindow()!=game?"Game is not focused":!focus_button_held(activation.button)?"Hold the focus button"
            :!s.time||GetTickCount64()<s.time||GetTickCount64()-s.time>50||!s.camera_valid||!s.projection.valid||now-s.projection_ms>50?"Waiting for fresh camera data":nullptr;
        if(gate){input.cancel_motion();release(gate);return;}
        const uint64_t current_signature=signature(settings,s.hero.id,sniper);
        if(signature_!=current_signature){input.cancel_motion();release();signature_=current_signature;}
        RECT viewport{};
        if(!GetClientRect(game,&viewport)||viewport.right<=0||viewport.bottom<=0){input.cancel_motion();release("Waiting for game viewport");return;}
        if(input_blocked_){status="Input failed / release the focus button";return;}
        if(last_sample_==s.projection_ms)return;
        last_sample_=s.projection_ms;input.cancel_motion();
        AdaptivePoint base;const auto* selected=choose(s,settings,now,base);if(!selected)return;
        auto next=build_adaptive_solution(s,*selected,settings,sniper,now,input.queue_delay_ms(),hitbox_);
        next.acquisition_angle=base.distance;
        if(!base.valid||!next.valid) {
            clear_motion();solution={};solution.target=target_;solution.occluded=true;
            if(!occluded_since_)occluded_since_=now;
            status=next.prediction_status==std::string_view("Predicted point blocked")?"Predicted point blocked / target reserved":"Point blocked / target reserved";
            if(now-occluded_since_>=100){lose(now);status="Target unavailable / selecting next target";}return;
        }
        occluded_since_=0;next.retained=true;solution=next;hitbox_=next.source_id;
        if(next.inside){clear_motion();status="Free movement / inside permitted region";return;}
        if(!last_motion_){last_motion_=now;status="Adaptive focus acquired";return;}
        const float dt=float(std::clamp((now-last_motion_)/1000.,0.,.025));last_motion_=now;if(dt<=0)return;
        const auto correction=response_.step(s.matrix,next.aim_point,float(viewport.right),float(viewport.bottom),dt,settings.adaptive,activation.adaptive_speed);
        if(!correction.valid){clear_motion();status="Waiting for a valid projected focus point";return;}
        solution.error=correction.error;solution.pixel_error=correction.remaining;solution.requested_pixels=correction.requested;
        solution.requested_x=correction.x;solution.requested_y=correction.y;
        const long x=correction.x,y=correction.y;
        status=next.prediction_status;
        if(!x&&!y)return;
        if(input.move(game,x,y,activation.button))++moves;
        else {++input_failures;input_blocked_=true;clear_motion();status="Input failed / release the focus button";}
    }
};
}
