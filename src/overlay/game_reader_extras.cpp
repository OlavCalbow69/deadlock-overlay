#include "game_reader.h"
#include <cstring>
#include <unordered_set>
namespace overlay {
void GameReader::read_external_player(Player& p,const Entity& controller,const Entity& pawn) {
    const auto& f=profile_.features;
    if(f.player_name){char name[128]{};if(read(controller.address+f.player_name,name,sizeof(name))){name[127]=0;p.nickname=name;for(char& c:p.nickname)if(static_cast<unsigned char>(c)<32)c=' ';}}
    if(f.level){int n{};if(read(pawn.address+f.level,n)&&n>=0&&n<=100)p.level=n;}
}
void GameReader::read_observers(Snapshot& out,const std::array<uintptr_t,64>& chunks,uint64_t now) {
    const auto& f=profile_.features;
    if(!f.observer||!f.observer_mode||!f.observer_target||!f.player_name||!local_handle_)return;
    if(now>=next_observers_) {
        next_observers_=now+200;observer_cache_.clear();
        for(const auto& cached:controllers_) {
            auto c=entity(chunks,cached.handle);uint32_t handle{};if(c.address!=cached.address||!read(c.address+profile_.fields.pawn_handle,handle)||handle==local_handle_)continue;
            auto pawn=entity(chunks,handle);if(!pawn.address)continue;
            auto observer=pointer(pawn.address+f.observer);uint8_t mode{};uint32_t target{};
            // Source 2 uses IN_EYE=2 and CHASE=3; m_iObserverMode is one byte.
            if(!observer||!read(observer+f.observer_mode,mode)||!read(observer+f.observer_target,target)||target!=local_handle_||(mode!=2&&mode!=3))continue;
            Player p;read_external_player(p,c,pawn);if(!p.nickname.empty()&&entity(chunks,handle).address==pawn.address)observer_cache_.push_back(p.nickname);
        }
    }
    out.observers_valid=true;out.spectators=observer_cache_;
}
void GameReader::read_external_local(Snapshot& out,const std::array<uintptr_t,64>& chunks,const Entity& pawn,uintptr_t rules) {
    auto& h=out.helpers;const auto& f=profile_.features;
    auto globals=pointer(base_+profile_.addresses.global_vars);float simulation{};uint8_t paused{};
    if(!globals||!read(globals+0x30,h.game_time)||!std::isfinite(h.game_time)||h.game_time<0
        ||!read(pawn.address+profile_.fields.simulation_time,simulation)||std::abs(simulation-h.game_time)>.5f
        ||!read(rules+profile_.fields.server_paused,paused)||paused>1)return;
    h.time_valid=true;h.paused=paused!=0;
    if(vindicta(out.hero)&&out.hero.sniper_valid&&f.sniper_minimum&&f.charges) {
        auto ability=entity(chunks,out.hero.sniper_handle);auto data=pointer(ability.address+profile_.layout.ability_vdata);float cooldown{},post{};
        if(ability.address&&data&&read(data+f.sniper_minimum,h.sniper_minimum)&&std::isfinite(h.sniper_minimum)&&h.sniper_minimum>=0&&h.sniper_minimum<=3
            &&read(ability.address+f.charges,h.charges)&&h.charges>=0&&h.charges<=20
            &&read(ability.address+profile_.fields.ability_cooldown,cooldown)&&std::isfinite(cooldown)
            &&read(ability.address+profile_.fields.ability_post_cast,post)&&std::isfinite(post)) {
            h.sniper_valid=true;h.sniper=ability.handle;h.sniper_started=out.hero.scope_start;
            const bool ready=h.charges>0&&cooldown<=h.game_time&&post<=h.game_time;
            h.sniper_can_scope=ready&&!out.hero.sniper_scoped;h.sniper_ready=ready&&out.hero.sniper_scoped&&h.game_time-h.sniper_started>=h.sniper_minimum;
            h.sniper_status=!h.charges?"No Assassinate charges":h.sniper_ready?"Scope ready":h.sniper_can_scope?"Scope available":"Waiting for charge / cooldown";
        }
    }
    if(!h.sniper_valid)h.sniper_status="Assassinate timing unavailable";
}
void GameReader::read_puddle(Snapshot& out,const std::array<uintptr_t,64>& chunks,const Entity& pawn,uint8_t team) {
    const auto& f=profile_.features;if(!f.puddle_position||!f.puddle_normal||!f.puddle_state||!f.puddle_next)return;
    // Remote pawns may not replicate the public ability vector. Resolve the
    // stable slot map (already validated by the primary/melee reader) instead.
    auto now=GetTickCount64();auto cached=puddle_cache_.find(pawn.handle);
    if(cached==puddle_cache_.end()||now>=cached->second.second) {
        std::array<unsigned char,72> map{},again{};auto at=pawn.address+profile_.fields.ability_component+24;
        if(!read(at,map))return;uint32_t capacity{};uintptr_t entries{};
        std::memcpy(&capacity,map.data()+44,4);capacity&=0x7fffffff;std::memcpy(&entries,map.data()+48,8);
        if(!entries||!capacity||capacity>128)return;
        std::vector<unsigned char> nodes(capacity*12);if(!read(entries,nodes.data(),nodes.size()))return;
        uint32_t resolved{};
        for(uint32_t i=0;i<capacity;++i){uint32_t handle{};uint16_t key{},slot{};
            std::memcpy(&key,nodes.data()+i*12,2);std::memcpy(&handle,nodes.data()+i*12+4,4);
            auto a=entity(chunks,handle);if(!a.address||type(a.address)!=".?AVCCitadel_Ability_Viscous_Telepunch@@"
                ||!read(a.address+profile_.fields.ability_slot,slot)||slot!=key)continue;
            if(resolved&&resolved!=handle)return;resolved=handle;
        }
        if(!read(at,again)||map!=again||entity(chunks,pawn.handle).address!=pawn.address)return;
        if(puddle_cache_.size()>=512)puddle_cache_.clear();cached=puddle_cache_.insert_or_assign(pawn.handle,std::pair{resolved,now+1000}).first;
    }
    auto handle=cached->second.first;if(handle) {
        auto a=entity(chunks,handle);if(!a.address){puddle_cache_.erase(cached);return;}
        uint8_t state{};PuddleThreat p;p.handle=handle;p.team=team;
        if(!read(a.address+f.puddle_state,state)||state!=3||!read(a.address+f.puddle_position,p.position)||!finite(p.position)
            ||!read(a.address+f.puddle_normal,p.normal)||!finite(p.normal)||!read(a.address+f.puddle_next,p.impact)||!std::isfinite(p.impact))return;
        uint8_t radius_dirty{1},reach_dirty{1};
        // Read the effective upgraded property caches only after the updater has
        // verified their constructor registration in this exact DLL.
        if(!f.puddle_radius||!f.puddle_reach
            ||!read(a.address+f.puddle_radius+profile_.layout.property_dirty,radius_dirty)||radius_dirty
            ||!read(a.address+f.puddle_reach+profile_.layout.property_dirty,reach_dirty)||reach_dirty
            ||!read(a.address+f.puddle_radius+profile_.layout.property_cached_value,p.radius)
            ||!read(a.address+f.puddle_reach+profile_.layout.property_cached_value,p.reach)
            ||!std::isfinite(p.radius)||!std::isfinite(p.reach)
            ||p.radius<1||p.radius>600||p.reach<1||p.reach>1000)return;
        uint8_t after{};if(!read(a.address+f.puddle_state,after)||state!=after||entity(chunks,handle).address!=a.address)return;
        out.puddles.push_back(p);
    }
}
}
