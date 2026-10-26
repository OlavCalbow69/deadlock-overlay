#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

namespace overlay {
struct VindictaAutoSelectSettings {
    bool enabled{},show_area{true};
    float angle{6},speed{70};
};
inline void sanitize_auto_select(VindictaAutoSelectSettings& s) {
    s.angle=std::clamp(std::isfinite(s.angle)?s.angle:6.f,1.f,25.f);
    s.speed=std::clamp(std::isfinite(s.speed)?s.speed:70.f,1.f,140.f);
}
inline bool read_auto_select_setting(VindictaAutoSelectSettings& s,std::string_view key,std::istream& in) {
    constexpr std::string_view prefix="vindicta_auto_select_";
    if(!key.starts_with(prefix))return false;key.remove_prefix(prefix.size());
    if(key=="enabled"||key=="show_area") {int n{};if(in>>n)(key=="enabled"?s.enabled:s.show_area)=n!=0;}
    else {float n{};if(in>>n&&std::isfinite(n)){if(key=="angle")s.angle=n;else if(key=="speed")s.speed=n;}}
    return true;
}
inline void write_auto_select_settings(std::ostream& out,const VindictaAutoSelectSettings& s) {
    out<<"vindicta_auto_select_enabled="<<s.enabled<<"\nvindicta_auto_select_show_area="<<s.show_area
       <<"\nvindicta_auto_select_angle="<<s.angle<<"\nvindicta_auto_select_speed="<<s.speed<<'\n';
}
struct AssassinateReadState {
    bool valid{},estimated{},cast_allowed{};
    uint32_t ability{};uint64_t sampled{};
    float damage{},low_bonus{},low_threshold{},head_bonus{},minimum_power{},full_charge{},range{},piercing{};
    float spirit{},cast_completed{};
    std::string source,status{"Assassinate damage not requested"},omissions;
};
enum AssassinateDefenseField : unsigned {
    DefenseResist=1,DefenseIncomingAll=2,DefenseIncomingSpirit=4,DefenseAbilityReduction=8,
    DefenseAllReduction=16,DefenseBarrier=32,DefenseStates=64
};
struct AssassinateDefense {
    uint64_t sampled{};unsigned known{};
    bool protected_state{};
    float resistance{},incoming_all{},incoming_spirit{},ability_reduction{},all_reduction{},barrier{};
    std::string omissions;
};
enum class AssassinateScopeObservation { Same,Waiting,Closed,Changed };
struct AssassinateScopeContinuity {
    float started{};uint64_t missing_since{};
    void reset(float value=0){started=value;missing_since=0;}
    AssassinateScopeObservation observe(bool scoped,float value,uint64_t now) {
        if(!scoped) {
            if(!missing_since)missing_since=now;
            return now-missing_since<100?AssassinateScopeObservation::Waiting:AssassinateScopeObservation::Closed;
        }
        missing_since=0;
        // Client prediction may correct the start time after the server ack.
        // A materially different start belongs to a newly opened scope.
        if(started>0&&std::abs(value-started)>.25f)return AssassinateScopeObservation::Changed;
        started=value;return AssassinateScopeObservation::Same;
    }
};
inline bool assassinate_health_eligible(int health,int maximum) {
    return health>0&&maximum>0&&health<=maximum&&int64_t(health)*100<int64_t(maximum)*49;
}
inline bool assassinate_head_hit(int group){return group==1||group==19;}
inline bool assassinate_cache_tick_usable(uint32_t current,uint32_t cached) {
    // The client restores global tickcount after prediction. A current-version
    // local cache can therefore be ahead of the externally observed clock.
    return cached>=current?cached-current<=16:current-cached<=1;
}
inline float assassinate_ring(const AssassinateReadState& a,float clock,float started,float minimum) {
    if(!a.valid||!std::isfinite(clock)||!std::isfinite(started)||started<=0||!std::isfinite(minimum)||a.full_charge<=0)return 0;
    return std::clamp((clock-started-minimum)/a.full_charge,0.f,1.f);
}
inline float assassinate_damage(const AssassinateReadState& a,const AssassinateDefense& d,
    int health,int maximum,float ring,bool head) {
    if(!a.valid||d.protected_state||health<=0||maximum<=0||!std::isfinite(ring))return 0;
    const float bonus=double(health)*100<double(maximum)*a.low_threshold?a.low_bonus:0.f;
    float packet=(a.damage+bonus)*(a.minimum_power+(1-a.minimum_power)*std::clamp(ring,0.f,1.f));
    if(head)packet*=1+a.head_bonus/100;
    packet*=std::max(0.f,1+(d.incoming_all+d.incoming_spirit)/100);
    const float resistance=d.resistance>0?d.resistance*std::clamp(1-a.piercing/100,0.f,1.f):d.resistance;
    packet*=std::max(0.f,1-resistance/100);
    packet*=std::max(0.f,1-d.ability_reduction/100);
    packet=std::max(0.f,packet-std::max(0.f,d.barrier));
    packet*=std::max(0.f,1-d.all_reduction/100);
    return std::isfinite(packet)?std::clamp(packet,0.f,10000000.f):0.f;
}
inline float assassinate_required_charge(const AssassinateReadState& a,const AssassinateDefense& d,
    int health,int maximum,bool head) {
    if(!a.valid||health<=0||maximum<=0||health>maximum)return std::numeric_limits<float>::infinity();
    if(assassinate_damage(a,d,health,maximum,1,head)<float(health))return std::numeric_limits<float>::infinity();
    if(assassinate_damage(a,d,health,maximum,0,head)>=float(health))return 0;
    float low=0,high=1;
    for(int i=0;i<16;++i){const float middle=(low+high)*.5f;if(assassinate_damage(a,d,health,maximum,middle,head)>=float(health))high=middle;else low=middle;}
    return high;
}
inline bool assassinate_acquirable(float required){return std::isfinite(required)&&required<=.600001f;}
}
