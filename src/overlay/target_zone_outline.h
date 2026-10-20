#pragma once
#include "adaptive_target_zone.h"

namespace overlay::target_zone {
struct Pixel {double x{},y{};};
struct OutlineSegment {Pixel a,b;};
inline Pixel operator+(Pixel a,Pixel b){return {a.x+b.x,a.y+b.y};}
inline Pixel operator-(Pixel a,Pixel b){return {a.x-b.x,a.y-b.y};}
inline Pixel operator*(Pixel a,double b){return {a.x*b,a.y*b};}
inline double product(Pixel a,Pixel b){return a.x*b.x+a.y*b.y;}
inline double cross2(Pixel a,Pixel b){return a.x*b.y-a.y*b.x;}
namespace outline_detail {
constexpr double near_depth=.0011001,curve_tolerance=.25;
struct Vector {
    double x{},y{},z{};
    Vector()=default;Vector(double x,double y,double z):x(x),y(y),z(z){};Vector(Vec3 p):x(p.x),y(p.y),z(p.z){}
    Vector operator+(Vector p)const{return {x+p.x,y+p.y,z+p.z};}
    Vector operator-(Vector p)const{return {x-p.x,y-p.y,z-p.z};}
    Vector operator*(double f)const{return {x*f,y*f,z*f};}
};
inline double product(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline double length(Vector v){return std::sqrt(product(v,v));}
inline double row_value(const std::array<double,4>& row,Vector p){return row[0]*p.x+row[1]*p.y+row[2]*p.z+row[3];}
inline Vector xyz(const std::array<double,4>& row){return {row[0],row[1],row[2]};}
inline bool finite_pixel(Pixel p){return std::isfinite(p.x)&&std::isfinite(p.y);}

inline void clip(std::vector<Pixel>& points,std::vector<Pixel>& scratch,double width,double height) {
    for(int plane=0;plane<4&&!points.empty();++plane) {
        auto distance=[&](Pixel p){return plane==0?p.x:plane==1?width-p.x:plane==2?p.y:height-p.y;};
        scratch.clear();auto previous=points.back();double old=distance(previous);
        for(auto point:points) {
            double next=distance(point);
            if((next>=0)!=(old>=0))scratch.push_back(previous+(point-previous)*(old/(old-next)));
            if(next>=0)scratch.push_back(point);
            previous=point;old=next;
        }
        points.swap(scratch);
    }
}
inline void hull(std::vector<Pixel>& points,std::vector<Pixel>& scratch) {
    std::sort(points.begin(),points.end(),[](Pixel a,Pixel b){return a.x==b.x?a.y<b.y:a.x<b.x;});
    points.erase(std::unique(points.begin(),points.end(),[](Pixel a,Pixel b){return std::hypot(a.x-b.x,a.y-b.y)<1e-8;}),points.end());
    if(points.size()<3)return;
    scratch.clear();
    auto push=[&](Pixel p,size_t minimum){while(scratch.size()>minimum&&cross2(scratch.back()-scratch[scratch.size()-2],p-scratch.back())<=0)scratch.pop_back();scratch.push_back(p);};
    for(auto p:points)push(p,1);
    size_t lower=scratch.size();
    for(size_t i=points.size()-1;i-->0;)push(points[i],lower);
    scratch.pop_back();points.swap(scratch);
}
struct Projector {
    hitbox_focus::Projection projection;
    double width,height,depth_length;Vector normal;double plane;
    Projector(const Matrix& matrix,float width,float height):projection(matrix,width,height,{0,0}),width(width),height(height) {
        depth_length=length(xyz(projection.w));normal=xyz(projection.w)*(1/depth_length);
        plane=(near_depth-projection.w[3])/depth_length;
    }
    double depth(Vector p)const{return row_value(projection.w,p);}
    bool point(Vector p,Pixel& pixel)const {
        double w=depth(p);if(w<near_depth-1e-8)return false;
        pixel={row_value(projection.x,p)/w,row_value(projection.y,p)/w};return finite_pixel(pixel);
    }
    bool sphere(Vector center,double radius,Pixel direction,Pixel& pixel)const {
        std::array<double,4> d{};for(int i=0;i<4;++i)d[i]=direction.x*projection.x[i]+direction.y*projection.y[i];
        double w=depth(center),r2=radius*radius,ww=depth_length*depth_length;
        if(w-radius*depth_length<=near_depth)return false;
        double baseline=row_value(d,center)/w;Vector v=xyz(d)-xyz(projection.w)*baseline;
        double vv=product(v,v),vw=product(v,xyz(projection.w)),denominator=w*w-r2*ww;
        double delta=(-r2*vw+std::sqrt(std::max(0.,r2*r2*vw*vw+denominator*r2*vv)))/denominator;
        v=v-xyz(projection.w)*delta;double norm=length(v);
        return norm>1e-12&&point(center+v*(radius/norm),pixel);
    }
    // Support of a capsule clipped by the camera plane. At a clipped optimum,
    // maximize over the circular plane sections of its swept endpoint sphere.
    // This also covers cuts through the cylinder when both end spheres miss.
    bool clipped_linear(const Hitbox& box,Vector objective,Vector& out)const {
        Vector a=box.a,b=box.shape==HitboxShape::Sphere?a:Vector(box.b),axis=b-a;
        double norm=length(objective),radius=box.radius;
        if(norm<1e-15)objective=normal,norm=1;
        Vector endpoint=product(objective,axis)>=0?b:a;
        out=endpoint+objective*(radius/norm);
        if(depth(out)>=near_depth)return true;
        double along=product(normal,axis),d0=plane-product(normal,a),lo=0,hi=1;
        if(std::abs(along)<1e-12){if(std::abs(d0)>radius)return false;}
        else {
            double first=(d0-radius)/along,last=(d0+radius)/along;if(first>last)std::swap(first,last);
            lo=std::max(lo,first);hi=std::min(hi,last);if(lo>hi)return false;
        }
        Vector lateral=objective-normal*product(objective,normal);double lateral_length=length(lateral),u=(lo+hi)*.5;
        if(lateral_length>1e-14) {
            double gradient=product(lateral,axis);
            if(std::abs(along)<1e-12)u=gradient>=0?hi:lo;
            else {
                double k=gradient/(lateral_length*along),d=-radius*k/std::hypot(1.,k);
                u=std::clamp((d0-d)/along,lo,hi);
            }
        }
        Vector center=a+axis*u;double d=plane-product(normal,center);
        out=center+normal*d;
        if(lateral_length>1e-14)out=out+lateral*(std::sqrt(std::max(0.,radius*radius-d*d))/lateral_length);
        return true;
    }
    bool support(const Hitbox& box,Pixel direction,Pixel& pixel)const {
        Vector a=box.a,b=box.shape==HitboxShape::Sphere?a:Vector(box.b);
        if(std::min(depth(a),depth(b))-box.radius*depth_length>near_depth) {
            if(!sphere(a,box.radius,direction,pixel))return false;
            if(box.shape==HitboxShape::Capsule) {
                Pixel other;if(!sphere(b,box.radius,direction,other))return false;
                if(product(other,direction)>product(pixel,direction))pixel=other;
            }
            return true;
        }
        Vector world=(depth(a)>depth(b)?a:b)+normal*box.radius;
        if(!point(world,pixel))return false;
        std::array<double,4> d{};for(int i=0;i<4;++i)d[i]=direction.x*projection.x[i]+direction.y*projection.y[i];
        double value=product(pixel,direction);
        // Fractional linear support with positive depth. Each step solves the
        // exact clipped linear support, rather than tessellating in world space.
        for(int iteration=0;iteration<64;++iteration) {
            if(!clipped_linear(box,xyz(d)-xyz(projection.w)*value,world)||!point(world,pixel))return false;
            double next=product(pixel,direction);if(std::abs(next-value)<1e-7)return true;value=next;
        }
        return false;
    }
    bool arc_on_screen(Pixel a,Pixel b,Pixel da,Pixel db)const {
        double determinant=cross2(da,db);if(std::abs(determinant)<1e-14)return true;
        double h0=product(a,da),h1=product(b,db);
        Pixel tangent{(h0*db.y-da.y*h1)/determinant,(da.x*h1-h0*db.x)/determinant};
        std::array<Pixel,3> triangle{a,b,tangent};
        // Separating axes for the small tangent triangle and the viewport. The
        // entire convex arc lies in this triangle; offscreen arcs need no work.
        auto separate=[&](Pixel axis) {
            double low=product(triangle[0],axis),high=low;
            for(auto p:triangle){low=std::min(low,product(p,axis));high=std::max(high,product(p,axis));}
            double rect_low=std::min(0.,axis.x*width)+std::min(0.,axis.y*height);
            double rect_high=std::max(0.,axis.x*width)+std::max(0.,axis.y*height);
            return high<rect_low||low>rect_high;
        };
        if(separate({1,0})||separate({0,1}))return false;
        for(int i=0;i<3;++i){auto edge=triangle[(i+1)%3]-triangle[i];if(separate({edge.y,-edge.x}))return false;}
        return true;
    }
    bool arc(const Hitbox& box,Pixel a,Pixel b,Pixel da,Pixel db,std::vector<Pixel>& points,int level=0)const {
        Pixel edge=b-a;double norm=std::hypot(edge.x,edge.y);
        if(norm<1e-8||!arc_on_screen(a,b,da,db)){points.push_back(b);return true;}
        Pixel direction{edge.y/norm,-edge.x/norm},middle;
        if(!support(box,direction,middle))return false;
        double error=product(middle-a,direction);
        if(error<=curve_tolerance){points.push_back(b);return true;}
        if(level>=32||points.size()>16384)return false;
        return arc(box,a,middle,da,direction,points,level+1)&&arc(box,middle,b,direction,db,points,level+1);
    }
    bool polygon(const Hitbox& box,std::vector<Pixel>& points,std::vector<Pixel>& scratch)const {
        points.clear();
        if(box.shape==HitboxShape::Box) {
            for(auto corner:box.corners){Pixel p;if(point(Vector(corner),p))points.push_back(p);}
            for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit)) {
                Vector a=box.corners[i],b=box.corners[i|bit];double wa=depth(a),wb=depth(b);
                if((wa>=near_depth)!=(wb>=near_depth)) {
                    Pixel p;if(point(a+(b-a)*((near_depth-wa)/(wb-wa)),p))points.push_back(p);
                }
            }
            hull(points,scratch);
        }else {
            if(std::max(depth(Vector(box.a)),depth(Vector(box.b)))+box.radius*depth_length<near_depth)return true;
            constexpr int divisions=8;std::array<Pixel,divisions> directions{},supports{};
            for(int i=0;i<divisions;++i) {
                double theta=i*6.283185307179586/divisions;directions[i]={std::cos(theta),std::sin(theta)};
                if(!support(box,directions[i],supports[i]))return false;
            }
            points.push_back(supports[0]);
            for(int i=0;i<divisions;++i)if(!arc(box,supports[i],supports[(i+1)%divisions],directions[i],directions[(i+1)%divisions],points))return false;
            if(!points.empty())points.pop_back();
        }
        clip(points,scratch,width,height);
        // Degenerate duplicates at the clip planes otherwise look like short
        // internal edges and interfere with the union's half-plane tests.
        points.erase(std::unique(points.begin(),points.end(),[](Pixel a,Pixel b){return std::hypot(a.x-b.x,a.y-b.y)<1e-7;}),points.end());
        if(points.size()>1&&std::hypot(points.front().x-points.back().x,points.front().y-points.back().y)<1e-7)points.pop_back();
        return true;
    }
};
struct Polygon {
    std::vector<Pixel> points;double left{},top{},right{},bottom{};
    void bounds() {
        left=top=std::numeric_limits<double>::max();right=bottom=-left;
        for(auto p:points){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}
    }
};
inline bool covered(Pixel a,Pixel b,const Polygon& polygon,bool owns_coincident,double& lo,double& hi) {
    if(polygon.points.size()<3||std::max(a.x,b.x)<polygon.left||std::min(a.x,b.x)>polygon.right
        ||std::max(a.y,b.y)<polygon.top||std::min(a.y,b.y)>polygon.bottom)return false;
    lo=0;hi=1;Pixel delta=b-a;
    for(size_t i=0;i<polygon.points.size();++i) {
        Pixel vertex=polygon.points[i],edge=polygon.points[(i+1)%polygon.points.size()]-vertex;
        double norm=std::hypot(edge.x,edge.y);if(norm<1e-10)continue;
        double value=cross2(edge,a-vertex)/norm,slope=cross2(edge,delta)/norm;
        if(std::abs(slope)<1e-9) {
            if(value< -1e-7)return false;
            // Same-facing coincident boundaries have one owner. Opposite-facing
            // boundaries join two solids and both disappear inside the union.
            if(std::abs(value)<1e-7&&owns_coincident&&product(edge,delta)>0)return false;
        }else {
            double t=-value/slope;
            if(slope>0)lo=std::max(lo,t);else hi=std::min(hi,t);
            if(lo>=hi)return false;
        }
    }
    return hi>lo;
}
}

// Convex silhouettes are combined as a polygon union by subtracting covered
// edge intervals. Gaps and holes survive, while overlap edges never reach ImGui.
class UnionOutline {
    std::vector<outline_detail::Polygon> polygons_;
    std::vector<Pixel> scratch_;
    std::vector<std::pair<double,double>> intervals_;
public:
    std::vector<OutlineSegment> segments;
    bool build(std::span<const Hitbox> boxes,const Matrix& matrix,float width,float height) {
        segments.clear();if(width<=0||height<=0||!projection_info(matrix).valid)return false;
        outline_detail::Projector projector(matrix,width,height);polygons_.resize(boxes.size());
        for(size_t i=0;i<boxes.size();++i) {
            auto& polygon=polygons_[i];
            if(!projector.polygon(boxes[i],polygon.points,scratch_)){segments.clear();return false;}
            polygon.bounds();
        }
        for(size_t i=0;i<polygons_.size();++i) {
            const auto& points=polygons_[i].points;if(points.size()<3)continue;
            for(size_t j=0;j<points.size();++j) {
                Pixel a=points[j],b=points[(j+1)%points.size()];intervals_.clear();
                if(std::hypot(a.x-b.x,a.y-b.y)<1e-6)continue;
                for(size_t k=0;k<polygons_.size();++k)if(i!=k) {
                    double lo{},hi{};if(outline_detail::covered(a,b,polygons_[k],i<k,lo,hi))intervals_.emplace_back(lo,hi);
                }
                std::sort(intervals_.begin(),intervals_.end());double start=0;
                auto emit=[&](double end){if(end-start>1e-8)segments.push_back({a+(b-a)*start,a+(b-a)*end});};
                for(auto [lo,hi]:intervals_){emit(lo);start=std::max(start,hi);}
                emit(1);
            }
        }
        return true;
    }
};
class ZoneOutlineCache {
    uint64_t revision_{};uint32_t target_{};Matrix matrix_{};float width_{},height_{};bool selected_{},ready_{};
public:
    UnionOutline activation,retention;
    bool prepare(const AdaptiveTargetZone& zone,const Matrix& matrix,float width,float height,bool selected) {
        if(revision_==zone.revision&&target_==zone.target&&matrix_==matrix&&width_==width&&height_==height&&selected_==selected)return ready_;
        revision_=zone.revision;target_=zone.target;matrix_=matrix;width_=width;height_=height;selected_=selected;
        ready_=activation.build(zone.activation,matrix,width,height);
        retention.segments.clear();if(selected)ready_=retention.build(zone.retention,matrix,width,height)&&ready_;
        return ready_;
    }
};
}
