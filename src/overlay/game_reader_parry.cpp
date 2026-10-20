#include "game_reader.h"
#include <cstring>
#include <limits>

namespace overlay {
void GameReader::read_melee_template(const std::array<uintptr_t,64>& chunks,const Entity& pawn) {
    auto previous=std::move(melee_template_);melee_template_={};const auto& f=profile_.fields;const auto& v=profile_.layout;
    auto fail=[&](const char* reason){melee_template_.status=reason;};
    auto ability=ability_slot(chunks,pawn,22);
    if(!ability.address||pointer(ability.address)!=base_+profile_.addresses.hold_melee_vtable){fail("Standard melee ability unavailable");return;}
    auto vdata=pointer(ability.address+v.ability_vdata);
    MeleeTemplate data;
    data.vdata=vdata;
    if(!vdata||!read(vdata+f.ability_properties,data.property_map)){fail("Melee property dictionary unavailable");return;}
    uint32_t property_capacity{},property_count{};uintptr_t properties{};int property_root{};
    std::memcpy(&property_capacity,data.property_map.data()+12,4);property_capacity&=0x7fffffff;
    std::memcpy(&properties,data.property_map.data()+16,8);std::memcpy(&property_root,data.property_map.data()+24,4);
    std::memcpy(&property_count,data.property_map.data()+28,4);
    if(!properties||!property_count||property_count>property_capacity||property_capacity>256||property_root<0||uint32_t(property_root)>=property_capacity) {
        fail("Melee property dictionary invalid");return;
    }
    // Resolve definitions directly from VData. The local property views can
    // remain unbound until the local player actually performs a melee attack.
    // Cache only addresses; read current base values and validate the map again.
    if(previous.valid&&previous.vdata==vdata&&previous.property_map==data.property_map) {
        data.range_definition=previous.range_definition;data.angle_definition=previous.angle_definition;
    }else {
        std::array<bool,256> visited{};std::vector<int> pending{property_root};
        while(!pending.empty()&&(!data.range_definition||!data.angle_definition)) {
            int index=pending.back();pending.pop_back();
            if(index<0||uint32_t(index)>=property_capacity||visited[size_t(index)]){fail("Melee property dictionary changed");return;}
            visited[size_t(index)]=true;
            const auto at=properties+v.ability_property_stride*size_t(index);
            std::array<unsigned char,24> node{};int left{},right{};uintptr_t key{};
            if(!read(at,node)){fail("Melee property node unavailable");return;}
            std::memcpy(&left,node.data(),4);std::memcpy(&right,node.data()+4,4);std::memcpy(&key,node.data()+16,8);
            auto name=text(key,64);
            if(name=="MeleeAttackLength")data.range_definition=at+24;
            else if(name=="MeleeHalfAngle")data.angle_definition=at+24;
            if(left!=-1)pending.push_back(left);if(right!=-1)pending.push_back(right);
        }
    }
    if(!data.range_definition||!data.angle_definition
        ||!read(data.range_definition+v.property_base_value,data.range)||!read(data.angle_definition+v.property_base_value,data.angle)
        ||!std::isfinite(data.range)||data.range<20||data.range>1000||!std::isfinite(data.angle)||data.angle<1||data.angle>85) {
        fail("Melee base range or angle unavailable");return;
    }
    // Remote bonus range and parry-pierce are sampled separately. Never borrow
    // the local player's item-modified cached melee range.
    std::array<unsigned char,40> map{},again{};uint32_t capacity{};uintptr_t entries{};int root{};
    if(!read(vdata+f.melee_attacks,map)){fail("Melee attack definitions unavailable");return;}
    std::memcpy(&capacity,map.data()+12,4);capacity&=0x7fffffff;std::memcpy(&entries,map.data()+16,8);std::memcpy(&root,map.data()+24,4);
    if(!entries||!capacity||capacity>64){fail("Melee attack definitions invalid");return;}
    for(int kind=1;kind<=4;++kind) {
        auto index=root;bool found=false;
        for(uint32_t steps=0;steps<capacity&&index>=0&&uint32_t(index)<capacity;++steps) {
            std::array<int32_t,6> node{};auto at=entries+v.melee_attack_stride*size_t(index);
            if(!read(at,node)){fail("Melee attack definition unreadable");return;}
            if(node[4]==kind) {
                uint8_t parryable{};
                if(!read(at+24+f.attack_parryable,parryable)||parryable>1||!read(at+24+v.melee_attack_duration,data.duration[kind])
                    ||!std::isfinite(data.duration[kind])||data.duration[kind]<0||data.duration[kind]>2){fail("Melee attack definition invalid");return;}
                data.parryable[kind]=parryable!=0;found=true;break;
            }
            index=node[kind<node[4]?0:1];
        }
        if(!found){fail("Required melee attack definition missing");return;}
    }
    if(!read(vdata+f.melee_attacks,again)||again!=map||pointer(ability.address+v.ability_vdata)!=vdata
        ||!read(vdata+f.ability_properties,again)||again!=data.property_map
        ||entity(chunks,ability.handle).address!=ability.address||entity(chunks,pawn.handle).address!=pawn.address) {
        fail("Melee definition changed during sample");return;
    }
    data.valid=true;data.status="Ready / live base melee definition";melee_template_=std::move(data);
}
GameReader::Entity GameReader::ability_slot(const std::array<uintptr_t,64>& chunks,const Entity& pawn,uint16_t slot) const {
    // Same validated slot-map layout used by the primary-weapon reader. The
    // public vector can be empty for remote players; their slot map is separate.
    std::array<unsigned char,72> map{},again{};
    auto component=pawn.address+profile_.fields.ability_component;
    if(!read(component+24,map))return {};
    uint32_t buckets{},capacity{},entry_capacity{},count{};uintptr_t bucket_array{},entries{};
    std::memcpy(&buckets,map.data(),4);std::memcpy(&capacity,map.data()+4,4);std::memcpy(&bucket_array,map.data()+8,8);
    std::memcpy(&entry_capacity,map.data()+44,4);std::memcpy(&entries,map.data()+48,8);std::memcpy(&count,map.data()+60,4);
    capacity&=0x7fffffff;entry_capacity&=0x7fffffff;
    if(!buckets||buckets>512||(buckets&(buckets-1))||capacity<buckets||!bucket_array||!entries||!count||entry_capacity>128)return {};
    uint32_t hash=uint32_t(slot)*uint32_t(-2048144789);hash^=hash>>13;hash*=uint32_t(-1028477387);hash^=hash>>16;
    int index{};if(!read(bucket_array+4*(hash&(buckets-1)),index))return {};
    for(uint32_t steps=0;steps<entry_capacity&&index>=0&&uint32_t(index)<entry_capacity;++steps) {
        std::array<unsigned char,12> entry{};const auto at=entries+12*size_t(index);
        if(!read(at,entry))return {};uint16_t key{};uint32_t handle{};
        std::memcpy(&key,entry.data(),2);std::memcpy(&handle,entry.data()+4,4);
        if(key==slot) {
            auto result=entity(chunks,handle);uint32_t after{};
            return result.address&&read(at+4,after)&&after==handle&&read(component+24,again)&&again==map
                &&entity(chunks,pawn.handle).address==pawn.address?result:Entity{};
        }
        std::memcpy(&index,entry.data()+8,4);
    }
    return {};
}
bool GameReader::modifier_value(const Entity& pawn,uint32_t index,float& value) const {
    value=0;const auto prop=pointer(pawn.address+profile_.fields.modifier_property);
    if(!prop||index>=1024)return false;
    uint8_t group{};uint32_t version{},after{};
    if(!read(prop+0x210,version)||!read(prop+160+index,group))return false;
    if(group!=255) {
        uint8_t policy{},fallback{};uint16_t dirty{};std::array<uint32_t,12> cache{},again{};uint32_t tick{};
        if(!read(base_+profile_.addresses.modifier_policy+index,policy)||!read(base_+profile_.addresses.modifier_fallback,fallback)
            ||!read(prop+0x214+2*index,dirty)||!read(prop+profile_.layout.modifier_cache+48*index,cache))return false;
        if(!policy)policy=fallback;
        if(policy==4) {auto globals=pointer(base_+profile_.addresses.global_vars);if(!globals||!read(globals+68,tick))return false;}
        float number{};std::memcpy(&number,&cache[8],4);
        if(!read(prop+0x210,after)||!read(prop+profile_.layout.modifier_cache+48*index,again)||again!=cache
            ||!valid_modifier_cache(cache[0],cache[2],version,after,policy,cache[3],tick,dirty,cache[4],number,cache[1]))return false;
        value=cache[4]?number:0;
    }
    return std::isfinite(value)&&read(prop+0x210,after)&&version==after&&pointer(pawn.address+profile_.fields.modifier_property)==prop;
}
bool GameReader::collision_hitbox(const Entity& object,Hitbox& out) const {
    const auto& f=profile_.fields;const auto collision=pointer(object.address+f.collision),scene=pointer(object.address+f.scene_node);
    std::array<Vec3,2> bounds{},again{};std::array<float,8> transform{};
    if(!collision||!scene||!read(collision+f.collision_min,bounds)||!read(scene+f.node_world,transform)
        ||!finite(bounds[0])||!finite(bounds[1])||bounds[0].x>=bounds[1].x||bounds[0].y>=bounds[1].y||bounds[0].z>=bounds[1].z)return false;
    ModelHitbox model{bounds[0],bounds[1],0,-1,0,HitboxShape::Box};
    if(!world_hitbox(model,transform,out)||!read(collision+f.collision_min,again)
        ||std::memcmp(bounds.data(),again.data(),sizeof(bounds))||pointer(object.address+f.collision)!=collision||pointer(object.address+f.scene_node)!=scene)return false;
    return true;
}
void GameReader::read_parry_local(ParryReadState& out,const std::array<uintptr_t,64>& chunks,const Entity& pawn,uintptr_t rules) {
    read_melee_template(chunks,pawn);
    out.melee_definition_valid=melee_template_.valid;out.melee_definition_status=melee_template_.status;
    out.local_handle=pawn.handle;out.sampled=GetTickCount64();const auto& f=profile_.fields;const auto& v=profile_.layout;
    const auto globals=pointer(base_+profile_.addresses.global_vars);uint8_t paused{};float simulation{};
    if(!globals||!read(globals+0x30,out.game_time)||!read(globals+84,out.interval)||!std::isfinite(out.game_time)
        ||out.game_time<0||out.game_time>10000000||!std::isfinite(out.interval)||out.interval<.001f||out.interval>.1f
        ||!read(pawn.address+f.simulation_time,simulation)||!std::isfinite(simulation)||std::abs(simulation-out.game_time)>.5f
        ||!rules||!read(rules+f.server_paused,paused)||paused>1) {out.status="Parry game clock unavailable";return;}
    out.paused=paused!=0;
    auto ability=ability_slot(chunks,pawn,19);
    if(!ability.address||pointer(ability.address)!=base_+profile_.addresses.parry_vtable) {out.status="Normal parry ability unavailable";return;}
    const auto prop=pointer(pawn.address+f.modifier_property);std::array<uint32_t,11> enabled{},predicted{},after{};
    if(!prop||!read(prop+f.state_enabled,enabled)||!read(prop+f.state_predicted,predicted)) {out.status="Parry state unavailable";return;}
    auto state=[&](uint32_t index){return index<352&&((enabled[index/32]|predicted[index/32])&(1u<<(index%32)))!=0;};
    float cooldown{},post_cast{},start{},success{},duration{};uint8_t cooling{},interrupt{};
    auto vdata=pointer(ability.address+v.ability_vdata);
    if(!vdata||!read(ability.address+f.ability_cooldown,cooldown)||!read(ability.address+f.ability_cooling,cooling)||cooling>1
        ||!read(ability.address+f.ability_post_cast,post_cast)||!read(ability.address+f.parry_start,start)
        ||!read(ability.address+f.parry_success_end,success)||!read(vdata+f.parry_duration,duration)
        ||!read(pawn.address+f.ability_component+f.ability_interrupt,interrupt)||interrupt>1
        ||!std::isfinite(cooldown)||!std::isfinite(post_cast)||!std::isfinite(start)||!std::isfinite(success)
        ||!std::isfinite(duration)||duration<=0||duration>2||cooldown<0||cooldown>out.game_time+7200) {out.status="Parry cooldown data unavailable";return;}
    out.ability=ability.handle;out.cooldown_remaining=std::max(0.f,cooldown-out.game_time);
    out.active=state(v.state_parry_active)||(start>0&&out.game_time>=start&&out.game_time<start+duration)||(success>out.game_time&&success-out.game_time<1);
    bool blocked=state(v.state_stunned)||state(v.state_sleep)||state(v.state_command)||state(v.state_zipline)
        ||state(v.state_parry_disabled)||state(v.state_urn)||interrupt;
    const bool dash=state(v.state_ground_dash)||state(v.state_air_dash),force=state(v.state_force_parry);
    if(!force&&!dash&&(state(v.state_shooting_disabled)||(state(v.state_melee_disabled)&&!state(v.state_parry_ignores_melee))||state(v.state_melee_debuff)))blocked=true;
    out.ready=!blocked&&!cooling&&out.cooldown_remaining<=0&&post_cast<=out.game_time&&!out.active;
    out.status=out.active?"Parry active":blocked?"Parry blocked by current player state":cooling||out.cooldown_remaining>0?"Parry cooldown":post_cast>out.game_time?"Parry recovery":"Ready";
    Hitbox hull;
    if(!collision_hitbox(pawn,hull)) {out.status="Local collision geometry unavailable";return;}
    out.local_hitboxes.push_back(hull);
    // The modifier vector validates membership, including item sales and swaps.
    // RTTI scans are infrequent; the cached watcher is revalidated every sample.
    std::array<unsigned char,24> header{},header_after{};int count{};uint32_t capacity{};uintptr_t array{};
    if(read(prop+f.modifier_list,header)) {
        std::memcpy(&count,header.data(),4);std::memcpy(&array,header.data()+8,8);std::memcpy(&capacity,header.data()+16,4);capacity&=0x7fffffff;
        if(count>=0&&count<=2048&&capacity>=uint32_t(count)&&capacity<=4096&&(array||!count)) {
            std::vector<uintptr_t> modifiers(size_t(count),0);
            if((!count||read(array,modifiers.data(),modifiers.size()*8))&&read(prop+f.modifier_list,header_after)&&header==header_after) {
                if(std::find(modifiers.begin(),modifiers.end(),counterspell_watcher_)==modifiers.end())counterspell_watcher_=0;
                if(!counterspell_watcher_&&GetTickCount64()>=next_counterspell_scan_) {
                    next_counterspell_scan_=GetTickCount64()+250;
                    for(auto modifier:modifiers)if(modifier&&pointer(modifier)==base_+profile_.addresses.counterspell_watcher_vtable){counterspell_watcher_=modifier;break;}
                }
                if(counterspell_watcher_&&pointer(counterspell_watcher_)==base_+profile_.addresses.counterspell_watcher_vtable) {
                    uint8_t disabled{},ready{},armed{};uint32_t handle{},serial{},serial_after{};float item_end{};uint8_t item_cooling{};
                    auto watcher=counterspell_watcher_;
                    if(read(watcher+f.modifier_serial,serial)&&read(watcher+f.modifier_disabled,disabled)&&!disabled
                        &&read(watcher+f.modifier_ready,ready)&&ready==1&&read(watcher+f.modifier_ability,handle)
                        &&read(watcher+v.counterspell_armed,armed)&&armed<=1) {
                        auto item=entity(chunks,handle);uint32_t owner{};
                        if(item.address&&read(item.address+f.owner_handle,owner)&&owner==pawn.handle
                            &&read(item.address+f.ability_cooldown,item_end)&&std::isfinite(item_end)&&item_end>=0&&item_end<=out.game_time+7200
                            &&read(item.address+f.ability_cooling,item_cooling)&&item_cooling<=1
                            &&read(watcher+f.modifier_serial,serial_after)&&serial==serial_after&&entity(chunks,handle).address==item.address) {
                            out.counterspell_owned=true;out.counterspell=handle;out.counterspell_active=armed!=0;
                            out.spell_cooldown_remaining=std::max(0.f,item_end-out.game_time);out.counterspell_ready=!item_cooling&&out.spell_cooldown_remaining<=0;
                        }
                    }
                }
            }
        }
    }
    if(!read(prop+f.state_enabled,after)||after!=enabled||!read(prop+f.state_predicted,after)||after!=predicted
        ||entity(chunks,ability.handle).address!=ability.address||entity(chunks,pawn.handle).address!=pawn.address
        ||pointer(pawn.address+f.modifier_property)!=prop||pointer(base_+profile_.addresses.global_vars)!=globals) {out.valid=false;out.status="Parry state changed during sample";return;}
    out.valid=true;
}
MeleeThreatSample GameReader::read_melee(const std::array<uintptr_t,64>& chunks,const Entity& pawn,const Player& player) {
    MeleeThreatSample out;out.pawn=pawn.handle;out.team=player.team;out.name=player.model_name;out.status="Melee ability unavailable";
    const auto& f=profile_.fields;const auto& v=profile_.layout;
    auto ability=ability_slot(chunks,pawn,22);
    if(!ability.address)return read_replicated_melee(chunks,pawn,player);
    if(pointer(ability.address)!=base_+profile_.addresses.hold_melee_vtable){out.status="Unsupported melee class";return out;}
    out.ability=ability.handle;uint8_t using_melee{},hit{};int after_state{},after_type{};float after_trigger{};
    if(!read(ability.address+f.melee_state,out.state)||!read(ability.address+f.melee_type,out.attack_type)||out.state<0||out.state>5||out.attack_type<0||out.attack_type>4
        ||!read(ability.address+f.melee_using,using_melee)||using_melee>1||!read(ability.address+f.melee_hit,hit)||hit>1
        ||!read(ability.address+f.melee_state_start,out.state_start)||!read(ability.address+f.melee_trigger,out.trigger_time)
        ||!read(ability.address+f.melee_activated,out.activated)||!read(pawn.address+f.simulation_time,out.simulation_time)
        ||!std::isfinite(out.state_start)||!std::isfinite(out.trigger_time)||!std::isfinite(out.activated)||!std::isfinite(out.simulation_time)) {out.status="Melee state unavailable";return out;}
    out.using_melee=using_melee!=0;out.hit=hit!=0;
    if(out.state==0||out.state==1){out.status=out.state==0?"Idle":"Charging / not committed";return out;}
    if(!out.attack_type)out.attack_type=out.state==3?3:2; // Same anticipated-attack fallback as the game.
    const auto vdata=pointer(ability.address+v.ability_vdata);std::array<unsigned char,40> map{},again{};
    if(!vdata||!read(vdata+f.melee_attacks,map)){out.status="Attack definitions unavailable";return out;}
    uint32_t capacity{};uintptr_t entries{},attack{};int index{};
    std::memcpy(&capacity,map.data()+12,4);capacity&=0x7fffffff;std::memcpy(&entries,map.data()+16,8);std::memcpy(&index,map.data()+24,4);
    if(!entries||!capacity||capacity>64){out.status="Attack definitions invalid";return out;}
    for(uint32_t steps=0;steps<capacity&&index>=0&&uint32_t(index)<capacity;++steps) {
        std::array<int32_t,6> node{};auto at=entries+v.melee_attack_stride*size_t(index);
        if(!read(at,node))break;
        if(node[4]==out.attack_type){attack=at+24;break;}
        index=node[out.attack_type<node[4]?0:1];
    }
    uint8_t parryable{},dirty{};
    auto property=[&](uintptr_t address,float& value) {return read(address+v.property_dirty,dirty)&&!dirty
        &&read(address+v.property_cached_value,value)&&std::isfinite(value);};
    float bonus{},pierce{};
    if(!attack||!read(attack+f.attack_parryable,parryable)||parryable>1||!read(attack+v.melee_attack_duration,out.attack_duration)
        ||!std::isfinite(out.attack_duration)||out.attack_duration<0||out.attack_duration>2
        ||!property(ability.address+v.melee_range_property,out.range)||!property(ability.address+v.melee_angle_property,out.half_angle)
        ||!modifier_value(pawn,v.melee_range_index,bonus)||!modifier_value(pawn,v.parry_pierce_index,pierce)) {out.status="Melee parameters not fresh";return out;}
    out.range+=bonus;out.parryable=parryable!=0;out.piercing=pierce>0;
    if(out.range<20||out.range>1500||out.half_angle<1||out.half_angle>85||!player.dot_valid[0]) {out.status="Melee geometry unavailable";return out;}
    Vec3 angles{};
    if(!read(out.state==4?ability.address+f.melee_forced_angles:pawn.address+f.eye_angles,angles)||!finite(angles)||std::abs(angles.x)>90||std::abs(angles.y)>3600) {out.status="Attack direction unavailable";return out;}
    constexpr float radians=.01745329252f;
    out.direction={std::cos(angles.x*radians)*std::cos(angles.y*radians),std::cos(angles.x*radians)*std::sin(angles.y*radians),-std::sin(angles.x*radians)};
    out.origin=sub(player.dots[0],{0,0,6}); // External approximation of the melee eye origin.
    out.velocity=player.motion.velocity_valid?player.motion.velocity:player.velocity_valid?player.velocity:Vec3{};
    if(!read(ability.address+f.melee_state,after_state)||after_state!=out.state||!read(ability.address+f.melee_type,after_type)
        ||(after_type!=out.attack_type&&after_type!=0)||!read(ability.address+f.melee_trigger,after_trigger)||after_trigger!=out.trigger_time
        ||!read(vdata+f.melee_attacks,again)||again!=map||pointer(ability.address+v.ability_vdata)!=vdata
        ||entity(chunks,ability.handle).address!=ability.address||entity(chunks,pawn.handle).address!=pawn.address) {out.status="Melee state changed during sample";return out;}
    out.valid=true;out.status=out.piercing?"Parry piercing":out.parryable?"Committed melee":"Attack cannot be parried";return out;
}
MeleeThreatSample GameReader::read_replicated_melee(const std::array<uintptr_t,64>& chunks,const Entity& pawn,const Player& player) {
    MeleeThreatSample out;out.pawn=pawn.handle;out.team=player.team;out.name=player.model_name;out.status="Replicated heavy melee / idle";
    const auto& f=profile_.fields;const auto& v=profile_.layout;const auto now=GetTickCount64();
    if(replicated_melee_.size()>128)for(auto i=replicated_melee_.begin();i!=replicated_melee_.end();)i=now-i->second.seen>1000?replicated_melee_.erase(i):std::next(i);
    if(replicated_melee_.size()>=128&&!replicated_melee_.contains(pawn.handle)){out.status="Replicated melee cache full";return out;}
    auto& tracking=replicated_melee_[pawn.handle];
    if(now<tracking.seen||now-tracking.seen>50||tracking.model!=player.model_name)tracking={};
    tracking.seen=now;tracking.model=player.model_name;
    // The bench has a separate attack state machine. Its shape and timing are
    // deliberately not inferred from the standard heavy-melee masks.
    if(player.model_name.find("baba")!=std::string::npos){out.status="Special melee form unsupported";tracking.charged=false;return out;}
    const auto prop=pointer(pawn.address+f.modifier_property);std::array<uint32_t,11> enabled{},predicted{},after{};
    if(!prop||!read(prop+f.state_enabled,enabled)||!read(prop+f.state_predicted,predicted)
        ||!read(pawn.address+f.simulation_time,out.simulation_time)||!std::isfinite(out.simulation_time)) {out.status="Replicated melee state unavailable";return out;}
    auto state=[&](uint32_t index){return index<352&&((enabled[index/32]|predicted[index/32])&(1u<<(index%32)))!=0;};
    const bool charged=state(v.state_charge_melee),locked=state(v.state_input_disabled)||state(v.state_jump_disabled);
    if(!read(prop+f.state_enabled,after)||after!=enabled||!read(prop+f.state_predicted,after)||after!=predicted
        ||pointer(pawn.address+f.modifier_property)!=prop||entity(chunks,pawn.handle).address!=pawn.address) {out.status="Replicated melee changed during sample";return out;}
    out.replicated=true;
    if(!charged){tracking.charged=false;tracking.started=tracking.committed=0;return out;}
    if(locked) {
        if(!tracking.charged)tracking.started=out.simulation_time;
        tracking.charged=true;tracking.committed=0;out.state=1;out.status="Heavy-melee wind-up / not committed";return out;
    }
    // Require observing the wind-up first. Starting mid-attack cannot mistake
    // the identical post-hit mask for a new incoming punch.
    if(!tracking.charged){out.status="Heavy melee already underway / waiting for next wind-up";return out;}
    if(!state(v.state_shooting_disabled)||!state(v.state_aim_forward_pitch)||state(v.state_stunned)||state(v.state_sleep)) {
        out.status="Replicated melee interrupted";return out;
    }
    if(!tracking.committed)tracking.committed=out.simulation_time;
    if(out.simulation_time-tracking.committed>.65f){out.status="Heavy-melee follow-through";return out;}
    if(!melee_template_.valid){out.status=melee_template_.status;return out;}
    float bonus{},pierce{};Vec3 angles{};
    if(!modifier_value(pawn,v.melee_range_index,bonus)||!modifier_value(pawn,v.parry_pierce_index,pierce)
        ||!read(pawn.address+f.eye_angles,angles)||!finite(angles)||std::abs(angles.x)>90||std::abs(angles.y)>3600||!player.dot_valid[0]) {
        out.status="Replicated melee parameters unavailable";return out;
    }
    out.state=2;out.attack_type=2;out.activated=tracking.started;out.state_start=tracking.committed;
    out.trigger_time=tracking.committed;out.using_melee=true;out.parryable=melee_template_.parryable[2];out.piercing=pierce>0;
    out.range=melee_template_.range+bonus;out.half_angle=melee_template_.angle;out.attack_duration=melee_template_.duration[2];
    constexpr float radians=.01745329252f;
    out.direction={std::cos(angles.x*radians)*std::cos(angles.y*radians),std::cos(angles.x*radians)*std::sin(angles.y*radians),-std::sin(angles.x*radians)};
    out.origin=sub(player.dots[0],{0,0,6});out.velocity=player.motion.velocity_valid?player.motion.velocity:player.velocity_valid?player.velocity:Vec3{};
    out.valid=out.range>=20&&out.range<=1500&&finite(out.velocity);
    out.status=out.piercing?"Parry piercing":out.valid?"Replicated heavy-melee dash / approximate contact":"Replicated melee geometry invalid";
    return out;
}
void GameReader::read_spell_threats(ParryReadState& out,const std::array<uintptr_t,64>& chunks) {
    out.unsupported_projectiles=unsupported_projectiles_;const auto& f=profile_.fields;
    for(const auto& object:spell_projectiles_) {
        if(entity(chunks,object.handle).address!=object.address)continue;
        auto name=type(object.address);
        if(name!=".?AVC_Citadel_Projectile_Archer_ChargedShot@@"&&name!=".?AVC_Citadel_Projectile_Bebop_Hook@@")continue;
        SpellThreatSample sample;sample.handle=object.handle;sample.name=name.find("Archer")!=std::string::npos?"Charged Shot":"Bebop Hook";
        uint8_t dormant{};uint32_t owner{};auto scene=pointer(object.address+f.scene_node);
        std::array<float,8> transform{};std::array<Vec3,2> bounds{};
        if(!scene||!read(scene+f.dormant,dormant)||dormant||!read(scene+f.node_world,transform)||!scene_point(transform,{},sample.position)
            ||!read(object.address+f.abs_velocity,sample.velocity)||!finite(sample.velocity)||dot(sample.velocity,sample.velocity)<2500
            ||dot(sample.velocity,sample.velocity)>50000.f*50000.f||!read(object.address+f.team,sample.team)
            ||!read(object.address+f.owner_handle,owner))continue;
        sample.owner=owner;
        for(int depth=0;depth<3;++depth) {
            auto parent=entity(chunks,owner);if(!parent.address)break;
            uint8_t team{};if(read(parent.address+f.team,team)&&(team==2||team==3)){sample.team=team;sample.owner=owner;break;}
            if(!read(parent.address+f.owner_handle,owner))break;
        }
        if(sample.team<2||sample.team>3)continue;
        const auto collision=pointer(object.address+f.collision);
        if(!collision||!read(collision+f.collision_min,bounds)||!finite(bounds[0])||!finite(bounds[1]))continue;
        Vec3 extent{std::max(std::abs(bounds[0].x),std::abs(bounds[1].x)),std::max(std::abs(bounds[0].y),std::abs(bounds[1].y)),std::max(std::abs(bounds[0].z),std::abs(bounds[1].z))};
        sample.radius=std::sqrt(dot(extent,extent))*transform[3];
        if(!std::isfinite(sample.radius)||sample.radius<0||sample.radius>96||entity(chunks,object.handle).address!=object.address
            ||pointer(object.address+f.scene_node)!=scene||pointer(object.address+f.collision)!=collision)continue;
        out.projectiles.push_back(std::move(sample));
    }
}
}
