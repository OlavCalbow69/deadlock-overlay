#include "game_reader.h"
#include <cstring>

namespace overlay {
void GameReader::read_world(Snapshot& out,const std::array<uintptr_t,64>& chunks,uintptr_t rules,uint64_t now) {
    auto& state=out.world;const auto& f=profile_.features;state.sampled=now;
    if(!rules||pointer(rules)!=base_+profile_.addresses.rules_vtable){state.status="Waiting for map rules";return;}
    if(now>=next_world_catalog_) {
        next_world_catalog_=now+5000;wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
        auto root=std::filesystem::path(path).parent_path()/L"data"/L"icons";
        std::error_code ec;auto stamp=std::filesystem::last_write_time(root/L"runtime.tsv",ec);
        if(!ec&&(!world_catalog_attempted_||stamp!=world_catalog_stamp_)){world_catalog_attempted_=true;world_catalog_stamp_=stamp;world_catalog_.load(root);}
    }
    state.capable=f.subclass!=0;
    if(f.minimap_mins&&f.minimap_maxs&&read(rules+f.minimap_mins,state.mins)&&read(rules+f.minimap_maxs,state.maxs))state.bounds_valid=minimap_bounds(state.mins,state.maxs);
    auto globals=pointer(base_+profile_.addresses.global_vars);uint8_t paused{};float current{},interval{},anchor{};int tick{},anchor_tick{};
    if(globals&&read(globals+0x30,current)&&std::isfinite(current)&&current>=0&&current<10000000
        &&read(rules+profile_.fields.server_paused,paused)&&paused<=1
        &&read(rules+profile_.fields.match_clock_tick,anchor_tick)&&read(rules+profile_.fields.match_clock_time,anchor)
        &&read(globals+68,tick)&&read(globals+84,interval)
        &&midboss_clock(anchor,anchor_tick,tick,interval,paused!=0,state.match_time)
        &&pointer(base_+profile_.addresses.game_rules)==rules&&pointer(base_+profile_.addresses.global_vars)==globals) {
        state.game_time=current;state.clock_valid=true;state.paused=paused!=0;
    }
    auto vector_read=[&]<class T>(uintptr_t address,std::vector<T>& result,size_t limit)->bool {
        std::array<unsigned char,24> before{},after{};uint32_t count{};uintptr_t data{};
        if(!address||!read(address,before))return false;std::memcpy(&count,before.data(),4);std::memcpy(&data,before.data()+8,8);
        if(count>limit||(count&&(data<0x10000||data>0x7fffffffffffULL)))return false;
        result.resize(count);if(count&&!read(data,result.data(),count*sizeof(T)))return false;
        return read(address,after)&&after==before;
    };
    if(options_.world&&f.subclass) {
        breakables_.begin(out.pid,out.map_name,state.clock_valid?state.game_time:-1);
        state.objects.reserve(world_entities_.size()+64);
        for(const auto& cached:world_entities_) {
            auto object=entity(chunks,cached.handle);if(object.address!=cached.address)continue;
            WorldObject item;item.handle=object.handle;item.kind=world_kind(type(object.address));
            FocusTarget base;uintptr_t scene{};uint8_t life{},dormant{};std::array<float,8> transform{};
            if(item.kind==WorldKind::None||!read(object.address+f.subclass,item.subclass)||!pawn_fields(object.address,base,scene,life)
                ||!scene||!read(scene+profile_.fields.dormant,dormant)||(dormant&&item.kind!=WorldKind::Breakable)||!read(scene+profile_.fields.node_world,transform)
                ||!scene_point(transform,{},item.position)||std::abs(item.position.x)>200000||std::abs(item.position.y)>200000||std::abs(item.position.z)>200000) {++state.invalid;continue;}
            item.team=base.team;item.health=base.health;item.maximum=base.maximum;
            auto definition=world_catalog_.unit(item.subclass);
            if(item.kind==WorldKind::Lane||item.kind==WorldKind::Jungle) {
                if(life||item.health<=0||item.maximum<=0)continue;
            }else if(item.kind==WorldKind::Spawner) {
                uint8_t active{};float next{};
                if(!f.spawner_active||!f.spawner_next||!read(object.address+f.spawner_active,active)||active>1
                    ||!read(object.address+f.spawner_next,next)||!std::isfinite(next)){++state.invalid;continue;}
                item.availability=active?WorldAvailability::Alive:WorldAvailability::Waiting;
                if(!active&&state.clock_valid&&next>state.game_time&&next-state.game_time<=3600){item.remaining=next-state.game_time;item.authoritative=true;}
            }else if(item.kind==WorldKind::Souls||item.kind==WorldKind::Pickup) {
                uint8_t active{},finished{};uint32_t claimer{};
                if(!f.pickup_active||!f.pickup_finished||!f.pickup_claimer||!read(object.address+f.pickup_active,active)||active!=1
                    ||!read(object.address+f.pickup_finished,finished)||finished!=0||!read(object.address+f.pickup_claimer,claimer))continue;
                // Moving/vacuumed pickups are already assigned, not ground loot.
                if(claimer&&claimer!=UINT32_MAX)continue;
            }else if(item.kind==WorldKind::Breakable) {
                if(item.health<-1000000||item.health>1000000||item.maximum<0||item.maximum>1000000||life>2){++state.invalid;continue;}
                item.availability=(life||item.health<=0)?WorldAvailability::Waiting:WorldAvailability::Alive;
            }
            // Reject recycled identities and scene changes before publishing.
            if(entity(chunks,object.handle).address!=object.address||pointer(object.address+profile_.fields.scene_node)!=scene){++state.invalid;continue;}
            if(!definition)++state.unclassified;
            if(item.kind==WorldKind::Breakable) {
                float respawn=definition?(out.practice&&definition->practice_respawn>0?definition->practice_respawn:definition->respawn):0;
                item=breakables_.observe(item,state.clock_valid?state.game_time:-1,respawn);
            }
            state.objects.push_back(item);
        }
        breakables_.append_missing(state.objects,state.clock_valid?state.game_time:-1);
        if((options_.world_mask&2)&&f.camp_origins&&f.camp_next&&f.camp_midboss&&state.clock_valid) {
            std::vector<Vec3> positions;std::vector<float> next;std::vector<uint8_t> bosses;
            if(vector_read(rules+f.camp_origins,positions,256)&&vector_read(rules+f.camp_next,next,256)
                &&vector_read(rules+f.camp_midboss,bosses,256)&&positions.size()==next.size()&&next.size()==bosses.size()) {
                state.camp_timers_valid=true;
                for(size_t i=0;i<positions.size();++i) {
                    if(!finite(positions[i])||!std::isfinite(next[i])||bosses[i]>1||next[i]<=state.match_time||next[i]-state.match_time>3600)continue;
                    WorldObject camp;camp.kind=WorldKind::Camp;camp.position=positions[i];camp.availability=WorldAvailability::Waiting;
                    camp.remaining=next[i]-state.match_time;camp.authoritative=true;camp.midboss=bosses[i]!=0;state.objects.push_back(camp);
                }
            }
        }
    }
    if(options_.minimap&&options_.minimap_lanes) {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        state.lanes=map_lanes_.get(std::filesystem::path(executable).parent_path()/L"data"/L"map-lanes",out.map_name,now);
        state.lane_source=state.lanes.empty()?"unavailable":"installed_map";
    }
    if(options_.minimap&&options_.minimap_lanes&&state.lanes.empty()&&f.lane_number&&f.lane_points)for(const auto& cached:lane_entities_) {
        auto object=entity(chunks,cached.handle);if(object.address!=cached.address)continue;MapLane lane;
        if(!read(object.address+f.lane_number,lane.number)||!map_lane_number(lane.number)||!vector_read(object.address+f.lane_points,lane.points,256)||lane.points.size()<2)continue;
        if(std::any_of(lane.points.begin(),lane.points.end(),[](Vec3 p){return !finite(p)||std::abs(p.x)>200000||std::abs(p.y)>200000||std::abs(p.z)>200000;}))continue;
        if(f.lane_in)vector_read(object.address+f.lane_in,lane.tangent_in,256);
        if(f.lane_out)vector_read(object.address+f.lane_out,lane.tangent_out,256);
        if(entity(chunks,object.handle).address==object.address){state.lanes.push_back(std::move(lane));state.lane_source="replicated";}
    }
    state.status=!state.capable?"World fields unavailable: Update all data":!world_catalog_.loaded?"Game icons unavailable":!state.bounds_valid&&options_.minimap?"Minimap bounds unavailable":!state.clock_valid?"Live objects ready; timers unavailable":"Live world data ready";
}
}
