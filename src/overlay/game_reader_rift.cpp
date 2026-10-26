#include "game_reader.h"
#include <cstring>

namespace overlay {
void GameReader::read_rift(Snapshot& out,const std::array<uintptr_t,64>& chunks,uintptr_t rules,uint64_t now) {
    if(now<next_rift_){out.rift=rift_cache_;return;}next_rift_=now+100;
    auto fail=[&](const char* message){rift_history_.reset();out.rift={};out.rift.sampled=now;out.rift.status=message;rift_cache_=out.rift;};
    const auto& f=profile_.features;
    if(!f.rift_started||!f.rift_giveup||!f.rift_position||!f.rift_fow||!f.rift_fow_stride||!f.rift_spawner_class){fail("Rift fields unavailable: Update all data");return;}
    float started{},giveup{},clock{};uint8_t paused{};Vec3 position{};
    auto globals=pointer(base_+profile_.addresses.global_vars);
    if(!rules||pointer(rules)!=base_+profile_.addresses.rules_vtable||!globals||!read(globals+48,clock)||!std::isfinite(clock)||clock<0||clock>10000000
        ||!read(rules+f.rift_started,started)||!read(rules+f.rift_giveup,giveup)||!read(rules+f.rift_position,position)||!finite(position)
        ||!std::isfinite(started)||started<0||started>clock+1||!std::isfinite(giveup)||!read(rules+profile_.fields.server_paused,paused)||paused>1){fail("Rift clock unavailable");return;}
    const auto stride=f.rift_fow_stride;
    if(stride<64||stride>512||f.rift_fow_class+4>stride||f.rift_fow_entity+4>stride||f.rift_fow_lane+4>stride||f.rift_fow_visible+1>stride){fail("Rift marker layout unavailable");return;}
    uint32_t incoming{};int lane{};Vec3 warning_position{};bool saw_team=false,read_team=false;
    for(const auto& cached:rift_teams_) {
        auto team=entity(chunks,cached.handle);uint8_t number{};
        if(team.address!=cached.address||!read(team.address+profile_.fields.team,number)||number!=out.local_team)continue;saw_team=true;
        std::array<unsigned char,24> header{},after{};uint32_t count{};uintptr_t data{};
        if(!read(team.address+f.rift_fow,header))continue;
        std::memcpy(&count,header.data(),4);std::memcpy(&data,header.data()+8,8);
        if(count>2048||count&&(data<65536||data>0x7fffffffffffULL))continue;
        std::vector<unsigned char> entries(size_t(count)*stride);
        if(count&&!read(data,entries.data(),entries.size()))continue;
        if(!read(team.address+f.rift_fow,after)||after!=header||entity(chunks,cached.handle).address!=team.address)continue;read_team=true;
        for(uint32_t i=0;i<count;++i) {
            auto record=entries.data()+size_t(i)*stride;uint32_t cls{};int index{},candidate_lane{};
            std::memcpy(&cls,record+f.rift_fow_class,4);if(cls!=f.rift_spawner_class||record[f.rift_fow_visible]!=1)continue;
            std::memcpy(&index,record+f.rift_fow_entity,4);std::memcpy(&candidate_lane,record+f.rift_fow_lane,4);
            if(index<0||index>0x7ffe||!chunks[uint32_t(index)>>9])continue;
            std::array<unsigned char,24> identity{};if(!read(chunks[uint32_t(index)>>9]+(uint32_t(index)&511)*112,identity))continue;
            uintptr_t address{};uint32_t handle{};std::memcpy(&address,identity.data(),8);std::memcpy(&handle,identity.data()+16,4);
            if(!address||entity(chunks,handle).address!=address)continue;
            // CLASS_KOTH_SPAWNER comes from the replicated map marker. The
            // runtime warning uses the shared pickup client class; the static
            // CInfoKOTHSpawnLocation is only a possible map location.
            const auto name=type(address);if(name!=".?AVC_CitadelItemPickup@@"&&name!=".?AVCCitadelItemKothSpawner@@"&&name!=".?AVCInfoKOTHSpawnLocation@@")continue;
            auto scene=pointer(address+profile_.fields.scene_node);Vec3 origin{};
            if(!scene||!read(scene+profile_.fields.node_world,origin)||!finite(origin)||std::abs(origin.x)>200000||std::abs(origin.y)>200000)continue;
            if(incoming&&incoming!=handle){fail("Multiple rift warnings; timing ambiguous");return;}
            incoming=handle;lane=candidate_lane;warning_position=origin;
        }
    }
    if(saw_team&&!read_team){fail("Rift marker update pending");return;}
    auto convar_seconds=[&](uint32_t slot,const char* name,float& value){
        auto variable=slot?pointer(base_+slot):0;
        return variable&&text(pointer(variable),80)==name&&read(variable+88,value)&&std::isfinite(value)&&value>=0&&value<=600;
    };
    // Server spawner transitions through early warning, then opening warning,
    // before creating the capture point. Neither duration is a spawn interval.
    float early{},opening{},warning_seconds{};
    if(convar_seconds(f.rift_warning_cvar,"citadel_koth_early_warning_time",early)
        &&convar_seconds(f.rift_opening_cvar,"citadel_koth_warning_time",opening)&&early+opening<=600)warning_seconds=early+opening;
    if(pointer(base_+profile_.addresses.game_rules)!=rules||pointer(base_+profile_.addresses.global_vars)!=globals){fail("Rift map changed");return;}
    out.rift=rift_history_.update(now,clock,paused!=0,started>0,giveup,started>0?position:warning_position,incoming,lane,warning_seconds);
    // A current event location can still identify its lane after the warning is
    // gone, including when Vanta started in the middle of a rift.
    if((out.rift.phase==RiftPhase::Active||out.rift.warning_handle)&&!rift_lane_valid(out.rift.lane)) {
        position=out.rift.position;
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto& map_paths=map_lanes_.get(std::filesystem::path(executable).parent_path()/L"data"/L"map-lanes",out.map_name,now);
        float best=FLT_MAX;int nearest{};
        auto consider=[&](int number,const std::vector<Vec3>& points){
            for(size_t i=1;i<points.size();++i){auto a=points[i-1],b=points[i];if(!finite(a)||!finite(b))continue;a.z=b.z=position.z;auto ab=sub(b,a);float length=dot(ab,ab);auto q=add(a,mul(ab,length>0?std::clamp(dot(sub(position,a),ab)/length,0.f,1.f):0));auto d=sub(position,q);float distance=dot(d,d);if(distance<best){best=distance;nearest=number;}}
        };
        for(const auto& path:map_paths)consider(path.number,path.points);
        if(f.lane_points&&f.lane_number)for(const auto& cached:lane_entities_){auto object=entity(chunks,cached.handle);if(object.address!=cached.address)continue;
            int number{};std::array<unsigned char,24> vector{};if(!read(object.address+f.lane_number,number)||!rift_lane_valid(number)||!read(object.address+f.lane_points,vector))continue;
            uint32_t count{};uintptr_t data{};std::memcpy(&count,vector.data(),4);std::memcpy(&data,vector.data()+8,8);if(count<2||count>256||!data)continue;
            std::vector<Vec3> points(count);std::array<unsigned char,24> after{};
            if(!read(data,points.data(),count*sizeof(Vec3))||!read(object.address+f.lane_points,after)||after!=vector||entity(chunks,cached.handle).address!=object.address)continue;
            consider(number,points);
        }
        if(best<1600.f*1600.f){out.rift.lane=nearest;rift_history_.lane=nearest;}
    }
    rift_cache_=out.rift;
}
}
