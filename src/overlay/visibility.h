#pragma once
#include "focus_math.h"
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace overlay {
struct Triangle { Vec3 a,b,c; };
static_assert(sizeof(Triangle)==36);
inline float coordinate(Vec3 v,int axis){return axis==0?v.x:axis==1?v.y:v.z;}
inline std::string visibility_map_key(std::string name){
    std::replace(name.begin(),name.end(),'\\','/');
    auto slash=name.find_last_of('/');if(slash!=std::string::npos)name.erase(0,slash+1);
    auto dot_at=name.find('.');if(dot_at!=std::string::npos)name.resize(dot_at);
    if(name.empty()||name.size()>96)return {};
    for(auto& c:name){if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_'&&c!='-')return {};}
    return name;
}
// A gameplay world resource is maps/<map>/world. Portraits/UI scenes have
// additional path components and must never select the match collision mesh.
inline std::string visibility_world_map_key(std::string resource) {
    std::replace(resource.begin(),resource.end(),'\\','/');
    for(auto& c:resource)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(!resource.starts_with("maps/"))return {};
    auto end=resource.find('/',5);if(end==std::string::npos)return {};
    auto name=resource.substr(5,end-5),tail=resource.substr(end+1);
    if(tail!="world"&&tail!="world.vwrld"&&tail!="world.vwrld_c")return {};
    if(name=="start"||name=="scenes"||name=="ui"||visibility_map_key(name)!=name)return {};
    return name;
}
inline bool valid_world_vector(int count,uint32_t capacity,uintptr_t array) {
    return count>=0&&count<=256&&capacity>=uint32_t(count)&&capacity<=512
        &&(!count||(array>=0x10000&&array<=0x7fffffffffffULL));
}
inline bool add_visibility_world(std::string& selected,const std::string& candidate) {
    if(candidate.empty())return true;
    if(selected.empty()){selected=candidate;return true;}
    return selected==candidate; // Multiple distinct arenas are an ambiguous transition.
}
// Immutable after construction: shared between the sampler and renderer.
class VisibilityMesh {
    struct Node { Vec3 low{},high{};uint32_t first{},count{},left{},right{}; };
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    static Vec3 minimum(Vec3 a,Vec3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
    static Vec3 maximum(Vec3 a,Vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
    uint32_t build(uint32_t first,uint32_t count){
        uint32_t index=uint32_t(nodes_.size());nodes_.push_back({});
        Vec3 low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(uint32_t i=first;i<first+count;++i){auto& t=triangles_[i];low=minimum(low,minimum(t.a,minimum(t.b,t.c)));high=maximum(high,maximum(t.a,maximum(t.b,t.c)));}
        nodes_[index]={low,high,first,count,0,0};if(count<=8)return index;
        auto extent=sub(high,low);int axis=extent.y>extent.x?1:0;if(extent.z>coordinate(extent,axis))axis=2;
        uint32_t mid=first+count/2;
        std::nth_element(triangles_.begin()+first,triangles_.begin()+mid,triangles_.begin()+first+count,[axis](const Triangle& a,const Triangle& b){return coordinate(add(add(a.a,a.b),a.c),axis)<coordinate(add(add(b.a,b.b),b.c),axis);});
        auto left=build(first,mid-first),right=build(mid,first+count-mid);
        nodes_[index].count=0;nodes_[index].left=left;nodes_[index].right=right;return index;
    }
    static bool box_hit(const Node& n,Vec3 start,Vec3 direction,float near_t,float far_t){
        for(int axis=0;axis<3;++axis){
            auto s=coordinate(start,axis),d=coordinate(direction,axis),lo=coordinate(n.low,axis),hi=coordinate(n.high,axis);
            if(std::abs(d)<1e-12f){if(s<lo||s>hi)return false;continue;}
            float a=(lo-s)/d,b=(hi-s)/d;if(a>b)std::swap(a,b);
            near_t=std::max(near_t,a);far_t=std::min(far_t,b);if(near_t>far_t)return false;
        }return true;
    }
    static bool triangle_hit(const Triangle& triangle,Vec3 start,Vec3 direction,float near_t,float far_t){
        // Double sided Moller-Trumbore, finite segment rather than an infinite ray.
        auto e1=sub(triangle.b,triangle.a),e2=sub(triangle.c,triangle.a),p=cross(direction,e2);
        double det=dot(e1,p);if(std::abs(det)<1e-8)return false;
        auto v=sub(start,triangle.a);double u=dot(v,p)/det;if(u<0||u>1)return false;
        auto q=cross(v,e1);double w=dot(direction,q)/det;if(w<0||u+w>1)return false;
        double t=dot(e2,q)/det;return t>=near_t&&t<=far_t;
    }
    bool blocked(uint32_t index,Vec3 start,Vec3 direction,float near_t,float far_t)const{
        const auto& n=nodes_[index];if(!box_hit(n,start,direction,near_t,far_t))return false;
        if(n.count){for(uint32_t i=n.first;i<n.first+n.count;++i)if(triangle_hit(triangles_[i],start,direction,near_t,far_t))return true;return false;}
        return blocked(n.left,start,direction,near_t,far_t)||blocked(n.right,start,direction,near_t,far_t);
    }
public:
    bool assign(std::vector<Triangle> triangles){
        triangles_.clear();nodes_.clear();if(triangles.empty()||triangles.size()>8000000)return false;
        for(auto& t:triangles){for(auto v:{t.a,t.b,t.c})if(!finite(v)||std::abs(v.x)>1e7f||std::abs(v.y)>1e7f||std::abs(v.z)>1e7f)return false;}
        triangles.erase(std::remove_if(triangles.begin(),triangles.end(),[](const Triangle& t){auto n=cross(sub(t.b,t.a),sub(t.c,t.a));return dot(n,n)<1e-10f;}),triangles.end());
        if(triangles.empty())return false;triangles_=std::move(triangles);nodes_.reserve(triangles_.size()/2+1);build(0,uint32_t(triangles_.size()));return true;
    }
    bool load(const std::filesystem::path& path){
        triangles_.clear();nodes_.clear();std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return false;
        auto bytes=in.tellg();if(bytes<=0||bytes%sizeof(Triangle)||bytes>std::streamoff(8000000ULL*sizeof(Triangle)))return false;
        std::vector<Triangle> triangles(size_t(bytes)/sizeof(Triangle));in.seekg(0);if(!in.read(reinterpret_cast<char*>(triangles.data()),bytes))return false;
        return assign(std::move(triangles));
    }
    size_t triangle_count()const{return triangles_.size();}
    bool clear(Vec3 start,Vec3 end)const{
        if(nodes_.empty()||!finite(start)||!finite(end))return false;
        auto direction=sub(end,start);float length=std::sqrt(dot(direction,direction));if(!std::isfinite(length))return false;
        if(length<.002f)return true;
        // Only 0.001 units at endpoints; do not skip thin walls near the camera/target.
        float epsilon=.001f/length;return !blocked(0,start,direction,epsilon,1.f-epsilon);
    }
};
inline bool visibility_allowed(bool enabled,const std::shared_ptr<const VisibilityMesh>& mesh,Vec3 source,bool source_valid,Vec3 target){return !enabled||(mesh&&source_valid&&mesh->clear(source,target));}
}
