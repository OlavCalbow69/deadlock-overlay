#include "game_reader.h"
#include <cstring>
#include <array>

namespace overlay {
namespace {
void omitted(std::string& text,const char* name){if(!text.empty())text+=", ";text+=name;}
}
bool GameReader::assassinate_modifier(const Entity& pawn,uint32_t index,float& value,bool& estimated,bool normalized) const {
    value=normalized?1.f:0.f;
    if(!index||index>=1024)return false;
    const auto prop=pointer(pawn.address+profile_.fields.modifier_property);uint8_t group{},again_group{};uint32_t version{},after{};
    if(!prop||!read(prop+0x210,version)||!version||!read(prop+160+index,group))return false;
    if(group!=255) {
        uint8_t policy{},fallback{};uint16_t dirty{},after_dirty{};std::array<uint32_t,12> cached{},again{};
        if(!read(base_+profile_.addresses.modifier_policy+index,policy)||!read(base_+profile_.addresses.modifier_fallback,fallback)
            ||!read(prop+0x214+2*index,dirty)||dirty||!read(prop+profile_.layout.modifier_cache+48*index,cached))return false;
        if(!policy)policy=fallback;
        if(policy<1||policy>4||(cached[0]&1)||cached[2]!=version||!cached[1]||(cached[4]!=0&&cached[4]!=2))return false;
        if(policy==2||policy==4) {
            auto globals=pointer(base_+profile_.addresses.global_vars);uint32_t tick{};
            if(!globals||!read(globals+68,tick)||!assassinate_cache_tick_usable(tick,cached[3]))return false;
            if(tick!=cached[3])estimated=true;
        }
        float number{};std::memcpy(&number,&cached[8],4);
        if(!std::isfinite(number)||!read(prop+profile_.layout.modifier_cache+48*index,again)||again!=cached
            ||!read(prop+0x214+2*index,after_dirty)||after_dirty)return false;
        // Context-dependent client caches are useful estimates, not a promise
        // that every ability- or attacker-specific modifier was evaluated here.
        estimated|=cached[1]!=1664691683u||policy<3;
        if(cached[4])value=number;
    }
    return std::isfinite(value)&&read(prop+0x210,after)&&version==after
        &&read(prop+160+index,again_group)&&group==again_group&&pointer(pawn.address+profile_.fields.modifier_property)==prop;
}
bool GameReader::assassinate_starting_stat(int hero,uint32_t stat,float& value) const {
    value=0;const auto it=heroes_.find(hero);
    if(it==heroes_.end()||!profile_.features.as_starting_stats)return false;
    const auto at=it->second.address+profile_.features.as_starting_stats;
    std::array<unsigned char,40> map{},again{};if(!read(at,map))return false;
    uint32_t capacity{},count{};uintptr_t entries{};int index{};
    std::memcpy(&capacity,map.data()+12,4);capacity&=0x7fffffff;std::memcpy(&entries,map.data()+16,8);
    std::memcpy(&index,map.data()+24,4);std::memcpy(&count,map.data()+28,4);
    if(capacity>256||count>capacity||(!entries&&count))return false;
    for(uint32_t step=0;index>=0&&step<capacity;++step) {
        if(uint32_t(index)>=capacity)return false;
        std::array<unsigned char,24> node{};if(!read(entries+24*size_t(index),node))return false;uint32_t key{};
        std::memcpy(&key,node.data()+16,4);
        if(key==stat){std::memcpy(&value,node.data()+20,4);return std::isfinite(value)&&read(at,again)&&again==map;}
        std::memcpy(&index,node.data()+(stat<key?0:4),4);
    }
    return index==-1&&read(at,again)&&again==map;
}
bool GameReader::assassinate_spirit(const Snapshot& s,const std::array<uintptr_t,64>& chunks,const Entity& pawn,float& value,bool& estimated) {
    const auto& f=profile_.features;const auto& v=profile_.layout;
    if(!f.as_modifier_layout||!f.as_upgrade_spirit||!f.as_upgrade_owner||!f.as_modifier_destroy||!f.level)return false;
    const auto hero=heroes_.find(s.hero.id);if(hero==heroes_.end())return false;
    const auto prop=pointer(pawn.address+profile_.fields.modifier_property);
    uint32_t version{},after{};uint8_t group{},again_group{};float base{},factor{1};
    if(!prop||!assassinate_starting_stat(s.hero.id,59,base)||!read(prop+0x210,version)||!version
        ||!read(prop+160+f.as_spirit,group)||!assassinate_modifier(pawn,f.as_spirit_percent,factor,estimated,true)||factor<0||factor>20)return false;
    float sum{};
    if(group!=255) {
        auto groups=pointer(prop+144);const auto at=groups+24*size_t(group);
        std::array<unsigned char,24> header{},again{};int count{};uintptr_t entries{};
        if(!groups||!read(at,header))return false;
        std::memcpy(&count,header.data(),4);std::memcpy(&entries,header.data()+8,8);
        if(count<0||count>64||(count&&!entries))return false;
        std::array<uintptr_t,128> records{},records_after{};
        if(count&&!read(entries,records.data(),16*size_t(count)))return false;
        for(int i=0;i<count;++i) {
            const auto modifier=records[2*size_t(i)],callback=records[2*size_t(i)+1];
            uint8_t destroyed{},disabled{};uint32_t serial{},serial_after{};float contribution{};
            if(!modifier||!callback||!read(modifier+f.as_modifier_destroy,destroyed)
                ||!read(modifier+profile_.fields.modifier_disabled,disabled)||disabled>1
                ||!read(modifier+profile_.fields.modifier_serial,serial))return false;
            if(destroyed||disabled)continue;
            if(type(modifier)==".?AVCCitadel_Modifier_HeroUpgradeBonuses@@") {
                const auto owner=pointer(modifier+f.as_upgrade_owner);
                // This modifier's native getter falls back to the local pawn
                // while its optional owner pointer is unset on the client.
                if((owner&&owner!=pawn.address)||!read(modifier+f.as_upgrade_spirit,contribution)||!std::isfinite(contribution))return false;
                // The optional level/getter witnesses pin this compiled vector:
                // one entry per hero level, containing (float, EModifierValue).
                const auto levels=hero->second.address+0xF28;int available{},level{};uintptr_t data{};
                if(!read(pawn.address+f.level,level)||level<0||level>100||!read(levels,available)||available<0||available>100
                    ||!read(levels+8,data)||(available&&!data))return false;
                for(int n=0;n<std::min(level,available);++n) {
                    const auto entry=pointer(data+8*size_t(n));if(!entry)continue;int bonuses{};uintptr_t bonuses_data{};
                    if(!read(entry+48,bonuses)||bonuses<0||bonuses>128||!read(entry+56,bonuses_data)||(bonuses&&!bonuses_data))return false;
                    for(int j=0;j<bonuses;++j) {uint16_t stat{};float amount{};
                        if(!read(bonuses_data+8*size_t(j)+4,stat)||!read(bonuses_data+8*size_t(j),amount)||!std::isfinite(amount))return false;
                        if(stat==f.as_spirit)contribution+=amount;
                    }
                }
            }else {
                // Recognize the reviewed native intrinsic-property getter family;
                // never execute game code. Full bodies are pinned by the updater.
                constexpr unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,
                    0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x81,0xa0,0,0,0,0x45,0x33,0xe4,0x0f,0x29,0x74,0x24,0x30,0x4d,0x8b,0xf8,0x48,0x8b,0xfa,0x48,0x8b,0xe9,0x48,0x85,0xc0};
                std::array<unsigned char,66> code{};
                if(!read(callback,code)||!std::equal(std::begin(prefix),std::end(prefix),code.begin()))return false;
                const int slot=code[63]==0x44&&code[64]==0x39&&code[65]==0x20?0:code[63]==0x83&&code[64]==0x38?code[65]:-1;
                if(slot<0||slot>10)return false;
                const auto descriptor=pointer(modifier+160);int slots{};
                if(!descriptor||!read(descriptor,slots)||slots<=slot||slots>128)return false;
                const auto slot_at=pointer(descriptor+8)+24*size_t(slot);int properties{};uintptr_t properties_data{};
                if(!read(slot_at,properties)||properties!=1||!read(slot_at+8,properties_data)||!properties_data)return false;
                const auto view=pointer(properties_data),definition=pointer(view+v.property_definition);uint32_t condition{};
                if(!view||!definition||!read(definition+92,condition)||(condition!=0&&condition!=3)||pointer(definition+72))return false;
                // Condition 3 is "has ability charges"; Assassinate is a charged
                // ability even while unscoped. Other conditional forms stay gated.
                uint32_t owner_handle{},upgrade{};uint16_t required{};int upgrades{};uintptr_t upgrade_data{};
                if(!read(modifier+profile_.fields.modifier_ability,owner_handle))return false;
                const auto owner=entity(chunks,owner_handle);
                if(!owner.address||!read(owner.address+f.as_upgrade_info,upgrade)||!read(definition+106,required)
                    ||!read(definition+v.property_base_value,contribution)||!std::isfinite(contribution)
                    ||!read(definition+168,upgrades)||upgrades<0||upgrades>16||!read(definition+176,upgrade_data)||(upgrades&&!upgrade_data))return false;
                const uint16_t bits=uint16_t(upgrade>>16);
                if((bits&required)!=required)contribution=0;
                else for(int j=0;j<upgrades;++j) {
                    const auto change=pointer(upgrade_data+8*size_t(j)),level=pointer(change+48);uint16_t mask{};int operation{};float amount{};
                    if(!change||!level||!read(level+32,mask)||!read(change+32,operation)||operation<0||operation>1||!read(change+28,amount)||!std::isfinite(amount))return false;
                    if((bits&mask)==mask){if(operation==0)contribution+=amount;else contribution*=1+amount/100;}
                }
                uint32_t upgrade_after{};
                if(pointer(view+v.property_definition)!=definition||pointer(modifier+160)!=descriptor||pointer(properties_data)!=view
                    ||!read(owner.address+f.as_upgrade_info,upgrade_after)||upgrade_after!=upgrade||entity(chunks,owner_handle).address!=owner.address)return false;
            }
            uint8_t disabled_after{},destroyed_after{};
            if(!std::isfinite(contribution)||std::abs(contribution)>10000||!read(modifier+profile_.fields.modifier_serial,serial_after)||serial_after!=serial
                ||!read(modifier+profile_.fields.modifier_disabled,disabled_after)||disabled_after!=disabled
                ||!read(modifier+f.as_modifier_destroy,destroyed_after)||destroyed_after!=destroyed)return false;
            sum+=contribution;
        }
        if(!read(at,again)||again!=header||(count&&(!read(entries,records_after.data(),16*size_t(count))||records_after!=records)))return false;
    }
    value=(base+sum)*factor;
    return std::isfinite(value)&&value>=-1000&&value<=10000&&read(prop+0x210,after)&&version==after
        &&read(prop+160+f.as_spirit,again_group)&&group==again_group&&pointer(pawn.address+profile_.fields.modifier_property)==prop;
}
void GameReader::read_assassinate(Snapshot& out,const std::array<uintptr_t,64>& chunks,const Entity& pawn) {
    auto& a=out.assassinate;const auto& f=profile_.features;const auto& v=profile_.layout;
    a.sampled=GetTickCount64();
    a.status="Waiting for a stable Assassinate sample";
    if(!vindicta(out.hero)){a.status="Vindicta required";return;}
    if(!out.helpers.sniper_valid||!f.assassinate_layout||!f.as_upgrade_info||!f.as_cast_completed||!f.as_interrupt_states
        ||!f.as_scale_stat||!f.as_scale_amount||!f.as_scale_disabled){a.status="Update data / Assassinate damage capability unavailable";return;}
    auto ability=entity(chunks,out.hero.sniper_handle);const auto data=pointer(ability.address+v.ability_vdata);
    uint32_t packed{},packed_after{};
    if(!ability.address||!data||!read(ability.address+f.as_upgrade_info,packed)||!read(ability.address+f.as_cast_completed,a.cast_completed)
        ||!std::isfinite(a.cast_completed)){a.status="Waiting for stable Assassinate data";return;}
    a.ability=ability.handle;const uint16_t upgrade=uint16_t(packed>>16);
    float spirit_base{},spirit_add{},spirit_factor{1},ability_percent{},all_factor{1};bool approximate=false;
    const bool live_spirit=assassinate_spirit(out,chunks,pawn,a.spirit,approximate);
    if(!live_spirit&&(!assassinate_starting_stat(out.hero.id,59,spirit_base)||!assassinate_modifier(pawn,f.as_spirit,spirit_add,approximate)
        ||!assassinate_modifier(pawn,f.as_spirit_percent,spirit_factor,approximate,true)
        ||spirit_factor<0||spirit_factor>20||spirit_add<-1000||spirit_add>10000)){a.status="Waiting for current Spirit Power";return;}
    if(!live_spirit)a.spirit=(spirit_base+spirit_add)*spirit_factor;
    if(!assassinate_modifier(pawn,f.as_ability_damage,ability_percent,approximate))omitted(a.omissions,"ability damage modifier");
    if(!assassinate_modifier(pawn,f.as_all_damage,all_factor,approximate,true))omitted(a.omissions,"outgoing damage modifier");
    if(!assassinate_modifier(pawn,f.as_piercing,a.piercing,approximate))omitted(a.omissions,"resistance piercing");
    if(ability_percent<-100||ability_percent>10000||all_factor<0||all_factor>100||a.piercing<-100||a.piercing>10000)return;
    std::array<unsigned char,40> map{},map_after{};
    if(!read(data+profile_.fields.ability_properties,map)){a.status="Ability definitions unavailable";return;}
    uint32_t capacity{},count{};uintptr_t entries{};int root{};
    std::memcpy(&capacity,map.data()+12,4);capacity&=0x7fffffff;std::memcpy(&entries,map.data()+16,8);
    std::memcpy(&root,map.data()+24,4);std::memcpy(&count,map.data()+28,4);
    if(!entries||!count||count>capacity||capacity>256||root<0||uint32_t(root)>=capacity){a.status="Ability definitions invalid";return;}
    struct Property {const char* name;uint32_t view;float* result;};
    const Property properties[]={{"Damage",f.as_damage_view,&a.damage},{"LowHealthEnemyDamageBonus",f.as_bonus_view,&a.low_bonus},
        {"HeadshotBonus",f.as_head_view,&a.head_bonus},{"LowHealthEnemyThresholdPct",f.as_threshold_view,&a.low_threshold},
        {"MinChargeDamagePercent",f.as_min_power_view,&a.minimum_power},{"TimeToFullCharge",f.as_charge_view,&a.full_charge},{"Range",f.as_range_view,&a.range}};
    if(assassinate_template_.data!=data||assassinate_template_.map!=map) {
        AssassinateTemplate resolved;resolved.data=data;resolved.map=map;
        std::array<bool,256> visited{};std::vector<int> pending{root};
        while(!pending.empty()) {
            int index=pending.back();pending.pop_back();if(index<0)continue;
            if(uint32_t(index)>=capacity||visited[size_t(index)]){a.status="Ability dictionary changed";return;}visited[size_t(index)]=true;
            const auto at=entries+v.ability_property_stride*size_t(index);std::array<unsigned char,24> node{};uintptr_t key{};int left{},right{};
            if(!read(at,node))return;std::memcpy(&left,node.data(),4);std::memcpy(&right,node.data()+4,4);std::memcpy(&key,node.data()+16,8);
            const auto name=text(key,64);
            for(size_t p=0;p<std::size(properties);++p)if(name==properties[p].name)resolved.definitions[p]=at+24;
            pending.push_back(left);pending.push_back(right);
        }
        if(std::find(resolved.definitions.begin(),resolved.definitions.end(),uintptr_t{})!=resolved.definitions.end()){a.status="Required ability definition missing";return;}
        assassinate_template_=resolved;
    }
    bool used_cache=false,used_definitions=false;
    for(size_t p=0;p<std::size(properties);++p) {
        const auto& property=properties[p];const auto definition=assassinate_template_.definitions[p];
        auto view=ability.address+property.view;uint8_t reusable{},after_reusable{};float cached_value{},value{};
        // The historical contract calls this byte "dirty"; the native getter
        // uses 1 for a reusable cache. Accept it only after independently
        // reproducing the value for the current upgrades and Spirit below.
        const bool cached=property.view&&pointer(view+v.property_definition)==definition&&read(view+v.property_dirty,reusable)&&reusable==1
            &&read(view+v.property_cached_value,cached_value)&&std::isfinite(cached_value)&&read(view+v.property_dirty,after_reusable)&&after_reusable==1
            &&pointer(view+v.property_definition)==definition;
        used_definitions=true;uint16_t required{};
        if(!read(definition+106,required)||!read(definition+v.property_base_value,value)||!std::isfinite(value)){a.status="Ability base value unavailable";return;}
        if((upgrade&required)!=required)value=0;
        else {
            int upgrades{};uintptr_t list{};
            if(!read(definition+168,upgrades)||!read(definition+176,list)||upgrades<0||upgrades>16||(upgrades&&!list))return;
            for(int i=0;i<upgrades;++i) {
                auto entry=pointer(list+8*size_t(i));auto level=pointer(entry+48);uint16_t mask{};int operation{};float amount{};
                if(!entry||!level||!read(level+32,mask)||!read(entry+32,operation)||!read(entry+28,amount)||!std::isfinite(amount)||operation<0||operation>1)return;
                if((upgrade&mask)==mask){if(operation==0)value+=amount;else value*=1+amount/100;}
            }
            auto scale=pointer(definition+72);
            if(scale) {
                auto scale_data=pointer(scale+8);int stat{};float amount{};uint8_t disabled{};
                if(type(scale)!=".?AVCScaleFunctionAbilityProperty_TechDamage@@"||!scale_data||!read(scale_data+f.as_scale_stat,stat)||stat!=59
                    ||!read(scale_data+f.as_scale_amount,amount)||!std::isfinite(amount)||amount<0||amount>100
                    ||!read(scale_data+f.as_scale_disabled,disabled)||disabled>1){a.status="Unsupported ability scaling / update reader";return;}
                if(!disabled)value=(value+a.spirit*amount)*(1+ability_percent/100)*all_factor;
            }
        }
        if(cached&&std::abs(cached_value-value)<=std::max(.001f,std::abs(value)*.00001f)){value=cached_value;used_cache=true;}
        *property.result=value;
    }
    a.minimum_power/=100;
    if(a.damage<=0||a.damage>1000000||a.low_bonus<0||a.low_bonus>1000000||a.low_threshold<0||a.low_threshold>100||a.head_bonus<0||a.head_bonus>1000
        ||a.minimum_power<0||a.minimum_power>1||a.full_charge<=0||a.full_charge>30||a.range<=0||a.range>1000000){a.status="Ability values outside supported range";return;}
    auto prop=pointer(pawn.address+profile_.fields.modifier_property);std::array<uint32_t,11> enabled{},predicted{},interrupt{},again{};
    uint8_t interrupted{};
    if(!prop||!read(prop+profile_.fields.state_enabled,enabled)||!read(prop+profile_.fields.state_predicted,predicted)
        ||!read(data+f.as_interrupt_states,interrupt)||!read(pawn.address+profile_.fields.ability_component+profile_.fields.ability_interrupt,interrupted)||interrupted>1)return;
    a.cast_allowed=!interrupted;
    for(size_t i=0;i<enabled.size();++i)if((enabled[i]|predicted[i])&interrupt[i])a.cast_allowed=false;
    if(!read(data+profile_.fields.ability_properties,map_after)||map!=map_after||!read(ability.address+f.as_upgrade_info,packed_after)||packed!=packed_after
        ||!read(prop+profile_.fields.state_enabled,again)||again!=enabled||!read(prop+profile_.fields.state_predicted,again)||again!=predicted
        ||entity(chunks,ability.handle).address!=ability.address||entity(chunks,pawn.handle).address!=pawn.address||pointer(ability.address+v.ability_vdata)!=data)return;
    a.valid=true;a.estimated=approximate||!a.omissions.empty();
    if(approximate)omitted(a.omissions,"ability-conditioned modifier caches");
    a.source=used_cache?(used_definitions?"Effective caches + live definitions":"Effective ability caches"):"Live definitions + Spirit + upgrades";
    if(live_spirit)a.source+=" / live Spirit modifiers";
    a.status=a.cast_allowed?(a.estimated?"Damage estimate ready":"Damage data ready"):"Ability interrupted / cannot cast";
}
void GameReader::read_assassinate_defense(Player& player,const Entity& pawn) {
    auto& d=player.assassinate_defense;const auto& f=profile_.features;d.sampled=GetTickCount64();
    bool estimated=false;
    auto sample=[&](uint32_t index,float& value,unsigned bit,const char* name,bool normalized=false) {
        float n{};bool approximate=false;
        if(assassinate_modifier(pawn,index,n,approximate,normalized)&&std::abs(n)<=1000000){value=n;d.known|=bit;estimated|=approximate;return true;}
        omitted(d.omissions,name);return false;
    };
    float base{},resist{1},reduction{1};
    const bool base_valid=assassinate_starting_stat(player.hero_id,14,base);
    const bool r=sample(f.as_resist,resist,0,"Spirit resistance",true),rr=sample(f.as_resist_reduction,reduction,0,"Spirit resistance reduction",true);
    if(!base_valid)omitted(d.omissions,"base Spirit resistance");
    if(base_valid&&r&&rr){const auto positive=(2-resist)*(base>=0?1-base/100:1.f);const auto negative=reduction*(base<0?1+base/100:1.f);d.resistance=(negative-positive)*100;d.known|=DefenseResist;}
    sample(f.as_incoming_all,d.incoming_all,DefenseIncomingAll,"incoming damage");
    sample(f.as_incoming_spirit,d.incoming_spirit,DefenseIncomingSpirit,"incoming Spirit damage");
    float factor{1};if(sample(f.as_ability_reduction,factor,DefenseAbilityReduction,"ability reduction",true))d.ability_reduction=(factor-1)*100;
    factor=1;if(sample(f.as_all_reduction,factor,DefenseAllReduction,"all damage reduction",true))d.all_reduction=(factor-1)*100;
    sample(f.as_barrier,d.barrier,DefenseBarrier,"barriers");
    auto prop=pointer(pawn.address+profile_.fields.modifier_property);std::array<uint32_t,11> enabled{},predicted{},again{};
    if(prop&&f.as_invulnerable&&f.as_tech_invulnerable&&f.as_tech_damage_invulnerable&&read(prop+profile_.fields.state_enabled,enabled)&&read(prop+profile_.fields.state_predicted,predicted)) {
        for(auto state:{f.as_invulnerable,f.as_tech_invulnerable,f.as_tech_damage_invulnerable,f.as_unkillable,f.as_unkillable_hidden})if(state&&state<352&&((enabled[state/32]|predicted[state/32])&(1u<<(state%32))))d.protected_state=true;
        if(read(prop+profile_.fields.state_enabled,again)&&again==enabled&&read(prop+profile_.fields.state_predicted,again)&&again==predicted) {
            if(f.as_unkillable&&f.as_unkillable_hidden)d.known|=DefenseStates;else omitted(d.omissions,"death-prevention states");
        }
        else {d.protected_state=true;omitted(d.omissions,"unstable protection state");}
    }else omitted(d.omissions,"protection states");
    if(estimated)omitted(d.omissions,"attacker-conditioned modifiers");
}
}
