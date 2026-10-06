#pragma once
#include "logic.h"
#include <cfloat>
#include <string_view>
#include <string>
#include <vector>
namespace overlay {
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 mul(Vec3 a,float k){return {a.x*k,a.y*k,a.z*k};}
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool scene_point(const std::array<float,8>& transform,Vec3 local,Vec3& world) {
    for(auto value:transform)if(!std::isfinite(value))return false;
    if(!finite(local)||transform[3]<=0||transform[3]>16)return false;
    Vec3 rotation{transform[4],transform[5],transform[6]};float w=transform[7];
    float norm=dot(rotation,rotation)+w*w;if(norm<.9f||norm>1.1f)return false;
    float normalize=1/std::sqrt(norm);rotation=mul(rotation,normalize);w*=normalize;
    Vec3 scaled=mul(local,transform[3]),twice=mul(cross(rotation,scaled),2);
    world=add({transform[0],transform[1],transform[2]},add(add(scaled,mul(twice,w)),cross(rotation,twice)));
    return finite(world);
}
inline Vec3 focus_line(const std::array<Vec3,3>& bones,float position){
    position=std::isfinite(position)?std::clamp(position,0.f,2.f):1.f;
    int segment=position<1?0:1;return add(bones[segment],mul(sub(bones[segment+1],bones[segment]),position-float(segment)));
}
inline float slide_focus(float position,float mouse_y,float segment_pixels){
    if(!std::isfinite(mouse_y)||!std::isfinite(segment_pixels))return position;
    return std::clamp(position+mouse_y/std::max(segment_pixels,20.f),0.f,2.f);
}
struct ClosestLinePoint { Vec3 world{};ScreenPoint screen{};float position{},distance{}; };
// Find the nearest point in screen space, then undo perspective interpolation
// to recover its world-space position. No physical-mouse telemetry is needed.
inline bool closest_focus_line(const Matrix& matrix,const std::array<Vec3,3>& bones,float width,float height,ScreenPoint center,ClosestLinePoint& out) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!std::isfinite(center.x)||!std::isfinite(center.y))return false;
    std::array<ScreenPoint,3> screen{};std::array<float,3> depth{};
    for(int i=0;i<3;++i) {
        if(!finite(bones[i]))return false;
        const auto p=bones[i];float w=matrix[12]*p.x+matrix[13]*p.y+matrix[14]*p.z+matrix[15];
        float x=matrix[0]*p.x+matrix[1]*p.y+matrix[2]*p.z+matrix[3];
        float y=matrix[4]*p.x+matrix[5]*p.y+matrix[6]*p.z+matrix[7];
        if(!std::isfinite(w)||!std::isfinite(x)||!std::isfinite(y)||w<=.001f)return false;
        depth[i]=w;screen[i]={width*.5f*(1+x/w),height*.5f*(1-y/w)};
        if(!std::isfinite(screen[i].x)||!std::isfinite(screen[i].y))return false;
    }
    float best=FLT_MAX;bool found=false;
    for(int i=0;i<2;++i) {
        float dx=screen[i+1].x-screen[i].x,dy=screen[i+1].y-screen[i].y,length=dx*dx+dy*dy;
        if(!std::isfinite(length))return false;
        float t=length>1e-6f?std::clamp(((center.x-screen[i].x)*dx+(center.y-screen[i].y)*dy)/length,0.f,1.f):0.f;
        ScreenPoint point{screen[i].x+dx*t,screen[i].y+dy*t};float distance=std::hypot(point.x-center.x,point.y-center.y);
        if(distance>=best)continue;
        float world_t=t*depth[i]/((1-t)*depth[i+1]+t*depth[i]);
        out={add(bones[i],mul(sub(bones[i+1],bones[i]),world_t)),point,float(i)+world_t,distance};
        best=distance;found=true;
    }
    return found;
}
inline bool skeleton_bone(std::string_view name) {
    // Anatomical joints only: ignore fingers, cloth, weapons, IK and helper bones.
    constexpr std::string_view names[]={"pelvis","spine_0","spine_1","spine_2","spine_3","chest","neck","neck_0","head","head_end",
        "clavicle_L","clavicle_R","arm_upper_L","arm_upper_R","arm_lower_L","arm_lower_R","hand_L","hand_R",
        "leg_upper_L","leg_upper_R","leg_lower_L","leg_lower_R","ankle_L","ankle_R","ball_L","ball_R","foot_L","foot_R"};
    for(auto joint:names)if(name==joint)return true;
    return false;
}
inline std::vector<std::pair<int,int>> skeleton_edges(const std::vector<std::string>& names,const std::vector<int16_t>& parents) {
    std::vector<std::pair<int,int>> result;
    if(names.size()!=parents.size()||names.empty()||names.size()>1024)return result;
    for(int child=0;child<int(names.size())&&result.size()<64;++child) {
        if(!skeleton_bone(names[child]))continue;
        int parent=parents[child];
        for(size_t steps=0;steps<names.size();++steps) {
            if(parent<0||parent>=int(names.size())||parent==child)break;
            if(skeleton_bone(names[parent])){result.emplace_back(parent,child);break;}
            parent=parents[parent];
        }
    }
    return result;
}
inline bool camera_origin(const Matrix& m,Vec3& origin){
    Vec3 a{m[0],m[1],m[2]},b{m[4],m[5],m[6]},c{m[12],m[13],m[14]};
    float det=dot(a,cross(b,c));if(!std::isfinite(det)||std::abs(det)<1e-8f)return false;
    origin=mul(add(add(mul(cross(b,c),-m[3]),mul(cross(c,a),-m[7])),mul(cross(a,b),-m[15])),1.f/det);
    return finite(origin);
}
// Constant-speed, straight-line projectile interception. No assumed gravity or latency.
inline bool intercept(Vec3 source,Vec3 target,Vec3 relative_velocity,float speed,float& time){
    time=0;if(!finite(source)||!finite(target)||!finite(relative_velocity)||!std::isfinite(speed)||speed<=0)return false;
    Vec3 r=sub(target,source);double a=dot(relative_velocity,relative_velocity)-double(speed)*speed;
    double b=2.*dot(r,relative_velocity),c=dot(r,r),t=-1;
    if(c<1e-8){return true;}
    if(std::abs(a)<1e-6){if(std::abs(b)>1e-6)t=-c/b;}
    else {double d=b*b-4*a*c;if(d<0)return false;double root=std::sqrt(d);double t1=(-b-root)/(2*a),t2=(-b+root)/(2*a);
        if(t1>0)t=t1;if(t2>0&&(t<0||t2<t))t=t2;}
    if(!std::isfinite(t)||t<0||t>1)return false;time=float(t);return true;
}
struct VelocityEstimate {
    Vec3 baseline{},velocity{};uint64_t time{};bool valid{};
    void update(Vec3 position,uint64_t now){
        if(!finite(position)||!now){*this={};return;}
        if(!time||now<time||now-time>150||dot(sub(position,baseline),sub(position,baseline))>256.f*256.f){baseline=position;time=now;velocity={};valid=false;return;}
        if(now-time<30)return;
        auto measured=mul(sub(position,baseline),1000.f/float(now-time));
        if(dot(measured,measured)>4000.f*4000.f){baseline=position;time=now;velocity={};valid=false;return;}
        float weight=1.f-std::exp(-float(now-time)/60.f);
        velocity=valid?add(velocity,mul(sub(measured,velocity),weight)):measured;
        valid=true;baseline=position;time=now;
    }
};
}
