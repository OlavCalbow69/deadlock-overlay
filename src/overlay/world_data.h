#pragma once
#include "focus_math.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace overlay {
enum class WorldKind { None, Lane, Jungle, Breakable, Souls, Pickup, Spawner, Camp };
enum class WorldAvailability { Alive, Waiting, Unknown };
inline unsigned world_category_bit(WorldKind kind) {
    switch(kind){case WorldKind::Lane:return 1;case WorldKind::Jungle:case WorldKind::Camp:return 2;case WorldKind::Breakable:return 4;case WorldKind::Souls:case WorldKind::Pickup:return 8;case WorldKind::Spawner:return 16;default:return 0;}
}
inline WorldKind world_kind(std::string_view type) {
    if(type==".?AVC_NPC_Trooper@@")return WorldKind::Lane;
    if(type==".?AVC_NPC_TrooperNeutral@@"||type==".?AVC_NPC_TrooperNeutralNodeMover@@")return WorldKind::Jungle;
    if(type==".?AVC_Citadel_BreakableProp@@"||type==".?AVC_Citadel_BreakablePropHideout@@")return WorldKind::Breakable;
    if(type==".?AVC_Citadel_PickupItemSpawner@@")return WorldKind::Spawner;
    if(type==".?AVC_Citadel_Pickup_Gold@@"||type==".?AVC_Citadel_Pickup_Currency@@")return WorldKind::Souls;
    if(type.starts_with(".?AVC_Citadel_Pickup_")&&type!=".?AVC_Citadel_Pickup_AssignedGold@@")return WorldKind::Pickup;
    return WorldKind::None;
}
inline WorldKind world_kind_name(std::string_view name) {
    if(name=="lane")return WorldKind::Lane;if(name=="jungle")return WorldKind::Jungle;
    if(name=="breakable")return WorldKind::Breakable;if(name=="souls")return WorldKind::Souls;
    if(name=="pickup")return WorldKind::Pickup;if(name=="spawner")return WorldKind::Spawner;return WorldKind::None;
}
struct HeroAsset {std::string portrait,minimap,name;};
struct UnitAsset {WorldKind kind{};std::string icon,token;float respawn{},practice_respawn{};};
struct GameAssetCatalog {
    std::unordered_map<int,HeroAsset> heroes;
    std::unordered_map<uint32_t,UnitAsset> units;
    std::unordered_map<std::string,std::string> maps;
    bool loaded{};
    static bool safe_asset_path(const std::string& value) {
        auto p=std::filesystem::path(value);return !value.empty()&&value.size()<256&&!p.is_absolute()&&!p.has_root_name()
            &&value.find(':')==std::string::npos&&value.find("..") == std::string::npos&&p.extension()==".png";
    }
    bool load(const std::filesystem::path& root) {
        heroes.clear();units.clear();maps.clear();loaded=false;std::ifstream in(root/"runtime.tsv");std::string line;unsigned count{};
        while(std::getline(in,line)&&++count<8192) {
            if(line.size()>2048||line.empty()||line[0]=='#')continue;
            if(line.back()=='\r')line.pop_back();std::vector<std::string> parts;std::istringstream row(line);std::string word;
            while(std::getline(row,word,'\t'))parts.push_back(word);
            try {
                if(parts[0]=="hero"&&parts.size()==5) {int id=std::stoi(parts[1]);if(id<=0||id>4096)continue;heroes[id]={safe_asset_path(parts[2])?parts[2]:"",safe_asset_path(parts[3])?parts[3]:"",parts[4]};}
                if(parts[0]=="unit"&&parts.size()==7) {auto id=std::stoull(parts[1]);float r=std::stof(parts[4]),t=std::stof(parts[5]);if(id>UINT32_MAX||!std::isfinite(r)||!std::isfinite(t))continue;units[uint32_t(id)]={world_kind_name(parts[2]),safe_asset_path(parts[3])?parts[3]:"",parts[6],r>0&&r<=3600?r:0,t>0&&t<=3600?t:0};}
                if(parts[0]=="map"&&parts.size()==3&&safe_asset_path(parts[2]))maps.try_emplace(parts[1],parts[2]);
            }catch(...){continue;}
        }
        loaded=!heroes.empty();return loaded;
    }
    const UnitAsset* unit(uint32_t id)const {auto it=units.find(id);return it==units.end()?nullptr:&it->second;}
};
struct WorldObject {
    uint32_t handle{},subclass{};WorldKind kind{};Vec3 position{};uint8_t team{};
    int health{},maximum{};WorldAvailability availability{WorldAvailability::Alive};
    float remaining{-1};bool authoritative{},midboss{};
};
struct MapLane {int number{};std::vector<Vec3> points,tangent_in,tangent_out;};
struct WorldState {
    Vec3 mins{},maxs{};bool bounds_valid{},clock_valid{},paused{},capable{},camp_timers_valid{};
    float game_time{},match_time{};uint64_t sampled{};
    std::vector<WorldObject> objects;std::vector<MapLane> lanes;
    std::string lane_source{"not_requested"};
    unsigned unclassified{},invalid{};std::string status{"World data not requested"};
};
inline bool minimap_bounds(Vec3 mins,Vec3 maxs) {
    return finite(mins)&&finite(maxs)&&maxs.x-mins.x>=256&&maxs.y-mins.y>=256
        &&maxs.x-mins.x<200000&&maxs.y-mins.y<200000;
}
// Source's overview: +X to the right, +Y toward the top. Bounds come from GameRules.
inline bool minimap_uv(Vec3 p,Vec3 mins,Vec3 maxs,ScreenPoint& uv) {
    if(!finite(p)||!minimap_bounds(mins,maxs))return false;
    uv={(p.x-mins.x)/(maxs.x-mins.x),1.f-(p.y-mins.y)/(maxs.y-mins.y)};
    return std::isfinite(uv.x)&&std::isfinite(uv.y);
}
inline float world_distance_meters(Vec3 a,Vec3 b) {auto d=sub(a,b);return std::sqrt(dot(d,d))*.0254f;}
// Static objects are retained only after a confirmed death. Disappearance or a
// failed read alone never starts a respawn clock. No slow cache of live positions.
class BreakableHistory {
    struct Key {uint32_t token;int x,y,z;bool operator==(const Key&)const=default;};
    struct Hash {size_t operator()(const Key& k)const {size_t h=k.token;for(int v:{k.x,k.y,k.z})h^=std::hash<int>{}(v)+0x9e3779b9+(h<<6)+(h>>2);return h;}};
    struct Record {WorldObject object;unsigned dead_samples{};bool saw_alive{};float deadline{-1};uint64_t seen{};};
    std::unordered_map<Key,Record,Hash> records_;uint64_t generation_{};std::string map_;uint32_t pid_{};float last_clock_{-1};
public:
    void reset(){records_.clear();generation_=0;last_clock_=-1;}
    void begin(uint32_t pid,const std::string& map,float clock) {
        if(pid!=pid_||map!=map_||(last_clock_>=0&&clock+1<last_clock_))reset();pid_=pid;map_=map;last_clock_=clock;++generation_;
    }
    WorldObject observe(WorldObject object,float clock,float respawn) {
        Key key{object.subclass,int(std::lround(object.position.x/4)),int(std::lround(object.position.y/4)),int(std::lround(object.position.z/4))};
        if(!records_.contains(key)&&records_.size()>=4096)return object;
        auto& record=records_[key];record.seen=generation_;record.object=object;
        if(object.availability==WorldAvailability::Alive){record.dead_samples=0;record.deadline=-1;record.saw_alive=true;}
        else if(object.availability==WorldAvailability::Waiting) {
            if(record.dead_samples<2)++record.dead_samples;
            if(record.saw_alive&&record.dead_samples==2&&record.deadline<0&&respawn>0&&clock>=0)record.deadline=clock+respawn;
            if(record.deadline>=clock&&clock>=0)object.remaining=record.deadline-clock;
            else if(record.deadline>=0)object.availability=WorldAvailability::Unknown;
        }
        record.object=object;return object;
    }
    void append_missing(std::vector<WorldObject>& out,float clock) {
        for(auto it=records_.begin();it!=records_.end();) {
            auto& r=it->second;
            if(r.seen==generation_){++it;continue;}
            if(generation_-r.seen>72000||(r.deadline>=0&&clock>r.deadline+30)){it=records_.erase(it);continue;}
            if(r.dead_samples>=2&&r.deadline>=clock&&clock>=0){auto object=r.object;object.remaining=r.deadline-clock;object.availability=WorldAvailability::Waiting;out.push_back(object);}
            ++it;
        }
    }
};
}
