#pragma once
#include "focus_view.h"
#include "focus_area.h"
#include "adaptive_settings.h"

namespace overlay {
struct AdaptiveMouseCorrection {
    long x{},y{};bool valid{};
    AngularError error{};ScreenPoint remaining{},requested{};
};
// A finite screen-scale error for directions outside the projection hemisphere.
// This is a steering error, never a point used by drawing, visibility or clicks.
inline bool spherical_screen_error(const Matrix& matrix,Vec3 point,float width,float height,AngularError& error,ScreenPoint& pixels) {
    CameraBasis basis;
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!finite(point)||!camera_basis(matrix,basis))return false;
    const auto direction=sub(point,basis.origin);if(dot(direction,direction)<1e-10f)return false;
    error=direction_error(basis,direction);
    auto length=[&](int row){return std::sqrt(matrix[row]*matrix[row]+matrix[row+1]*matrix[row+1]+matrix[row+2]*matrix[row+2]);};
    const float depth=length(12),fx=width*.5f*length(0)/depth,fy=height*.5f*length(4)/depth;
    pixels={fx*(error.x/angular_degrees),-fy*(error.y/angular_degrees)};
    return std::isfinite(error.distance)&&std::isfinite(pixels.x)&&std::isfinite(pixels.y);
}
// Visual feedback uses the current render projection and the same one-pixel to
// one-count convention as legacy focus. It does not infer a measured mouse gain.
class AdaptiveMouseResponse {
    float velocity_x_{},velocity_y_{},remainder_x_{},remainder_y_{};
    static long counts(float step,float remaining,float& remainder) {
        if(!std::isfinite(step)||!std::isfinite(remaining)){remainder=0;return 0;}
        if(remainder*remaining<0)remainder=0;
        if(std::abs(remaining)<1){remainder=0;return 0;}
        const float desired=std::clamp(step+remainder,-32767.f,32767.f);
        const long maximum=long(std::min(std::floor(std::abs(remaining)),32767.f));
        const long result=std::clamp(long(desired),-maximum,maximum);remainder=desired-float(result);return result;
    }
public:
    void clear(){velocity_x_=velocity_y_=remainder_x_=remainder_y_=0;}
    AdaptiveMouseCorrection step(const Matrix& matrix,Vec3 point,float width,float height,float dt,const AdaptiveSettings& settings,float selected_speed=0.f,bool full_sphere=false) {
        AdaptiveMouseCorrection result;CameraBasis basis;ScreenPoint projected;
        if(!std::isfinite(dt)||dt<=0||!std::isfinite(width)||!std::isfinite(height)||!camera_basis(matrix,basis)
            ||(!full_sphere&&!focus_project(matrix,point,width,height,projected))){clear();return result;}
        auto response=settings;sanitize_adaptive(response);
        const float speed=std::isfinite(selected_speed)&&selected_speed>0?std::clamp(selected_speed,1.f,140.f):response.speed;
        dt=std::min(dt,.025f);result.error=direction_error(basis,sub(point,basis.origin));
        if(!std::isfinite(result.error.distance)||(!full_sphere&&result.error.distance>=90)){clear();return result;}
        if(full_sphere) {
            if(!spherical_screen_error(matrix,point,width,height,result.error,result.remaining)){clear();return result;}
        }else result.remaining={projected.x-width*.5f,projected.y-height*.5f};
        const float gain=1-std::exp(-dt/(adaptive_response_ms(speed)/1000));
        float vx=result.error.x*gain/dt,vy=result.error.y*gain/dt;
        const float rate=std::hypot(vx,vy);if(rate>response.max_rate){vx*=response.max_rate/rate;vy*=response.max_rate/rate;}
        float ax=vx-velocity_x_,ay=vy-velocity_y_;const float acceleration=std::hypot(ax,ay),limit=response.max_acceleration*dt;
        if(acceleration>limit){ax*=limit/acceleration;ay*=limit/acceleration;}
        vx=velocity_x_+ax;vy=velocity_y_+ay;
        auto bounded=[](float value,float error){return value*error<=0?0.f:std::copysign(std::min(std::abs(value),std::abs(error)),error);};
        const float sx=bounded(vx*dt,result.error.x),sy=bounded(vy*dt,result.error.y);velocity_x_=sx/dt;velocity_y_=sy/dt;
        const float yaw=sx/angular_degrees,pitch=sy/angular_degrees;
        const Vec3 direction=add(add(mul(basis.forward,std::cos(pitch)*std::cos(yaw)),mul(basis.right,std::cos(pitch)*std::sin(yaw))),mul(basis.up,std::sin(pitch)));
        ScreenPoint step_pixel,origin_pixel;
        if(!focus_project(matrix,add(basis.origin,mul(direction,1024)),width,height,step_pixel)
            ||!focus_project(matrix,add(basis.origin,mul(basis.forward,1024)),width,height,origin_pixel)){clear();return result;}
        result.requested={step_pixel.x-origin_pixel.x,step_pixel.y-origin_pixel.y};
        result.x=counts(result.requested.x,result.remaining.x,remainder_x_);
        result.y=counts(result.requested.y,result.remaining.y,remainder_y_);result.valid=true;return result;
    }
};
}
