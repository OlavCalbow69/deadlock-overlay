#pragma once
#include "game_reader.h"

namespace overlay {
inline constexpr float angular_degrees=180.f/3.14159265358979323846f;
struct CameraBasis {Vec3 origin{},right{},up{},forward{};};
inline bool camera_basis(const Matrix& m,CameraBasis& b) {
    if(!projection_info(m).valid||!camera_origin(m,b.origin))return false;
    auto unit=[](Vec3 v){return mul(v,1/std::sqrt(dot(v,v)));};
    b.right=unit({m[0],m[1],m[2]});b.up=unit({m[4],m[5],m[6]});b.forward=unit({m[12],m[13],m[14]});return true;
}
struct AngularError {float x{},y{},distance{};};
inline AngularError direction_error(const CameraBasis& basis,Vec3 direction) {
    const float x=dot(direction,basis.right),y=dot(direction,basis.up),z=dot(direction,basis.forward);
    return {std::atan2(x,z)*angular_degrees,std::atan2(y,std::hypot(x,z))*angular_degrees,std::atan2(std::hypot(x,y),z)*angular_degrees};
}
inline AngularError view_rotation_error(const CameraBasis& basis,Vec3 direction) {
    auto result=direction_error(basis,direction);
    const auto angles=[](Vec3 v){return std::array<float,2>{std::atan2(v.y,v.x)*angular_degrees,std::atan2(v.z,std::hypot(v.x,v.y))*angular_degrees};};
    const auto from=angles(basis.forward),to=angles(direction);
    result.x=std::remainder(to[0]-from[0],360.f);result.y=to[1]-from[1];return result;
}
inline AngularError point_error(const CameraBasis& basis,Vec3 point){return view_rotation_error(basis,sub(point,basis.origin));}
}
