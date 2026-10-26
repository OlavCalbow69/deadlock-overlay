#pragma once
#include "reader_profile_manifest.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace overlay {
struct ReaderProfile {
    struct Fields {
#define PROFILE_FIELD(key,value,cls,field) uint32_t key=value;
#define PROFILE_ADDRESS(key,value,module)
#define PROFILE_VALUE(key,value)
#include "reader_profile_values.inc"
#undef PROFILE_FIELD
#undef PROFILE_ADDRESS
#undef PROFILE_VALUE
    } fields;
    struct Addresses {
#define PROFILE_FIELD(key,value,cls,field)
#define PROFILE_ADDRESS(key,value,module) uint32_t key=value;
#define PROFILE_VALUE(key,value)
#include "reader_profile_values.inc"
#undef PROFILE_FIELD
#undef PROFILE_ADDRESS
#undef PROFILE_VALUE
    } addresses;
    struct Layout {
#define PROFILE_FIELD(key,value,cls,field)
#define PROFILE_ADDRESS(key,value,module)
#define PROFILE_VALUE(key,value) uint32_t key=value;
#include "reader_profile_values.inc"
#undef PROFILE_FIELD
#undef PROFILE_ADDRESS
#undef PROFILE_VALUE
    } layout;
    struct FeatureFields {
#define FEATURE_FIELD(key,cls,field,width) uint32_t key{};
#define FEATURE_PRIVATE(key) uint32_t key{};
#define FEATURE_SIZE(key,cls) uint32_t key{};
#define FEATURE_ENUM(key,name) uint32_t key{};
#define FEATURE_GLOBAL(key,name) uint32_t key{};
#include "reader_feature_fields.inc"
#undef FEATURE_FIELD
#undef FEATURE_PRIVATE
#undef FEATURE_SIZE
#undef FEATURE_ENUM
#undef FEATURE_GLOBAL
    } features;
    bool automatic{};
    std::string source{"built_in_6774"};
    static constexpr const char* client_hash="e2bafed8ebdfd239d506b4f59b24a09719f945c6a63f69766171349b886603d4";
    static constexpr const char* engine_hash="e5e4b2df76ff88d99dd9217e4ccde09bcce9b963327b38963f3ae90ce7e55ac2";
    static constexpr const char* world_hash="f2cd32e7336eeb00b1090cdca0c7327d4b245bb1a6ca3fe442081bb2dfb58ee1";
    bool load(const std::filesystem::path& file,const std::string& client,const std::string& engine,const std::string& world,
        uint32_t client_size,uint32_t engine_size,uint32_t world_size,std::string& error) {
        auto fail=[&](std::string why){error=std::move(why);return false;};
        std::error_code ec;auto size=std::filesystem::file_size(file,ec);
        if(ec)return fail("Reader profile missing: press Update all data");
        if(size>32768)return fail("Reader profile is too large");
        std::ifstream input(file);std::map<std::string,std::string> values;std::string line;
        while(std::getline(input,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.empty()||line[0]=='#')continue;
            auto at=line.find('=');if(at==std::string::npos||at==0||line.size()>1024||!values.emplace(line.substr(0,at),line.substr(at+1)).second)
                return fail("Malformed or duplicate reader profile entry");
        }
        if(!input.eof())return fail("Cannot read complete reader profile");
        auto equal=[&](const char* key,const std::string& expected){auto it=values.find(key);return it!=values.end()&&it->second==expected;};
        if(!equal("format","2")||!equal("layout","source2-reader-v2")||!equal("manifest",reader_manifest_id)||!equal("validation","passed"))
            return fail("Reader profile format or resolver version differs; rebuild data tools");
        if(!equal("client_hash",client)||!equal("engine_hash",engine)||!equal("world_hash",world))return fail("Reader profile belongs to a different DLL build: press Update all data");
        ReaderProfile next;
        auto number=[&](const char* key,uint32_t& out,uint32_t upper) {
            auto it=values.find(key);if(it==values.end())return false;
            const auto& value=it->second;
            if(value.empty()||value[0]=='-'||value[0]=='+'||value[0]==' ')return false;
            try {size_t used{};auto n=std::stoull(value,&used,0);if(used!=value.size()||n>=upper)return false;out=uint32_t(n);return true;}
            catch(...){return false;}
        };
        auto module_size=[&](std::string_view module){return module=="client"?client_size:module=="engine"?engine_size:module=="world"?world_size:0;};
#define PROFILE_FIELD(key,value,cls,field) if(!number("field." #key,next.fields.key,0x20000))return fail("Invalid schema field: " #key);
#define PROFILE_ADDRESS(key,value,module) if(!number("address." #key,next.addresses.key,module_size(module))||next.addresses.key<0x1000)return fail("Invalid address: " #key);
#define PROFILE_VALUE(key,value) if(!number("private." #key,next.layout.key,0x20000))return fail("Invalid private layout value: " #key);
#include "reader_profile_values.inc"
#undef PROFILE_FIELD
#undef PROFILE_ADDRESS
#undef PROFILE_VALUE
#define FEATURE_FIELD(key,cls,field,width) if(values.contains("feature." #key)&&!number("feature." #key,next.features.key,0x20000))return fail("Invalid feature field: " #key);
#define FEATURE_PRIVATE(key) FEATURE_FIELD(key,"","",4)
#define FEATURE_SIZE(key,cls) FEATURE_FIELD(key,"","",4)
#define FEATURE_ENUM(key,name) FEATURE_FIELD(key,"","",4)
#define FEATURE_GLOBAL(key,name) if(values.contains("feature." #key)&&!number("feature." #key,next.features.key,client_size))return fail("Invalid feature global: " #key);
#include "reader_feature_fields.inc"
#undef FEATURE_FIELD
#undef FEATURE_PRIVATE
#undef FEATURE_SIZE
#undef FEATURE_ENUM
#undef FEATURE_GLOBAL
        const auto& private_values=next.layout;
        if(!private_values.bullet_speed_index||private_values.bullet_speed_index>=1024||!private_values.bullet_override_index||private_values.bullet_override_index>=1024
            ||private_values.bullet_speed_index==private_values.bullet_override_index||private_values.modifier_cache>=0x10000||(private_values.modifier_cache&15)
            ||private_values.modifier_cache<0x214+2*(std::max(private_values.bullet_speed_index,private_values.bullet_override_index)+1))
            return fail("Invalid modifier cache layout");
        const auto& f=next.fields;
        for(auto state:{private_values.state_stunned,private_values.state_sleep,private_values.state_command,private_values.state_zipline,
            private_values.state_parry_disabled,private_values.state_urn,private_values.state_parry_active,private_values.state_force_parry,
            private_values.state_ground_dash,private_values.state_air_dash,private_values.state_shooting_disabled,private_values.state_melee_disabled,
            private_values.state_melee_debuff,private_values.state_parry_ignores_melee,private_values.state_charge_melee,
            private_values.state_input_disabled,private_values.state_jump_disabled,private_values.state_aim_forward_pitch})if(state>=352)return fail("Modifier state exceeds supported mask");
        if(private_values.melee_range_index>=1024||private_values.parry_pierce_index>=1024||private_values.melee_attack_stride<0x100
            ||private_values.melee_attack_stride>0x2000||private_values.melee_attack_duration+4>=private_values.melee_attack_stride-24
            ||private_values.ability_property_stride<0x80||private_values.ability_property_stride>=0x800||private_values.ability_property_stride%8
            ||private_values.property_base_value+4>=private_values.ability_property_stride-24
            ||private_values.property_cached_value+4>private_values.property_dirty||private_values.counterspell_armed>=0x400)
            return fail("Invalid parry reader layout");
        auto span=[](std::initializer_list<uint32_t> offsets){auto [low,high]=std::minmax_element(offsets.begin(),offsets.end());return *high-*low<4090;};
        if(!span({f.scene_node,f.health,f.max_health,f.life_state,f.team})||!span({f.pawn_handle,f.local_controller})
            ||!span({f.hero_spawned+f.spawn_id,f.hero_loading+f.spawn_id,f.hero_unspawned})
            ||std::max({f.hero_spawned+f.spawn_id,f.hero_loading+f.spawn_id,f.hero_unspawned})>4092
            ||f.collision_max!=f.collision_min+12||f.game_mode!=f.match_mode+4||f.mouse_sensitivity!=f.fov_sensitivity+4)
            return fail("Schema fields do not fit the supported reader layout");
        next.automatic=true;next.source="generated:"+client.substr(0,12);*this=std::move(next);return true;
    }
};
}
