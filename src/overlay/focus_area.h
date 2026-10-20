#pragma once
#include "focus_math.h"
#include <limits>

namespace overlay {
// Current client.dll multiplies world distance by 0.0254 to obtain meters
// (e.g. Lash down-strike height at RVA 0xF2B890, conversion at 0xF2B8BD).
inline constexpr float meters_per_world_unit=.0254f;
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
