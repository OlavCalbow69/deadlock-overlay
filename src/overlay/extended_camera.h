#pragma once
#include "tracking_settings.h"
#include "adaptive_focus.h"
#include <unordered_map>
namespace overlay {
class ExtendedCamera {
    bool previous_{},latched_{},wait_release_{true},cycle_previous_{},spent_{};
    uint32_t retained_{},pid_{};int profile_{-1};uint64_t backend_{},fingerprint_{};double random_at_{};
    std::unordered_map<uint32_t,int> random_joints_;
    std::mt19937 entropy_{std::random_device{}()};
    static uint64_t fingerprint(const CameraFocusSettings& s){std::ostringstream out;write_tracking_settings(out,s.tracking,"");return std::hash<std::string>{}(out.str());}
public:
    Snapshot prepared;
    std::string status{"Standard controls"};
    bool active{},selection_changed{};
    void cancel(){focus_lease_until=0;latched_=active=false;wait_release_=true;spent_=false;retained_=0;random_joints_.clear();tracking_motion.clear();}
    void observe(uint32_t handle) {
        if(!active)return;
        if(handle)retained_=handle;
        else if(retained_&&!current_auto_switch_){spent_=true;status="Target lost / release activation";tracking_motion.clear();}
    }
    const Snapshot& prepare(const Snapshot& snapshot,CameraFocusSettings& runtime,HWND game,bool menu,bool owned,int profile,uint64_t backend) {
        auto t=runtime.tracking;selection_changed=false;tracking_motion.enabled=false;focus_reference_offset={};
        RECT r{};GetClientRect(game,&r);focus_pixel_width=float(r.right);focus_pixel_height=float(r.bottom);
        if(!t.enabled){if(active||latched_)cancel();status="Standard controls";return snapshot;}
        auto hash=fingerprint(runtime);
        if(snapshot.pid!=pid_||profile!=profile_||backend!=backend_||hash!=fingerprint_){cancel();pid_=snapshot.pid;profile_=profile;backend_=backend;fingerprint_=hash;}
        const bool primary=t.activation_key?physical_key_down(t.activation_key):focus_button_held(runtime.focus_hold_button);
        const bool down=primary||(t.second_key&&physical_key_down(t.second_key));
        owned=owned||(t.activation_key?key_program_owned(t.activation_key):focus_button_owned(runtime.focus_hold_button))||key_program_owned(t.second_key);
        if(!down&&!owned){wait_release_=false;if(!t.toggle||!latched_){spent_=false;retained_=0;random_joints_.clear();tracking_motion.clear();}}
        bool allowed=game&&IsWindow(game)&&!IsIconic(game)&&GetForegroundWindow()==game&&!menu&&!owned
            &&snapshot.time&&GetTickCount64()>=snapshot.time&&GetTickCount64()-snapshot.time<=50&&snapshot.projection.valid;
        if(!allowed){cancel();previous_=down;return snapshot;}
        if(down&&!previous_&&!wait_release_){if(spent_){spent_=false;retained_=0;latched_=true;}else latched_=t.toggle?!latched_:true;}
        previous_=down;if(!t.toggle&&!down)latched_=false;
        active=!wait_release_&&!spent_&&(t.toggle?latched_:down);
        if(active) {
            focus_lease_key=t.activation_key;focus_lease_second=t.second_key;focus_lease_button=int(runtime.focus_hold_button);focus_lease_toggle=t.toggle;focus_lease_until=GetTickCount64()+20;
        }else focus_lease_until=0;
        focus_reference_offset={r.right>0?t.offset_x/r.right:0,r.bottom>0?t.offset_y/r.bottom:0};
        current_auto_switch_=t.auto_switch;prepared=snapshot;
        const bool cycling=t.cycle_key&&physical_key_down(t.cycle_key);
        if(cycling&&!cycle_previous_&&active) {
            std::vector<uint32_t> choices;
            auto consider=[&](const FocusTarget& p){if(!focus_candidate(p,snapshot,{runtime.focus_players,runtime.focus_minions,runtime.focus_orbs,runtime.strict_enemy_players},runtime.exclude_teammates))return;
                if(runtime.visible_only&&!p.visible)return;auto at=p.dot_valid[1]?p.dots[1]:p.dots[0];ScreenPoint q;if(!focus_project(snapshot.matrix,at,float(r.right),float(r.bottom),q))return;choices.push_back(p.handle);};
            for(auto& p:snapshot.players)consider(p);for(auto& p:snapshot.focus_targets)consider(p);
            std::sort(choices.begin(),choices.end());if(!choices.empty()){auto it=std::find(choices.begin(),choices.end(),retained_);retained_=it==choices.end()||++it==choices.end()?choices.front():*it;spent_=false;selection_changed=true;tracking_motion.clear();}
        }cycle_previous_=cycling;
        bool new_random=GetTickCount64()>=random_at_;if(new_random){random_at_=double(GetTickCount64())+t.interval*1000;}
        auto update=[&](FocusTarget& p) {
            int joint=t.joint;
            if(t.strategy) {
                std::vector<int> choices;for(int i=0;i<16;++i)if((p.joints_valid&(1u<<i))&&(joint_group(i)&unsigned(t.groups))) {
                    ScreenPoint q;if(!focus_project(snapshot.matrix,p.joints[i],float(r.right),float(r.bottom),q))continue;
                    if(runtime.visible_only&&!visibility_allowed(true,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,p.joints[i]))continue;
                    choices.push_back(i);
                }
                if(choices.empty()){if(!runtime.free_focus)p.dot_valid.fill(false);return;}
                if(t.strategy==1){float best=FLT_MAX;auto center=focus_reference(float(r.right),float(r.bottom));for(int i:choices){ScreenPoint q;focus_project(snapshot.matrix,p.joints[i],float(r.right),float(r.bottom),q);float d=std::hypot(q.x-center.x,q.y-center.y);if(d<best){best=d;joint=i;}}}
                else {auto found=random_joints_.find(p.handle);int prior=found==random_joints_.end()?-1:found->second;
                    if(new_random||std::find(choices.begin(),choices.end(),prior)==choices.end()){if(choices.size()>1)std::erase(choices,prior);joint=choices[std::uniform_int_distribution<size_t>(0,choices.size()-1)(entropy_)];random_joints_[p.handle]=joint;}else joint=prior;}
            }
            p.preferred_joint=joint;
            if(!runtime.free_focus&&joint>=0) {
                const bool valid=(p.joints_valid&(1u<<joint))!=0;
                p.dots[0]=p.joints[joint];p.dot_valid[0]=valid;p.dot_visible[0]=valid&&visibility_allowed(true,snapshot.visibility,snapshot.camera_position,snapshot.camera_valid,p.dots[0]);
            }
        };
        for(auto& p:prepared.players)update(p);for(auto& p:prepared.focus_targets)update(p);
        std::erase_if(random_joints_,[&](const auto& entry){return std::none_of(prepared.players.begin(),prepared.players.end(),[&](auto& p){return p.handle==entry.first;})
            &&std::none_of(prepared.focus_targets.begin(),prepared.focus_targets.end(),[&](auto& p){return p.handle==entry.first;});});
        if(!runtime.free_focus&&(t.joint>=0||t.strategy))runtime.focus_bone=0;
        else if(adaptive_mode(runtime)&&t.joint>=0)runtime.focus_bone=t.joint+3;
        if(t.range>0) {
            auto out_of_range=[&](const auto& p){return p.origin_valid&&world_distance(p.origin,snapshot.local_player_valid?snapshot.local_player.origin:snapshot.camera_position)>t.range;};
            std::erase_if(prepared.players,out_of_range);std::erase_if(prepared.focus_targets,out_of_range);
        }
        if(active&&(t.lock_subject||selection_changed)&&retained_) {
            const bool exists=std::any_of(prepared.players.begin(),prepared.players.end(),[&](auto& p){return p.handle==retained_;})||std::any_of(prepared.focus_targets.begin(),prepared.focus_targets.end(),[&](auto& p){return p.handle==retained_;});
            if(!exists&&!t.auto_switch){spent_=true;active=false;focus_lease_until=0;prepared.players.clear();prepared.focus_targets.clear();status="Target lost / release activation";}
            else if(exists){std::erase_if(prepared.players,[&](auto& p){return p.handle!=retained_;});std::erase_if(prepared.focus_targets,[&](auto& p){return p.handle!=retained_;});}
            else retained_=0;
        }
        tracking_motion.settings=t;tracking_motion.enabled=active;tracking_motion.sample=snapshot.time;tracking_motion.error=FLT_MAX;
        auto measure=[&](const auto& p){ScreenPoint q;int b=std::clamp(runtime.focus_bone,0,2);if(p.dot_valid[b]&&focus_project(snapshot.matrix,p.dots[b],float(r.right),float(r.bottom),q)){auto c=focus_reference(float(r.right),float(r.bottom));tracking_motion.error=std::min(tracking_motion.error,std::hypot(q.x-c.x,q.y-c.y));}};
        for(auto& p:prepared.players)measure(p);for(auto& p:prepared.focus_targets)measure(p);
        status=spent_?"Target lost / release activation":active?"Extended tracking active":wait_release_?"Release activation to arm":"Waiting for activation";
        return prepared;
    }
private:bool current_auto_switch_{};
};
}
