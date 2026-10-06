#pragma once
#include "focus_math.h"
#include <limits>

namespace overlay {
enum class FocusAreaMode { CenterFov, TargetBox3D, TargetCircle2D };
struct FocusArea {
    FocusAreaMode mode{FocusAreaMode::CenterFov};
    float center_percent{40.f},radius_meters{10.f};
    bool operator==(const FocusArea&)const=default;
};
inline FocusArea valid_focus_area(FocusArea area) {
    if(int(area.mode)<0||int(area.mode)>2)area.mode=FocusAreaMode::CenterFov;
    area.center_percent=std::isfinite(area.center_percent)?std::clamp(area.center_percent,1.f,80.f):40.f;
    area.radius_meters=std::isfinite(area.radius_meters)?std::clamp(area.radius_meters,.1f,50.f):10.f;
    return area;
}
// Current client.dll multiplies world distance by 0.0254 to obtain meters
// (e.g. Lash down-strike height at RVA 0xF2B890, conversion at 0xF2B8BD).
inline constexpr float meters_per_world_unit=.0254f;
inline float focus_radius_units(FocusArea area){return valid_focus_area(area).radius_meters/meters_per_world_unit;}
inline float center_fov_radius(FocusArea area,float width,float height) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return 0;
    return std::min(width,height)*valid_focus_area(area).center_percent*.01f;
}
struct FocusClip {float x{},y{},w{};};
inline bool focus_clip(const Matrix& m,Vec3 p,FocusClip& out) {
    if(!finite(p))return false;
    out={m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15]};
    return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.w);
}
// Positive-depth projection also permits an off-screen bone whose area overlaps the crosshair.
inline bool focus_project(const Matrix& matrix,Vec3 p,float width,float height,ScreenPoint& out) {
    FocusClip clip;
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!focus_clip(matrix,p,clip)||clip.w<=.001f)return false;
    out={(1+clip.x/clip.w)*width*.5f,(1-clip.y/clip.w)*height*.5f};
    return std::isfinite(out.x)&&std::isfinite(out.y);
}
inline bool focus_camera_ray(const Matrix& matrix,Vec3& origin,Vec3& direction) {
    if(!camera_origin(matrix,origin))return false;
    Vec3 x{matrix[0],matrix[1],matrix[2]},y{matrix[4],matrix[5],matrix[6]},w{matrix[12],matrix[13],matrix[14]};
    direction=cross(x,y);float length=std::sqrt(dot(direction,direction));
    if(!std::isfinite(length)||length<1e-8f)return false;
    direction=mul(direction,1/length);
    float forward=dot(direction,w);if(!std::isfinite(forward)||std::abs(forward)<1e-8f)return false;
    if(forward<0)direction=mul(direction,-1);
    return finite(direction);
}
inline bool focus_ray_box(Vec3 origin,Vec3 direction,Vec3 bone,float radius) {
    if(!finite(origin)||!finite(direction)||!finite(bone)||!std::isfinite(radius)||radius<=0||dot(direction,direction)<1e-12f)return false;
    float enter=0,exit=std::numeric_limits<float>::max();
    const float o[]={origin.x,origin.y,origin.z},d[]={direction.x,direction.y,direction.z},b[]={bone.x,bone.y,bone.z};
    for(int i=0;i<3;++i) {
        if(std::abs(d[i])<1e-8f){if(o[i]<b[i]-radius||o[i]>b[i]+radius)return false;continue;}
        float a=(b[i]-radius-o[i])/d[i],z=(b[i]+radius-o[i])/d[i];
        if(a>z)std::swap(a,z);enter=std::max(enter,a);exit=std::min(exit,z);if(enter>exit)return false;
    }
    return exit>=enter;
}
inline bool target_circle_radius(const Matrix& matrix,Vec3 bone,float radius_units,float width,float height,float& pixels) {
    FocusClip clip;
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!std::isfinite(radius_units)||radius_units<=0||!focus_clip(matrix,bone,clip)||clip.w<=.001f)return false;
    Vec3 x{matrix[0],matrix[1],matrix[2]},y{matrix[4],matrix[5],matrix[6]};
    // Camera-facing circle: the radius is measured in the plane through the bone.
    pixels=radius_units*(width*.5f*std::sqrt(dot(x,x))+height*.5f*std::sqrt(dot(y,y)))*.5f/clip.w;
    return std::isfinite(pixels)&&pixels>0;
}
inline bool focus_area_contains(FocusArea area,const Matrix& matrix,Vec3 bone,ScreenPoint aim,float width,float height,ScreenPoint crosshair) {
    area=valid_focus_area(area);
    if(!std::isfinite(crosshair.x)||!std::isfinite(crosshair.y)||!std::isfinite(aim.x)||!std::isfinite(aim.y)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
    if(area.mode==FocusAreaMode::CenterFov)
        return std::hypot(aim.x-crosshair.x,aim.y-crosshair.y)<=center_fov_radius(area,width,height);
    ScreenPoint anchor;
    if(!focus_project(matrix,bone,width,height,anchor))return false; // Behind-camera targets never qualify.
    if(area.mode==FocusAreaMode::TargetCircle2D) {
        float radius{};
        return target_circle_radius(matrix,bone,focus_radius_units(area),width,height,radius)&&std::hypot(anchor.x-crosshair.x,anchor.y-crosshair.y)<=radius;
    }
    Vec3 origin,direction;
    return focus_camera_ray(matrix,origin,direction)&&focus_ray_box(origin,direction,bone,focus_radius_units(area));
}
inline std::array<Vec3,8> focus_box_corners(Vec3 center,float radius) {
    std::array<Vec3,8> result;
    for(int i=0;i<8;++i)result[i]=add(center,{i&1?radius:-radius,i&2?radius:-radius,i&4?radius:-radius});
    return result;
}
// Clip in homogeneous space before dividing, including edges that cross the camera plane.
inline bool focus_project_segment(const Matrix& matrix,Vec3 a,Vec3 b,float width,float height,ScreenPoint& sa,ScreenPoint& sb) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
    FocusClip p,q;if(!focus_clip(matrix,a,p)||!focus_clip(matrix,b,q))return false;
    const float pa[]={p.w-.0011f,p.w+p.x,p.w-p.x,p.w+p.y,p.w-p.y};
    const float pb[]={q.w-.0011f,q.w+q.x,q.w-q.x,q.w+q.y,q.w-q.y};
    float start=0,end=1;
    for(int i=0;i<5;++i) {
        if(pa[i]<0&&pb[i]<0)return false;
        if((pa[i]<0)!=(pb[i]<0)) {
            float t=pa[i]/(pa[i]-pb[i]);
            if(pa[i]<0)start=std::max(start,t);else end=std::min(end,t);
        }
        if(start>end)return false;
    }
    auto screen=[&](float t) {
        float w=p.w+(q.w-p.w)*t,x=p.x+(q.x-p.x)*t,y=p.y+(q.y-p.y)*t;
        return ScreenPoint{std::clamp((1+x/w)*width*.5f,0.f,width),std::clamp((1-y/w)*height*.5f,0.f,height)};
    };
    sa=screen(start);sb=screen(end);
    return std::isfinite(sa.x)&&std::isfinite(sa.y)&&std::isfinite(sb.x)&&std::isfinite(sb.y);
}
}
