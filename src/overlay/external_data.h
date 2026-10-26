#pragma once
#include "focus_math.h"
#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
namespace overlay {
inline constexpr const char* joint_labels[]={"Head","Neck","Chest","Pelvis","Left upper arm","Right upper arm","Left forearm","Right forearm","Left hand","Right hand","Left thigh","Right thigh","Left knee","Right knee","Left foot","Right foot"};
inline constexpr const char* joint_names[]={"head","neck_0","spine_3","pelvis","arm_upper_L","arm_upper_R","arm_lower_L","arm_lower_R","hand_L","hand_R","leg_upper_L","leg_upper_R","leg_lower_L","leg_lower_R","ankle_L","ankle_R"};
inline unsigned joint_group(int i){return i<2?1u:i<4?2u:i<10?4u:8u;}
inline std::array<int,16> external_joint_map(const std::vector<std::string>& names,const std::string& prefix,int head,int torso,int pelvis) {
    std::array<int,16> result;result.fill(-1);
    for(int j=0;j<16;++j){auto wanted=prefix+joint_names[j];auto found=std::find_if(names.begin(),names.end(),[&](const auto& name){return name.size()==wanted.size()&&std::equal(name.begin(),name.end(),wanted.begin(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);});});if(found!=names.end())result[j]=int(found-names.begin());}
    result[0]=head;if(result[2]<0)result[2]=torso;result[3]=pelvis;
    if(result[1]<0){auto neck=std::find(names.begin(),names.end(),prefix+"neck");if(neck!=names.end())result[1]=int(neck-names.begin());}
    return result;
}
struct HelperReadState {
    bool time_valid{},paused{},sniper_valid{},sniper_ready{},sniper_can_scope{};
    float game_time{},sniper_started{},sniper_minimum{};
    uint32_t sniper{};int charges{};
    std::string sniper_status{"Sniper data not requested"};
};
struct PuddleThreat {uint32_t handle{};uint8_t team{};Vec3 position{},normal{};float radius{},reach{},impact{};};
}
