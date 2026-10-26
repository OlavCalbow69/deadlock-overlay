#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace overlay {
// Value 2 is retained only for Fixed bone's internal acquisition geometry.
// It is no longer a selectable or persisted Adaptive free region.
enum class AdaptiveRegion : uint8_t { HeadTorso, WholeBody, SelectedBone };
inline AdaptiveRegion selectable_adaptive_region(int value) {return value==1?AdaptiveRegion::WholeBody:AdaptiveRegion::HeadTorso;}
enum class AdaptiveAreaMode : uint8_t { CrosshairAngle, TargetZone, PixelCircle, PixelSquare };
inline const char* adaptive_area_name(AdaptiveAreaMode mode) {
    return mode==AdaptiveAreaMode::TargetZone?"Target Zone":mode==AdaptiveAreaMode::PixelCircle?"Pixel circle":mode==AdaptiveAreaMode::PixelSquare?"Pixel square":"Crosshair angle";
}
inline const char* adaptive_region_name(AdaptiveRegion region) {
    return region==AdaptiveRegion::WholeBody?"Whole body":"Head and torso";
}
struct AdaptiveSettings {
    float radius_degrees{6.f},speed{70.f},max_rate{720.f},max_acceleration{6000.f};
    AdaptiveRegion region{AdaptiveRegion::HeadTorso};
    AdaptiveAreaMode area{AdaptiveAreaMode::CrosshairAngle};
    float padding_percent{12.f},pixel_radius{120.f};
    bool show_target_zone{true};
    bool prediction{true},show_area{};
};
inline void sanitize_adaptive(AdaptiveSettings& s) {
    auto bounded=[](float v,float low,float high,float fallback){return std::isfinite(v)?std::clamp(v,low,high):fallback;};
    s.radius_degrees=bounded(s.radius_degrees,1,25,6);s.speed=bounded(s.speed,1,100,70);
    s.max_rate=bounded(s.max_rate,30,720,720);s.max_acceleration=bounded(s.max_acceleration,300,6000,6000);
    s.region=selectable_adaptive_region(int(s.region));
    s.area=static_cast<AdaptiveAreaMode>(std::clamp(int(s.area),0,3));
    s.pixel_radius=bounded(s.pixel_radius,20,400,120);
    s.padding_percent=bounded(s.padding_percent,0,100,12);
}
// X1 retains its 1-100 range. X2 can extend the same curve up to 140.
inline float adaptive_response_ms(float speed) {return 250.f*std::pow(20.f/250.f,(std::clamp(speed,1.f,140.f)-1)/99.f);}
}
