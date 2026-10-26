#pragma once
#include "game_reader.h"
#include <ostream>
#include <iomanip>
namespace overlay {
inline void external_json_string(std::ostream& out,std::string_view text) {
    out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";else if(c=='\t')out<<"\\t";else if(c>=32)out<<char(c);}out<<'"';
}
inline void write_rift_data(std::ostream& out,const RiftState& rift) {
    out<<"{\"valid\":"<<(rift.valid?"true":"false")<<",\"phase\":\""<<rift_phase_name(rift.phase)<<"\",\"timer_valid\":"<<(rift.timer_valid?"true":"false")<<",\"estimated\":"<<(rift.estimated?"true":"false")<<",\"remaining\":"<<rift.remaining<<",\"deadline\":"<<rift.deadline<<",\"game_time\":"<<rift.game_time<<",\"warning_seconds\":"<<rift.warning_seconds<<",\"warning_handle\":"<<rift.warning_handle<<",\"lane\":"<<rift.lane<<",\"lane_name\":";
    external_json_string(out,rift_lane_name(rift.lane));out<<",\"status\":";external_json_string(out,rift.status);out<<",\"paused\":"<<(rift.paused?"true":"false")<<"}\n";
}
inline void write_external_data(std::ostream& out,const Snapshot& s) {
    out<<"{\"read_only\":true,\"pid\":"<<s.pid<<",\"status\":";external_json_string(out,s.status);
    out<<",\"profile\":";external_json_string(out,s.reader_profile);out<<",\"sample_us\":"<<s.sample_us<<",\"read_calls\":"<<s.read_calls;
    out<<",\"hero\":";external_json_string(out,s.hero.name);out<<",\"local_geometry\":"<<(s.local_player_valid?"true":"false");
    out<<",\"observers_valid\":"<<(s.observers_valid?"true":"false")<<",\"spectators\":[";bool first=true;
    for(auto& p:s.spectators){if(!first)out<<',';first=false;external_json_string(out,p);}out<<"],\"players\":[";first=true;
    for(auto& p:s.players){if(!first)out<<',';first=false;out<<"{\"handle\":"<<p.handle<<",\"nickname\":";external_json_string(out,p.nickname);out<<",\"hero\":";external_json_string(out,p.hero_name);out<<",\"model\":";external_json_string(out,p.model_name);
        out<<",\"hero_id\":"<<p.hero_id<<",\"position\":["<<p.origin.x<<','<<p.origin.y<<','<<p.origin.z<<"],\"rig\":";external_json_string(out,p.bone_rig);out<<",\"level\":"<<p.level<<",\"joints_mask\":"<<p.joints_valid<<",\"origin_valid\":"<<(p.origin_valid?"true":"false")<<",\"collision_bounds\":"<<(p.activation_bounds?"true":"false")<<",\"hitboxes\":"<<p.hitboxes.size()<<"}";}
    const auto& h=s.helpers;out<<"],\"helpers\":{\"time_valid\":"<<(h.time_valid?"true":"false")<<",\"game_time\":"<<h.game_time;
    out<<",\"sniper_valid\":"<<(h.sniper_valid?"true":"false")<<",\"sniper_ready\":"<<(h.sniper_ready?"true":"false")<<",\"can_scope\":"<<(h.sniper_can_scope?"true":"false")<<",\"charges\":"<<h.charges<<",\"scope_started\":"<<h.sniper_started<<",\"minimum_scope\":"<<h.sniper_minimum<<",\"sniper_status\":";external_json_string(out,h.sniper_status);
    out<<"},\"puddle_capable\":"<<(s.puddle_capable?"true":"false")<<",\"puddles\":"<<s.puddles.size();
    const auto& w=s.world;out<<",\"world\":{\"status\":";external_json_string(out,w.status);
    out<<",\"lane_source\":";external_json_string(out,w.lane_source);out<<",\"lane_numbers\":[";bool first_lane=true;
    for(const auto& lane:w.lanes){if(!first_lane)out<<',';first_lane=false;out<<lane.number;}out<<']';
    out<<",\"map\":";external_json_string(out,s.map_name);out<<",\"bounds_valid\":"<<(w.bounds_valid?"true":"false")<<",\"mins\":["<<w.mins.x<<','<<w.mins.y<<','<<w.mins.z<<"],\"maxs\":["<<w.maxs.x<<','<<w.maxs.y<<','<<w.maxs.z<<"],\"clock_valid\":"<<(w.clock_valid?"true":"false")<<",\"game_time\":"<<w.game_time<<",\"match_time\":"<<w.match_time<<",\"camp_timers_valid\":"<<(w.camp_timers_valid?"true":"false")<<",\"lanes\":"<<w.lanes.size()<<",\"unclassified\":"<<w.unclassified<<",\"invalid\":"<<w.invalid<<",\"objects\":[";first=true;
    for(const auto& o:w.objects){if(!first)out<<',';first=false;out<<"{\"handle\":"<<o.handle<<",\"subclass\":"<<o.subclass<<",\"kind\":"<<int(o.kind)<<",\"team\":"<<unsigned(o.team)<<",\"health\":"<<o.health<<",\"maximum\":"<<o.maximum<<",\"position\":["<<o.position.x<<','<<o.position.y<<','<<o.position.z<<"],\"availability\":"<<int(o.availability)<<",\"remaining\":"<<o.remaining<<",\"authoritative\":"<<(o.authoritative?"true":"false")<<"}";}
    out<<"]}}";
}
}
