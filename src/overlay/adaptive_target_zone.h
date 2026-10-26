#pragma once
#include "game_reader.h"
#include "focus_hitboxes.h"
#include "adaptive_settings.h"
#include <bit>
#include <span>

namespace overlay {
inline bool adaptive_area_accepts(const Matrix& matrix,Vec3 point,const AdaptiveSettings& settings,float degrees,bool retained=false) {
    float scale=retained?1.3f:1.f;
    if(settings.area==AdaptiveAreaMode::PixelCircle||settings.area==AdaptiveAreaMode::PixelSquare) {
        ScreenPoint p;if(focus_pixel_width<=0||focus_pixel_height<=0||!focus_project(matrix,point,focus_pixel_width,focus_pixel_height,p))return false;
        auto c=focus_reference(focus_pixel_width,focus_pixel_height);float dx=std::abs(p.x-c.x),dy=std::abs(p.y-c.y),radius=settings.pixel_radius*scale;
        return settings.area==AdaptiveAreaMode::PixelSquare?dx<=radius&&dy<=radius:std::hypot(dx,dy)<=radius;
    }
    return degrees<=settings.radius_degrees*scale;
}
inline AnatomicalRegion preferred_region(int bone){if(bone>=3){int joint=bone-3;return joint<2?AnatomicalRegion::Head:joint==3?AnatomicalRegion::Pelvis:joint>=4?AnatomicalRegion::Limb:AnatomicalRegion::Torso;}return bone==0?AnatomicalRegion::Head:bone==2?AnatomicalRegion::Pelvis:AnatomicalRegion::Torso;}
inline bool adaptive_permits(const FocusTarget& target,const Hitbox& box,const AdaptiveSettings& settings,int bone) {
    if(target.kind!=TargetKind::Player||settings.region==AdaptiveRegion::WholeBody)return true;
    if(settings.region==AdaptiveRegion::SelectedBone) {
        if(target.preferred_joint>=0&&target.preferred_joint<16&&(target.joints_valid&(1u<<target.preferred_joint))) {
            const auto point=target.joints[target.preferred_joint];uint64_t source{};float best=FLT_MAX;
            for(const auto& candidate:target.hitboxes){auto delta=sub(hitbox_center(candidate),point);float distance=dot(delta,delta);if(distance<best){best=distance;source=candidate.source_id;}}
            return source&&box.source_id==source;
        }
        return box.region==preferred_region(bone);
    }
    return box.region==AnatomicalRegion::Head||box.region==AnatomicalRegion::Torso||box.region==AnatomicalRegion::Pelvis;
}
namespace target_zone {
inline bool bounds(const Hitbox& box,float& low,float& high) {
    if(!finite(box.a)||!finite(box.b))return false;
    if(box.shape==HitboxShape::Box) {
        low=std::numeric_limits<float>::max();high=-low;
        for(auto p:box.corners){if(!finite(p))return false;low=std::min(low,p.z);high=std::max(high,p.z);}
        std::array<Vec3,3> axes{};int i=0;
        for(int bit:{1,2,4}) {
            auto edge=sub(box.corners[bit],box.corners[0]);float length=std::sqrt(dot(edge,edge));
            if(!std::isfinite(length)||length<.001f)return false;
            axes[i++]=mul(edge,1/length);
        }
        // The slab intersection and local-axis expansion require an OBB.
        return std::abs(dot(axes[0],axes[1]))<.002f&&std::abs(dot(axes[0],axes[2]))<.002f&&std::abs(dot(axes[1],axes[2]))<.002f;
    }
    if((box.shape!=HitboxShape::Sphere&&box.shape!=HitboxShape::Capsule)||!std::isfinite(box.radius)||box.radius<=0)return false;
    low=std::min(box.a.z,box.shape==HitboxShape::Sphere?box.a.z:box.b.z)-box.radius;
    high=std::max(box.a.z,box.shape==HitboxShape::Sphere?box.a.z:box.b.z)+box.radius;
    return std::isfinite(low)&&std::isfinite(high);
}
inline bool expand(const Hitbox& source,float padding,Hitbox& expanded) {
    float low{},high{};
    if(!std::isfinite(padding)||padding<0||!bounds(source,low,high))return false;
    expanded=source;
    if(source.shape!=HitboxShape::Box){expanded.radius+=padding;return std::isfinite(expanded.radius);}
    std::array<Vec3,3> axes{};int j=0;
    for(int bit:{1,2,4}) {
        auto edge=sub(source.corners[bit],source.corners[0]);axes[j++]=mul(edge,padding/std::sqrt(dot(edge,edge)));
    }
    for(int i=0;i<8;++i) {
        auto p=source.corners[i];
        for(int axis=0;axis<3;++axis)p=add(p,mul(axes[axis],(i&(1<<axis))?1.f:-1.f));
        expanded.corners[i]=p;
    }
    expanded.a=expanded.corners[0];expanded.b=expanded.corners[7];return true;
}
inline bool intersects(std::span<const Hitbox> boxes,Vec3 origin,Vec3 direction) {
    for(const auto& box:boxes){double t{};if(hitbox_focus::intersect(box,{},origin,direction,t))return true;}
    return false;
}
inline uint64_t source_key(std::span<const Hitbox> boxes) {
    uint64_t hash=1469598103934665603ULL;
    for(const auto& box:boxes)for(uint64_t v:{box.source_id,uint64_t(box.shape),uint64_t(box.group)})hash=(hash^v)*1099511628211ULL;
    return (hash^uint64_t(boxes.size()))*1099511628211ULL;
}
}

// Current-space activation geometry. These copies are never passed to focus
// prediction, free movement or auto click. The renderer consumes these same
// primitives, including the smoothed body scale used by the controller.
struct AdaptiveTargetZone {
    uint32_t target{};TargetKind kind{};
    uint64_t source_identity{},revision{};
    double measured_ms{};
    float body_height{},padding{},retention_padding{};
    bool valid{},activation_hit{},retention_hit{};
    const char* reason{"Target geometry unavailable"};
    std::vector<Hitbox> activation,retention;
};
struct AdaptiveZoneIssue {uint32_t target{};TargetKind kind{};const char* reason{};};
class AdaptiveTargetZones {
    struct Entry {AdaptiveTargetZone zone;uint64_t seen{};double scale_ms{};};
    std::unordered_map<uint32_t,Entry> entries_;
    double sample_ms_{-1};uint64_t generation_{},revision_{};DWORD pid_{};
    AdaptiveRegion region_{};int bone_{-1};float padding_{-1};
    void build(const FocusTarget& target,const AdaptiveSettings& settings,int bone,bool fresh) {
        auto& entry=entries_[target.handle];auto& zone=entry.zone;entry.seen=generation_;
        zone.target=target.handle;zone.kind=target.kind;zone.measured_ms=sample_ms_;zone.revision=++revision_;
        zone.valid=zone.activation_hit=zone.retention_hit=false;zone.activation.clear();zone.retention.clear();
        std::span<const Hitbox> body=target.hitboxes;
        if(target.kind==TargetKind::SoulOrb) {
            if(!target.activation_bounds){zone.reason="Soul orb collision bounds unavailable";return;}
            body={&*target.activation_bounds,1};
        }
        if(body.empty()||body.size()>512){zone.reason="Active body hitboxes unavailable";return;}
        float low=std::numeric_limits<float>::max(),high=-low;
        for(const auto& box:body) {
            float a{},b{};if(!target_zone::bounds(box,a,b)){zone.reason="Invalid body geometry / scale";return;}
            low=std::min(low,a);high=std::max(high,b);
        }
        float height=high-low;
        if(!std::isfinite(height)||height<.001f){zone.reason="Body height unavailable";return;}
        const auto identity=target_zone::source_key(body);
        if(zone.source_identity!=identity||zone.body_height<=0||entry.scale_ms<=0)zone.body_height=height;
        else if(fresh) {
            const double dt=std::max(0.,sample_ms_-entry.scale_ms);
            zone.body_height+=(height-zone.body_height)*float(-std::expm1(-dt/150.));
        }
        zone.source_identity=identity;if(fresh||entry.scale_ms<=0)entry.scale_ms=sample_ms_;
        zone.padding=zone.body_height*settings.padding_percent*.01f;zone.retention_padding=zone.padding*1.3f;
        zone.activation.reserve(body.size());zone.retention.reserve(body.size());
        for(const auto& box:body)if(target.kind==TargetKind::SoulOrb||adaptive_permits(target,box,settings,bone)) {
            Hitbox activation,retention;
            if(!target_zone::expand(box,zone.padding,activation)||!target_zone::expand(box,zone.retention_padding,retention)) {
                zone.activation.clear();zone.retention.clear();zone.reason="Invalid permitted geometry";return;
            }
            zone.activation.push_back(activation);zone.retention.push_back(retention);
        }
        if(zone.activation.empty()){zone.reason="Permitted hitbox region unavailable";return;}
        zone.valid=true;zone.reason="Target Zone ready";
    }
public:
    bool prepare(const Snapshot& s,const AdaptiveSettings& settings,int bone) {
        if(pid_!=s.pid){entries_.clear();sample_ms_=-1;pid_=s.pid;}
        const bool fresh=sample_ms_!=s.projection_ms;
        if(!fresh&&region_==settings.region&&bone_==bone&&padding_==settings.padding_percent)return false;
        sample_ms_=s.projection_ms;region_=settings.region;bone_=bone;padding_=settings.padding_percent;++generation_;
        for(const auto& p:s.players)build(p,settings,bone,fresh);
        for(const auto& p:s.focus_targets)build(p,settings,bone,fresh);
        std::erase_if(entries_,[&](const auto& pair){return pair.second.seen!=generation_;});
        Vec3 origin,direction;
        if(hitbox_focus::ray(s.matrix,2,2,focus_reference(2,2),origin,direction))for(auto& [id,entry]:entries_) {
            auto& zone=entry.zone;if(!zone.valid)continue;
            zone.activation_hit=target_zone::intersects(zone.activation,origin,direction);
            zone.retention_hit=target_zone::intersects(zone.retention,origin,direction);
        }
        return true;
    }
    const AdaptiveTargetZone* find(uint32_t target)const {
        auto it=entries_.find(target);return it==entries_.end()?nullptr:&it->second.zone;
    }
};
}
