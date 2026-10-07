#pragma once
#include "focus_area.h"
#include "hitboxes.h"
#include <span>

namespace overlay {
enum class FreeMovementMode { BoneLine, Hitboxes };
inline const char* free_movement_name(bool enabled,FreeMovementMode mode) {
    return !enabled?"off":mode==FreeMovementMode::Hitboxes?"hitboxes_v2":"closest_line";
}
struct ClosestHitboxPoint {
    Vec3 world{};ScreenPoint screen{};float distance{};int index{-1};bool inside{};
};
namespace hitbox_focus {
inline double product(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
inline bool ray(const Matrix& matrix,float width,float height,ScreenPoint pixel,Vec3& origin,Vec3& direction) {
    if(width<=0||height<=0||!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(pixel.x)||!std::isfinite(pixel.y))return false;
    for(float value:matrix)if(!std::isfinite(value))return false;
    if(!camera_origin(matrix,origin))return false;
    const float x=2*pixel.x/width-1,y=1-2*pixel.y/height;
    Vec3 w{matrix[12],matrix[13],matrix[14]};
    direction=cross(sub({matrix[0],matrix[1],matrix[2]},mul(w,x)),sub({matrix[4],matrix[5],matrix[6]},mul(w,y)));
    double length=std::sqrt(product(direction,direction));
    if(!std::isfinite(length)||length<1e-8)return false;
    direction=mul(direction,float(1/length));double forward=product(direction,w);
    if(!std::isfinite(forward)||std::abs(forward)<1e-8)return false;
    if(forward<0)direction=mul(direction,-1);
    return true;
}
inline bool sphere_ray(Vec3 origin,Vec3 direction,Vec3 center,float radius,double& time) {
    Vec3 offset=sub(origin,center);double a=product(direction,direction),b=product(offset,direction),c=product(offset,offset)-double(radius)*radius;
    double discriminant=b*b-a*c;
    if(a<=0||discriminant<0||!std::isfinite(discriminant))return false;
    double root=std::sqrt(discriminant),enter=(-b-root)/a,exit=(-b+root)/a;
    if(exit<=1e-4)return false;
    time=enter>1e-4?enter:exit;return std::isfinite(time);
}
inline bool intersect(const Hitbox& box,Vec3 lead,Vec3 origin,Vec3 direction,double& time) {
    if(!finite(box.a)||!finite(box.b)||!finite(lead))return false;
    if(box.shape==HitboxShape::Box) {
        Vec3 base=add(box.corners[0],lead),offset=sub(origin,base);
        double enter=0,exit=std::numeric_limits<double>::max();
        for(int bit:{1,2,4}) {
            Vec3 edge=sub(box.corners[bit],box.corners[0]);double length=std::sqrt(product(edge,edge));
            if(!finite(edge)||length<1e-6)return false;
            Vec3 axis=mul(edge,float(1/length));double o=product(offset,axis),d=product(direction,axis);
            if(std::abs(d)<1e-10){if(o<0||o>length)return false;continue;}
            double a=-o/d,b=(length-o)/d;if(a>b)std::swap(a,b);
            enter=std::max(enter,a);exit=std::min(exit,b);if(enter>exit)return false;
        }
        if(exit<=1e-4)return false;
        time=enter>1e-4?enter:exit;return std::isfinite(time);
    }
    if((box.shape!=HitboxShape::Sphere&&box.shape!=HitboxShape::Capsule)||!std::isfinite(box.radius)||box.radius<=0)return false;
    Vec3 a=add(box.a,lead),b=add(box.b,lead);
    double best=std::numeric_limits<double>::max(),candidate{};
    if(sphere_ray(origin,direction,a,box.radius,candidate))best=candidate;
    if(box.shape==HitboxShape::Capsule) {
        if(sphere_ray(origin,direction,b,box.radius,candidate))best=std::min(best,candidate);
        Vec3 axis=sub(b,a),offset=sub(origin,a);double length=product(axis,axis),along=product(axis,direction),start=product(axis,offset);
        double qa=length*product(direction,direction)-along*along;
        double qb=length*product(offset,direction)-start*along;
        double qc=length*product(offset,offset)-start*start-double(box.radius)*box.radius*length;
        double discriminant=qb*qb-qa*qc;
        if(length>1e-8&&qa>length*1e-12&&discriminant>=0&&std::isfinite(discriminant)) {
            double root=std::sqrt(discriminant);
            for(double t:{(-qb-root)/qa,(-qb+root)/qa}) {
                double position=start+t*along;
                if(t>1e-4&&position>=0&&position<=length)best=std::min(best,t);
            }
        }
    }
    time=best;return best<std::numeric_limits<double>::max();
}
struct Point {Vec3 world{};double x{},y{},depth{};};
struct Projection {
    std::array<double,4> x{},y{},w{};
    Projection(const Matrix& matrix,float width,float height,ScreenPoint center) {
        for(int i=0;i<4;++i) {
            w[i]=matrix[12+i];
            x[i]=width*.5*matrix[i]+(width*.5-center.x)*w[i];
            y[i]=-height*.5*matrix[4+i]+(height*.5-center.y)*w[i];
        }
    }
    static double apply(const std::array<double,4>& row,Vec3 p){return row[0]*p.x+row[1]*p.y+row[2]*p.z+row[3];}
    bool point(Vec3 world,Point& out)const {
        double depth=apply(w,world);if(!finite(world)||!std::isfinite(depth)||depth<=.0011)return false;
        out={world,apply(x,world)/depth,apply(y,world)/depth,depth};
        return std::isfinite(out.x)&&std::isfinite(out.y);
    }
    // Exact support point of a projected sphere. This accounts for perspective,
    // render aspect ratio and off-center ellipses instead of using a pixel radius.
    bool sphere(Vec3 center,float radius,double dx,double dy,Point& out)const {
        std::array<double,4> d{};for(int i=0;i<4;++i)d[i]=dx*x[i]+dy*y[i];
        double depth=apply(w,center),ww=w[0]*w[0]+w[1]*w[1]+w[2]*w[2],r2=double(radius)*radius;
        if(!std::isfinite(depth)||depth-radius*std::sqrt(ww)<=.0011)return false;
        double baseline=apply(d,center)/depth;
        std::array<double,3> v{};for(int i=0;i<3;++i)v[i]=d[i]-baseline*w[i];
        double vv=v[0]*v[0]+v[1]*v[1]+v[2]*v[2],vw=v[0]*w[0]+v[1]*w[1]+v[2]*w[2];
        double denominator=depth*depth-r2*ww;
        double delta=(-r2*vw+std::sqrt(r2*r2*vw*vw+denominator*r2*vv))/denominator;
        for(int i=0;i<3;++i)v[i]-=delta*w[i];
        double length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!std::isfinite(length)||length<1e-12)return false;
        return point(add(center,{float(radius*v[0]/length),float(radius*v[1]/length),float(radius*v[2]/length)}),out);
    }
    bool support(const Hitbox& box,Vec3 lead,double dx,double dy,Point& out)const {
        if(box.shape==HitboxShape::Box) {
            double best=-std::numeric_limits<double>::max();
            for(auto corner:box.corners) {
                Point candidate;if(!point(add(corner,lead),candidate))return false;
                double value=candidate.x*dx+candidate.y*dy;
                if(value>best){best=value;out=candidate;}
            }
            return true;
        }
        if((box.shape!=HitboxShape::Sphere&&box.shape!=HitboxShape::Capsule)||!std::isfinite(box.radius)||box.radius<=0)return false;
        if(!sphere(add(box.a,lead),box.radius,dx,dy,out))return false;
        if(box.shape==HitboxShape::Capsule) {
            Point other;if(!sphere(add(box.b,lead),box.radius,dx,dy,other))return false;
            // A capsule is the convex hull of its endpoint spheres. With positive
            // depth, its projected support is the farther endpoint's support.
            if(other.x*dx+other.y*dy>out.x*dx+out.y*dy)out=other;
        }
        return true;
    }
};
inline Point blend(const Point& a,const Point& b,double t) {
    double wa=(1-t)/a.depth,wb=t/b.depth,total=wa+wb;
    return {add(mul(a.world,float(wa/total)),mul(b.world,float(wb/total))),a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,1/total};
}
// Two-dimensional GJK distance on the actual projected volume. The simplex has
// at most three points; perspective weights keep the result inside the shape.
inline bool closest(const Projection& projection,const Hitbox& box,Vec3 lead,Point& out) {
    Point center;if(!projection.point(add(hitbox_center(box),lead),center))return false;
    std::array<Point,3> simplex{};int count=1;
    double dx=-center.x,dy=-center.y;if(dx*dx+dy*dy<1e-12)dx=1;
    if(!projection.support(box,lead,dx,dy,simplex[0]))return false;
    out=simplex[0];
    for(int iteration=0;iteration<32;++iteration) {
        double squared=out.x*out.x+out.y*out.y;if(squared<1e-10)break;
        Point next;if(!projection.support(box,lead,-out.x,-out.y,next))return false;
        if(squared-(out.x*next.x+out.y*next.y)<=.02*std::sqrt(squared))break;
        simplex[count++]=next;
        if(count==3) {
            const auto& a=simplex[0];const auto& b=simplex[1];const auto& c=simplex[2];
            double area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            if(std::abs(area)>1e-12) {
                double u=(b.x*c.y-b.y*c.x)/area,v=(c.x*a.y-c.y*a.x)/area,z=1-u-v;
                if(u>=0&&v>=0&&z>=0) {
                    double wa=u/a.depth,wb=v/b.depth,wc=z/c.depth,total=wa+wb+wc;
                    out={add(add(mul(a.world,float(wa/total)),mul(b.world,float(wb/total))),mul(c.world,float(wc/total))),0,0,1/total};break;
                }
            }
        }
        double best=std::numeric_limits<double>::max(),best_t=0;int first=0,second=1;
        for(int i=0;i<count;++i)for(int j=i+1;j<count;++j) {
            double x=simplex[j].x-simplex[i].x,y=simplex[j].y-simplex[i].y,length=x*x+y*y;
            double t=length>1e-16?std::clamp(-(simplex[i].x*x+simplex[i].y*y)/length,0.,1.):0;
            double px=simplex[i].x+x*t,py=simplex[i].y+y*t,distance=px*px+py*py;
            if(distance<best){best=distance;first=i;second=j;best_t=t;}
        }
        Point a=simplex[first],b=simplex[second];out=blend(a,b,best_t);
        if(best_t==0||best_t==1){simplex[0]=best_t==0?a:b;count=1;}
        else {simplex[0]=a;simplex[1]=b;count=2;}
    }
    return finite(out.world)&&std::isfinite(out.x)&&std::isfinite(out.y);
}
}
inline bool closest_focus_hitboxes(const Matrix& matrix,std::span<const Hitbox> boxes,float width,float height,ScreenPoint center,Vec3 lead,ClosestHitboxPoint& out) {
    Vec3 origin{},direction{};
    if(boxes.empty()||!finite(lead)||!hitbox_focus::ray(matrix,width,height,center,origin,direction))return false;
    // Test the union, not a bounding rectangle or convex hull of the entire rig.
    // A crosshair through any full shape is free; gaps between shapes remain gaps.
    double nearest=std::numeric_limits<double>::max();int index=-1;
    for(size_t i=0;i<boxes.size();++i) {
        double time{};
        if(hitbox_focus::intersect(boxes[i],lead,origin,direction,time)&&time<nearest){nearest=time;index=int(i);}
    }
    if(index>=0) {
        Vec3 world=add(origin,mul(direction,float(nearest)));ScreenPoint screen;
        if(!focus_project(matrix,world,width,height,screen))return false;
        out={world,screen,0,index,true};return true;
    }
    hitbox_focus::Projection projection(matrix,width,height,center);bool found=false;double best=std::numeric_limits<double>::max();
    for(size_t i=0;i<boxes.size();++i) {
        hitbox_focus::Point point;
        if(!hitbox_focus::closest(projection,boxes[i],lead,point))continue;
        double squared=point.x*point.x+point.y*point.y;
        if(squared>=best)continue;
        out={point.world,{center.x+float(point.x),center.y+float(point.y)},float(std::sqrt(squared)),int(i),false};best=squared;found=true;
    }
    return found;
}
}
