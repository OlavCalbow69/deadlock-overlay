#pragma once
#include "focus_math.h"

namespace overlay {
enum class HitboxShape : uint8_t { Box, Sphere, Capsule };
struct ModelHitbox {
    Vec3 low{},high{};
    float radius{};
    int bone{-1},group{};
    HitboxShape shape{};
    bool translation_only{};
};
struct Hitbox {
    HitboxShape shape{};
    Vec3 a{},b{};
    float radius{};
    int group{};
    std::array<Vec3,8> corners{};
    bool visible{},visibility_known{};
};
inline bool valid_hitbox(const ModelHitbox& box) {
    if(!finite(box.low)||!finite(box.high)||!std::isfinite(box.radius)||box.group<0||box.group>=32)return false;
    for(auto p:{box.low,box.high})if(std::abs(p.x)>1024||std::abs(p.y)>1024||std::abs(p.z)>1024)return false;
    if(box.shape==HitboxShape::Box)return box.low.x<=box.high.x&&box.low.y<=box.high.y&&box.low.z<=box.high.z
        &&dot(sub(box.high,box.low),sub(box.high,box.low))<=512.f*512.f;
    if(box.shape!=HitboxShape::Sphere&&box.shape!=HitboxShape::Capsule)return false;
    return box.radius>0&&box.radius<=256&&dot(sub(box.high,box.low),sub(box.high,box.low))<=512.f*512.f;
}
inline bool world_hitbox(const ModelHitbox& model,std::array<float,8> transform,Hitbox& out) {
    if(!valid_hitbox(model))return false;
    // Translation-only shapes follow the bone origin without rotation or scale.
    if(model.translation_only) {
        for(float value:transform)if(!std::isfinite(value))return false;
        transform[3]=1;transform[4]=transform[5]=transform[6]=0;transform[7]=1;
    }
    Hitbox result;result.shape=model.shape;result.group=model.group;
    if(!scene_point(transform,model.low,result.a)||!scene_point(transform,model.high,result.b))return false;
    result.radius=model.radius*transform[3];
    if(result.shape==HitboxShape::Sphere)result.b=result.a;
    if(result.shape==HitboxShape::Box)for(int i=0;i<8;++i) {
        Vec3 corner{(i&1)?model.high.x:model.low.x,(i&2)?model.high.y:model.low.y,(i&4)?model.high.z:model.low.z};
        if(!scene_point(transform,corner,result.corners[i]))return false;
    }
    out=result;return true;
}
inline Vec3 hitbox_center(const Hitbox& box){return mul(add(box.a,box.b),.5f);}
// A fixed wire mesh avoids per-frame allocations and adapts capsules to their
// transformed endpoint axis. Segment projection clips at the camera/viewport.
template<class Line> void hitbox_segments(const Hitbox& box,Line&& line) {
    if(box.shape==HitboxShape::Box) {
        for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))line(box.corners[i],box.corners[i|bit]);
        return;
    }
    constexpr int slices=16;
    static const auto circle=[] {
        std::array<std::array<float,2>,slices> points{};
        for(int i=0;i<slices;++i){float angle=float(i)*6.28318530718f/slices;points[i]={std::cos(angle),std::sin(angle)};}
        return points;
    }();
    auto ring=[&](Vec3 center,Vec3 u,Vec3 v) {
        auto point=[&](int i){return add(center,mul(add(mul(u,circle[i][0]),mul(v,circle[i][1])),box.radius));};
        Vec3 previous=point(slices-1);for(int i=0;i<slices;++i){auto next=point(i);line(previous,next);previous=next;}
    };
    Vec3 axis=sub(box.b,box.a);float length=std::sqrt(dot(axis,axis));
    if(box.shape==HitboxShape::Sphere||length<.001f) {
        ring(box.a,{1,0,0},{0,1,0});ring(box.a,{1,0,0},{0,0,1});ring(box.a,{0,1,0},{0,0,1});return;
    }
    axis=mul(axis,1/length);
    Vec3 u=cross(axis,std::abs(axis.z)<.9f?Vec3{0,0,1}:Vec3{0,1,0});u=mul(u,1/std::sqrt(dot(u,u)));
    Vec3 v=cross(axis,u);
    ring(box.a,u,v);ring(box.b,u,v);
    for(auto side:{u,mul(u,-1),v,mul(v,-1)})line(add(box.a,mul(side,box.radius)),add(box.b,mul(side,box.radius)));
    auto cap=[&](Vec3 center,Vec3 direction,Vec3 side) {
        Vec3 previous=add(center,mul(side,box.radius));
        for(int i=1;i<=slices/2;++i) {
            auto next=add(center,mul(add(mul(side,circle[i][0]),mul(direction,circle[i][1])),box.radius));
            line(previous,next);previous=next;
        }
    };
    cap(box.a,mul(axis,-1),u);cap(box.a,mul(axis,-1),v);cap(box.b,axis,u);cap(box.b,axis,v);
}
}
