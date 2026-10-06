#include "game_reader.h"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>

namespace overlay {
namespace {
// 4 October 2026 client: field layouts checked against the corrected dump,
// globals checked against unique signatures and RIP-relative references.
constexpr char supported[] = "f66a0fdfe60029cca711dff32644941303396c2d9d16774a6cb2570c28520b10";
std::string sha256(const std::vector<unsigned char>& bytes) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)<0) return {};
    DWORD size{}, got{};
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &got, 0);
    std::vector<unsigned char> object(size); unsigned char digest[32]{};
    bool ok = BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
    if (ok) ok = BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0
        && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if (!ok) return {};
    std::ostringstream result;
    for (auto b:digest) result<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
    return result.str();
}
std::vector<int> pattern(const char* text) {
    std::istringstream stream(text); std::string token; std::vector<int> result;
    while (stream>>token) result.push_back(token=="?" ? -1 : std::stoi(token,nullptr,16));
    return result;
}
// Resolve only a unique match in an executable PE section, yielding an image RVA.
uintptr_t scan(const std::vector<unsigned char>& file, const char* signature) {
    if (file.size()<sizeof(IMAGE_DOS_HEADER)) return 0;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0
        || size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>file.size()) return 0;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE) return 0;
    auto section=IMAGE_FIRST_SECTION(nt); auto p=pattern(signature); uintptr_t found{}; int matches{};
    if (reinterpret_cast<const unsigned char*>(section+nt->FileHeader.NumberOfSections)>file.data()+file.size()) return 0;
    for (unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        if (!(section[s].Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        const size_t begin=section[s].PointerToRawData, size=section[s].SizeOfRawData;
        if (begin+size>file.size() || size<p.size()) continue;
        for (size_t i=0;i<=size-p.size();++i) {
            if (p[0]>=0 && file[begin+i]!=p[0]) continue;
            bool match=true;
            for (size_t j=1;j<p.size();++j) if (p[j]>=0 && file[begin+i+j]!=p[j]) {match=false;break;}
            if (match) {found=section[s].VirtualAddress+i;++matches;}
        }
    }
    return matches==1 ? found : 0;
}
}
GameReader::~GameReader() { detach(); }
bool GameReader::read(uintptr_t address, void* data, size_t count) const {
    if (!process_ || address<0x10000 || address>0x7fffffffffffULL || !count) return false;
    SIZE_T actual{};
    ++read_calls_;
    return ReadProcessMemory(process_,reinterpret_cast<LPCVOID>(address),data,count,&actual) && actual==count;
}
uintptr_t GameReader::pointer(uintptr_t address) const { uintptr_t value{}; read(address,value); return value; }
std::string GameReader::text(uintptr_t address, size_t limit) const {
    std::string result;
    // Batch names for the streamed-world scan. Fall back at a page boundary so
    // a valid short string is not rejected just because a larger read crosses it.
    for(size_t i=0;i<limit;) {
        std::array<char,16> block{};size_t count=std::min(block.size(),limit-i);
        if(!read(address+i,block.data(),count)){count=1;if(!read(address+i,block[0]))return {};}
        for(size_t j=0;j<count;++j){if(!block[j])return result;result+=block[j];}
        i+=count;
    }
    return {};
}
void GameReader::detach() {
    if (process_) CloseHandle(process_);
    process_=nullptr; pid_=0; base_=entity_slot_=matrix_address_=engine_base_=world_renderer_base_=0;
    types_.clear(); models_.clear(); controllers_.clear();extras_.clear(); next_enumerate_=0;
    velocities_.clear();local_motion_={};local_handle_=0;
    visibility_.reset();visibility_map_.clear();next_visibility_load_=0;
    clear_map_cache();map_resolution_status_.clear();
}
std::string GameReader::type(uintptr_t object) {
    auto vtable=pointer(object); if (!vtable) return {};
    auto found=types_.find(vtable); if (found!=types_.end()) return found->second;
    auto locator=pointer(vtable-8); std::array<uint32_t,6> col{}; std::string name;
    if (read(locator,col) && col[0]==1 && col[5]<locator) name=text(locator-col[5]+col[3]+16,180);
    if(types_.size()>=4096)types_.clear();
    types_[vtable]=name; return name;
}
bool GameReader::attach() {
    const auto now=GetTickCount64(); if (now<next_attach_) return false;
    next_attach_=now+3000;
    HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe); DWORD target{};
    if (list!=INVALID_HANDLE_VALUE) {
        if (Process32FirstW(list,&pe)) do { if (!_wcsicmp(pe.szExeFile,L"deadlock.exe")) {target=pe.th32ProcessID;break;} } while (Process32NextW(list,&pe));
        CloseHandle(list);
    }
    if (!target) {status_="Waiting for Deadlock"; return false;}
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,target);
    MODULEENTRY32W me{}; me.dwSize=sizeof(me); std::wstring path,engine_path,world_path;
    if (modules!=INVALID_HANDLE_VALUE) {
        if (Module32FirstW(modules,&me)) do {
            if (!_wcsicmp(me.szModule,L"client.dll")) {base_=reinterpret_cast<uintptr_t>(me.modBaseAddr);path=me.szExePath;}
            if (!_wcsicmp(me.szModule,L"engine2.dll")) {engine_base_=reinterpret_cast<uintptr_t>(me.modBaseAddr);engine_path=me.szExePath;}
            if (!_wcsicmp(me.szModule,L"worldrenderer.dll")) {world_renderer_base_=reinterpret_cast<uintptr_t>(me.modBaseAddr);world_path=me.szExePath;}
        } while (Module32NextW(modules,&me));
        CloseHandle(modules);
    }
    if (path.empty()) {status_="Waiting for client.dll (or insufficient access)";return false;}
    std::ifstream input(std::filesystem::path(path),std::ios::binary);
    std::vector<unsigned char> file{std::istreambuf_iterator<char>(input),{}};
    if (sha256(file)!=supported) {status_="Unsupported client.dll: update required";next_attach_=now+10000;return false;}
    std::ifstream engine_input(std::filesystem::path(engine_path),std::ios::binary);
    std::vector<unsigned char> engine_file{std::istreambuf_iterator<char>(engine_input),{}};
    if(sha256(engine_file)!="0782caed3e1c476389fe2a27a0713d47567a5237f706b151ce7cc34a05dbadc3")engine_base_=0;
    std::ifstream world_input(std::filesystem::path(world_path),std::ios::binary);
    std::vector<unsigned char> world_file{std::istreambuf_iterator<char>(world_input),{}};
    if(sha256(world_file)!="c14c141a2c16caf38c29debe169e8b406f7474800766281d81ba6fe7c2ffc474")world_renderer_base_=0;
    process_=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,target);
    if (!process_) {status_="Read access denied; try Run as administrator";return false;}
    pid_=target;
    auto system=scan(file,"48 8B 0D ? ? ? ? 8B D0 E8 ? ? ? ? 48 85 C0 74 ? 48 8B 40 10");
    auto transform=scan(file,"40 53 48 83 EC 30 48 83 3D ? ? ? ? 00 48 8B DA 74 ? 48 8B D1 48 8D 4C 24 ? E8");
    if (!system || !transform) {status_="Signatures missing or ambiguous";detach();return false;}
    int32_t displacement{};
    if (!read(base_+system+3,displacement)) {status_="Cannot read entity signature";detach();return false;}
    entity_slot_=base_+system+7+displacement;
    std::array<unsigned char,64> instructions{};
    if (!read(base_+transform,instructions)) {status_="Cannot read projection signature";detach();return false;}
    int matches{};
    for (size_t i=0;i+7<=instructions.size();++i) {
        if (instructions[i]==0x48 && instructions[i+1]==0x8d && instructions[i+2]==0x0d) {
            std::memcpy(&displacement,instructions.data()+i+3,4);
            matrix_address_=base_+transform+i+7+displacement; ++matches;
        }
    }
    if (matches!=1 || entity_slot_-base_!=0x3be8840 || matrix_address_-base_!=0x3c1d760) {
        status_="Resolved addresses differ from validated profile";detach();return false;
    }
    status_="Connected to validated client.dll"; return true;
}
void GameReader::clear_map_cache() {
    loaded_map_.clear();map_array_=map_rep_=map_world_=map_path_=0;map_count_=0;map_index_=-1;next_map_resolve_=0;
}
std::string GameReader::loaded_arena(uint64_t now) {
    auto unavailable=[&](const char* reason){clear_map_cache();map_resolution_status_=reason;return std::string{};};
    if(!world_renderer_base_)return unavailable("Streamed map needs a supported worldrenderer.dll profile");
    // Profile validated by the worldrenderer.dll hash on attach. This is the
    // active CSingleWorldRep vector, not the resource cache containing old maps.
    auto manager=world_renderer_base_+0x1d9760;
    if(pointer(manager)!=world_renderer_base_+0x14c720)return unavailable("Waiting for world renderer");
    std::array<unsigned char,24> header{};int count{};uint32_t capacity{};uintptr_t array{};
    if(!read(manager+0x90,header))return unavailable("Cannot read loaded map worlds");
    std::memcpy(&count,header.data(),4);std::memcpy(&array,header.data()+8,8);std::memcpy(&capacity,header.data()+16,4);
    if(!valid_world_vector(count,capacity,array))return unavailable("Invalid loaded-world list");
    if(!count)return unavailable("Waiting for streamed arena world");
    // Cheap identity checks between full scans prevent reusing a removed world.
    if(now<next_map_resolve_&&map_index_>=0&&map_index_<count&&count==map_count_&&array==map_array_
        &&pointer(array+size_t(map_index_)*8)==map_rep_&&pointer(map_rep_+0x30)==map_world_
        &&pointer(map_world_)==world_renderer_base_+0x14b6f0&&pointer(map_world_+0x240)==map_path_)return loaded_map_;
    std::vector<uintptr_t> entries(size_t(count),0);
    if(!read(array,entries.data(),entries.size()*8))return unavailable("Waiting for stable map-world list");
    std::string selected;uintptr_t selected_rep{},selected_world{},selected_path{};int selected_index=-1;
    for(int index=0;index<count;++index) {
        auto rep=entries[index];if(!rep)continue;
        std::array<uintptr_t,8> fields{};
        if(!read(rep,fields)||fields[0]!=world_renderer_base_+0x14c460)return unavailable("Waiting for stable map-world entries");
        auto world=fields[6];if(!world)continue;
        if(pointer(world)!=world_renderer_base_+0x14b6f0)return unavailable("Invalid streamed-world instance");
        auto path=pointer(world+0x240);if(!path)continue;
        auto resource=text(path,160);if(resource.empty())return unavailable("Waiting for loaded-world resource name");
        auto candidate=visibility_world_map_key(resource);if(candidate.empty())continue;
        if(!add_visibility_world(selected,candidate))return unavailable("Multiple arena worlds; waiting for map transition");
        if(selected_index<0){selected_index=index;selected_rep=rep;selected_world=world;selected_path=path;}
    }
    std::array<unsigned char,24> after{};std::vector<uintptr_t> current(entries.size());
    if(!read(manager+0x90,after)||after!=header||!read(array,current.data(),current.size()*8)||current!=entries)
        return unavailable("Map worlds changed; waiting for a stable sample");
    if(selected.empty())return unavailable("Waiting for streamed arena world");
    if(pointer(selected_rep+0x30)!=selected_world||pointer(selected_world+0x240)!=selected_path)
        return unavailable("Arena world changed; waiting for a stable sample");
    loaded_map_=selected;map_array_=array;map_count_=count;map_index_=selected_index;
    map_rep_=selected_rep;map_world_=selected_world;map_path_=selected_path;next_map_resolve_=now+1000;
    map_resolution_status_="Loaded arena world";return loaded_map_;
}
bool GameReader::enumerate(uintptr_t system) {
    std::array<uintptr_t,64> chunks{}; if (!read(system+0x10,chunks)) return false;
    controllers_.clear();extras_.clear(); std::vector<unsigned char> block(512*0x70);
    for (uint32_t c=0;c<64;++c) {
        if (!chunks[c] || !read(chunks[c],block.data(),block.size())) continue;
        for (uint32_t s=0;s<512;++s) {
            uintptr_t ptr{}; uint32_t handle{};
            std::memcpy(&ptr,block.data()+s*0x70,8); std::memcpy(&handle,block.data()+s*0x70+0x10,4);
            uint32_t index=c*512+s;
            if (!ptr || index>0x7ffe || handle==UINT32_MAX || (handle&0x7fff)!=index) continue;
            auto name=type(ptr);
            if (name.find("CitadelPlayerController")!=std::string::npos) controllers_.push_back({ptr,handle});
            auto kind=extra_target_kind(name);
            if(extras_.size()<512&&((kind==TargetKind::Minion&&options_.minions)||(kind==TargetKind::SoulOrb&&options_.orbs)))extras_.push_back({ptr,handle,kind});
        }
    }
    return true;
}
GameReader::Entity GameReader::entity(const std::array<uintptr_t,64>& chunks, uint32_t handle) const {
    uint32_t index=handle&0x7fff; if (handle==UINT32_MAX || index>0x7ffe) return {};
    auto chunk=chunks[index>>9]; if (!chunk) return {};
    std::array<unsigned char,0x18> identity{};
    if (!read(chunk+(index&0x1ff)*0x70,identity)) return {};
    Entity result; std::memcpy(&result.address,identity.data(),8);std::memcpy(&result.handle,identity.data()+0x10,4);
    return result.address && valid_handle(handle,result.handle,index) ? result : Entity{};
}
bool GameReader::anchor(uintptr_t scene, Player& player,bool skeleton) {
    std::array<unsigned char,0x28> model_state{};
    if(!read(scene+0x1c0,model_state))return false;
    uintptr_t binding{},transforms{};
    std::memcpy(&transforms,model_state.data(),8);std::memcpy(&binding,model_state.data()+0x20,8);
    auto model=pointer(binding);
    if (!model || !transforms) return false;
    auto found=models_.find(model);
    if (found==models_.end()) {
        if(models_.size()>=256)models_.clear();
        BoneInfo info; if (!read(model+0x178,info.count) || info.count<1 || info.count>1024) return false;
        auto names=pointer(model+0x168); if (!names) return false;
        int end=-1, head=-1,spine=-1,chest=-1;
        std::vector<uintptr_t> pointers(info.count);
        std::vector<std::string> bone_names(info.count);
        if (!read(names,pointers.data(),pointers.size()*8)) return false;
        for (int i=0;i<info.count;++i) {
            const auto n=text(pointers[i]);bone_names[i]=n; if (n=="head_end") end=i; else if (n=="head") head=i;
            else if(n=="spine_2")spine=i;else if(n=="chest")chest=i;else if(n=="pelvis")info.dots[2]=i;
        }
        info.index=select_anchor(end,head);info.name=end>=0?"head_end":"head";
        info.dots[0]=head;info.dots[1]=spine>=0?spine:chest;
        std::vector<int16_t> parents(info.count);auto parent_array=pointer(model+0x180);
        if(parent_array&&read(parent_array,parents.data(),parents.size()*sizeof(int16_t)))info.edges=skeleton_edges(bone_names,parents);
        found=models_.emplace(model,std::move(info)).first;
    }
    const auto& info=found->second; int16_t count{};
    if (info.index<0 || !read(scene+0x3e0,count) || count!=info.count || info.index>=count) return false;
    int first=info.index,last=info.index;
    for(auto index:info.dots)if(index>=0&&index<count){first=std::min(first,index);last=std::max(last,index);}
    if(skeleton)for(auto [a,b]:info.edges){first=std::min(first,std::min(a,b));last=std::max(last,std::max(a,b));}
    std::array<unsigned char,1024*0x20> positions;
    if(!read(transforms+first*0x20,positions.data(),size_t(last-first)*0x20+sizeof(Vec3)))return false;
    auto position=[&](int index,Vec3& out){if(index<first||index>last)return false;std::memcpy(&out,positions.data()+(index-first)*0x20,sizeof(out));return finite(out);};
    if(!position(info.index,player.head))return false;
    for(size_t i=0;i<info.dots.size();++i)player.dot_valid[i]=position(info.dots[i],player.dots[i]);
    if(skeleton)for(auto [a,b]:info.edges) {
        SkeletonSegment segment;
        if(position(a,segment.a)&&position(b,segment.b)&&dot(sub(segment.a,segment.b),sub(segment.a,segment.b))<512.f*512.f)player.skeleton.push_back(segment);
    }
    // Do not publish a transform if the model/array changed during the sample.
    std::array<unsigned char,0x28> current_state{};
    if(!read(scene+0x1c0,current_state))return false;
    uintptr_t current_binding{},current_transforms{};
    std::memcpy(&current_transforms,current_state.data(),8);std::memcpy(&current_binding,current_state.data()+0x20,8);
    if(current_binding!=binding||current_transforms!=transforms||pointer(binding)!=model)return false;
    player.anchor=info.name; return true;
}
bool GameReader::extra_anchor(const Entity& object,uintptr_t scene,FocusTarget& target) {
    if(object.kind==TargetKind::Minion) {
        Player bones;
        if(anchor(scene,bones)&&bones.dot_valid[0]&&bones.dot_valid[2]) {
            if(!bones.dot_valid[1]){bones.dots[1]=mul(add(bones.dots[0],bones.dots[2]),.5f);bones.dot_valid[1]=true;}
            target.dots=bones.dots;target.dot_valid=bones.dot_valid;return true;
        }
    }
    std::array<float,8> world_transform{};
    if(!read(scene+0x10,world_transform))return false;
    // C_BaseEntity's collision pointer and CCollisionProperty mins/maxs were
    // verified in this client's metadata. Avoid aiming at a minion's feet.
    auto collision=pointer(object.address+0x340);std::array<Vec3,2> bounds{};
    bool bounds_valid=collision&&read(collision+0x40,bounds)&&finite(bounds[0])&&finite(bounds[1]);
    if(bounds_valid)for(int axis=0;axis<3;++axis) {
        const float low=axis==0?bounds[0].x:axis==1?bounds[0].y:bounds[0].z;
        const float high=axis==0?bounds[1].x:axis==1?bounds[1].y:bounds[1].z;
        if(high<low||high-low>512||std::abs(low)>1024||std::abs(high)>1024)bounds_valid=false;
    }
    if(object.kind==TargetKind::Minion&&!bounds_valid)return false;
    Vec3 center=bounds_valid?mul(add(bounds[0],bounds[1]),.5f):Vec3{},world{};
    // Nonhuman NPC rigs use their collision center rather than fabricated bones.
    // Their mins/maxs are local coordinates. CGameSceneNode::m_nodeToWorld
    // supplies position, scale and quaternion; scale is essential for neutrals.
    if(!scene_point(world_transform,center,world))return false;
    target.dots.fill(world);target.dot_valid.fill(true);
    return true;
}
WeaponProfile GameReader::weapon_profile(const std::array<uintptr_t,64>& chunks,const Entity& pawn) {
    WeaponProfile result;
    if(type(pawn.address).find("CitadelPlayerPawn")==std::string::npos)return result;
    // Primary slot 21 in CCitadelAbilityComponent's handle map. Follow its live hash chain.
    std::array<unsigned char,72> map{};auto component=pawn.address+0x1440;
    if(!read(component+24,map))return result;
    uint32_t buckets{},bucket_capacity{},entry_capacity{},count{};uintptr_t bucket_array{},entries{};
    std::memcpy(&buckets,map.data(),4);std::memcpy(&bucket_capacity,map.data()+4,4);std::memcpy(&bucket_array,map.data()+8,8);
    std::memcpy(&entry_capacity,map.data()+44,4);std::memcpy(&entries,map.data()+48,8);std::memcpy(&count,map.data()+60,4);
    bucket_capacity&=0x7fffffff;entry_capacity&=0x7fffffff;
    if(!buckets||buckets>512||(buckets&(buckets-1))||bucket_capacity<buckets||!bucket_array||!entries||!count||entry_capacity>128)return result;
    uint32_t hash=21u*uint32_t(-2048144789);hash^=hash>>13;hash*=uint32_t(-1028477387);hash^=hash>>16;
    int index{};if(!read(bucket_array+4*(hash&(buckets-1)),index))return result;
    uint32_t handle=UINT32_MAX;uintptr_t slot_address{};
    for(uint32_t steps=0;steps<entry_capacity&&index>=0&&uint32_t(index)<entry_capacity;++steps){
        std::array<unsigned char,12> entry{};if(!read(entries+12*size_t(index),entry))return result;
        uint16_t key{};std::memcpy(&key,entry.data(),2);
        if(key==21){std::memcpy(&handle,entry.data()+4,4);slot_address=entries+12*size_t(index);break;}
        std::memcpy(&index,entry.data()+8,4);
    }
    auto ability=entity(chunks,handle);if(!ability.address||type(ability.address).find("PrimaryWeapon")==std::string::npos)return result;
    auto vdata=pointer(ability.address+0x390);if(!vdata)return result;
    std::array<unsigned char,32> weapon_map{};if(!read(vdata+368,weapon_map))return result;
    uint32_t capacity{};uintptr_t array{};int root{};
    std::memcpy(&capacity,weapon_map.data()+12,4);capacity&=0x7fffffff;
    std::memcpy(&array,weapon_map.data()+16,8);std::memcpy(&root,weapon_map.data()+24,4);
    auto primary=pointer(base_+0x3438b28);uintptr_t info{};
    if(!primary||!array||capacity>128)return result;
    for(uint32_t steps=0;steps<capacity&&root>=0&&uint32_t(root)<capacity;++steps){
        std::array<unsigned char,24> node{};auto address=array+0x8e8*size_t(root);
        if(!read(address,node))return result;uintptr_t key{};std::memcpy(&key,node.data()+16,8);
        if(key==primary){info=address+24;break;}
        std::memcpy(&root,node.data()+(primary<key?0:4),4);
    }
    if(!info)return result;
    std::array<unsigned char,32> values{};if(!read(info+0xd8,values))return result;
    std::memcpy(&result.base_speed,values.data(),4);std::memcpy(&result.random_factor,values.data()+4,4);std::memcpy(&result.inheritance,values.data()+28,4);
    auto prop=pointer(pawn.address+0x348);uint32_t version_before{},version_after{};
    std::array<uint8_t,2> groups{};
    if(!prop||!read(prop+512,version_before)||!read(prop+160+170,groups))return result;
    if(groups[1]!=255){result.status="Weapon speed override active";return result;}
    if(groups[0]!=255){
        result.status="Waiting for fresh bullet-speed modifier";
        uint8_t policy{},fallback{};uint16_t dirty{};
        // The engine writes this mirror even when its cache-read optimization is disabled.
        if(!read(base_+0x3b8acf0+170,policy)||!read(base_+0x3b8adec,fallback)||!read(prop+516+2*170,dirty))return result;
        if(!policy)policy=fallback;
        std::array<uint32_t,12> cache{},again{};
        if(!read(prop+1008+48*170,cache))return result;
        float bonus{};std::memcpy(&bonus,&cache[8],4);uint32_t tick{};
        if(policy==4){auto globals=pointer(base_+0x3273918);if(!globals||!read(globals+68,tick))return result;}
        if(!read(prop+512,version_after)||!read(prop+1008+48*170,again)||cache!=again
            ||!valid_modifier_cache(cache[0],cache[2],version_before,version_after,policy,cache[3],tick,dirty,cache[4],bonus,cache[1]))return result;
        result.bonus_percent=cache[4]?bonus:0;
    }
    if(!read(prop+512,version_after)||version_before!=version_after||pointer(pawn.address+0x348)!=prop
        ||pointer(ability.address+0x390)!=vdata||pointer(vdata+384)!=array||entity(chunks,handle).address!=ability.address
        ||entity(chunks,pawn.handle).address!=pawn.address)return result;
    std::array<unsigned char,72> map_after{};if(!read(component+24,map_after)||map_after!=map)return result;
    uint32_t current_handle{};if(!read(slot_address+4,current_handle)||current_handle!=handle)return result;
    if(!valid_weapon_numbers(result.base_speed,result.bonus_percent,result.inheritance,result.random_factor)){
        result.status=result.random_factor!=0?"Random bullet speed: auto prediction unavailable":"Invalid weapon-speed data";return result;
    }
    result.handle=handle;result.speed=result.base_speed*(1.f+result.bonus_percent/100.f);
    result.valid=true;result.status="Live primary weapon";return result;
}
Snapshot GameReader::sample(ReadOptions options) {
    if(options.minions!=options_.minions||options.orbs!=options_.orbs)next_enumerate_=0;
    options_=options;
    const auto started=std::chrono::steady_clock::now();read_calls_=0;
    Snapshot result;
    if (process_ && WaitForSingleObject(process_,0)!=WAIT_TIMEOUT) {detach();status_="Waiting for Deadlock";next_attach_=0;}
    if (!process_ && !attach()) {result.status=status_;return result;}
    result.pid=pid_;result.status=status_;
    auto rules=pointer(base_+0x3c17b60);std::array<int,2> modes{};
    if(rules&&pointer(rules)==base_+0x26859b0&&read(rules+0xa8,modes)&&pointer(base_+0x3c17b60)==rules) {
        result.match_mode=modes[0];result.game_mode=modes[1];
        // Verified mode enum: game 3=Sandbox; match 3=CoopBot.
        result.practice=(modes[1]==3||modes[0]==3);
        auto testing=pointer(base_+0x367e8b0);uint8_t hero_testing{};
        // The game's sandbox predicate also accepts its server testing convar.
        if((modes[0]==0||modes[0]==2)&&testing&&read(testing+88,hero_testing)&&hero_testing==1)result.practice=true;
    }
    if(engine_base_) {
        // Source2EngineToClient001 GetLevelName reads this connected-client state.
        auto connection=pointer(engine_base_+0x8b2db0);int state{};
        if(connection&&read(connection+560,state)&&state>=2) {
            auto name=pointer(connection+536);
            if(name)result.level_name=text(name,96);
        }
        auto demo=pointer(engine_base_+0x5b92b0);uint8_t playing{};
        // CDemoPlayer slot 11 is the playback predicate used by engine demo commands.
        if(demo&&pointer(demo)==engine_base_+0x4d7e88&&read(demo+0x1230,playing)&&playing==1
            &&pointer(engine_base_+0x5b92b0)==demo)result.replay=true;
    }
    if(visibility_map_key(result.level_name)=="start") {
        result.map_name=loaded_arena(GetTickCount64());result.map_source="loaded_arena_world";
    }else {
        clear_map_cache();result.map_name=result.level_name;result.map_source="engine_level";
    }
    result.practice=result.practice||result.map_name=="hero_testing"||result.map_name=="new_player_basics"||result.map_name=="fx_test";
    auto system=pointer(entity_slot_);
    if (!system || !read(matrix_address_,result.matrix)) {result.status="Waiting for a loaded match";return result;}
    for (auto value:result.matrix) if (!std::isfinite(value)) {result.status="Invalid projection matrix";return result;}
    auto now=GetTickCount64();
    const auto map_key=visibility_map_key(result.map_name);
    if(map_key!=visibility_map_){visibility_.reset();visibility_map_=map_key;next_visibility_load_=0;}
    if(!map_key.empty()&&!visibility_&&now>=next_visibility_load_){
        next_visibility_load_=now+5000;
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        auto mesh=std::make_shared<VisibilityMesh>();
        try {
            auto mesh_path=std::filesystem::path(executable).parent_path()/L"maps"/(map_key+".tri");
            auto disabled_path=mesh_path;disabled_path+=L".unavailable";
            if(std::filesystem::exists(disabled_path))visibility_status_="Map export failed - install all maps: "+map_key;
            else if(mesh->load(mesh_path)){visibility_=std::move(mesh);visibility_status_="Static map ready";}
            else visibility_status_="Missing / invalid map mesh: "+map_key;
        }catch(const std::exception&){visibility_status_="Map mesh load failed: "+map_key;}
    }
    result.visibility=visibility_;result.visibility_status=map_key.empty()?(result.map_source=="loaded_arena_world"?map_resolution_status_:"Waiting for map name"):visibility_status_;
    // Loading/building a mesh can take time. Refresh camera data before publishing a fresh sample.
    now=GetTickCount64();
    if(!read(matrix_address_,result.matrix)){result.status="Cannot read projection matrix";return result;}
    for(auto value:result.matrix)if(!std::isfinite(value)){result.status="Invalid projection matrix";return result;}
    result.camera_valid=camera_origin(result.matrix,result.camera_position);
    if (now>=next_enumerate_) {if (!enumerate(system)) {result.status="Cannot read entity list";return result;} next_enumerate_=now+((options.minions||options.orbs)?100:1000);}
    result.controllers=static_cast<int>(controllers_.size());
    std::array<uintptr_t,64> chunks{};
    if(!read(system+0x10,chunks)){result.status="Cannot read current entity chunks";return result;}
    for (const auto& controller:controllers_) {
        auto current=entity(chunks,controller.handle);
        if (current.address!=controller.address) {++result.invalid_handles;next_enumerate_=0;continue;}
        std::array<unsigned char,0xd5> controller_fields{};uint32_t handle{};
        if(!read(controller.address+0x6bc,controller_fields))continue;
        std::memcpy(&handle,controller_fields.data(),4);
        auto pawn=entity(chunks,handle);if (!pawn.address) {++result.invalid_handles;continue;}
        if(controller_fields[0xd4]) {
            result.weapon=weapon_profile(chunks,pawn);
            uint8_t team{};
            if(read(pawn.address+0x3ef,team)&&entity(chunks,handle).address==pawn.address)result.local_team=team;
            Player local;auto scene=pointer(pawn.address+0x330);
            if(local_handle_!=handle){local_motion_={};local_handle_=handle;}
            if(scene&&anchor(scene,local)&&local.dot_valid[2]&&entity(chunks,handle).address==pawn.address){
                local_motion_.update(local.dots[2],now);result.local_velocity=local_motion_.velocity;result.local_velocity_valid=local_motion_.valid;
            }else local_motion_={};
            uint32_t current_handle{};
            if(!read(controller.address+0x6bc,current_handle)||current_handle!=handle){result.weapon={};result.local_velocity_valid=false;local_motion_={};}
            continue;
        }
        if (type(pawn.address).find("CitadelPlayerPawn")==std::string::npos) continue;
        std::array<unsigned char,0xc0> fields{};
        if (!read(pawn.address+0x330,fields)) continue;
        uintptr_t scene{}; Player p; p.handle=handle;
        std::memcpy(&scene,fields.data(),8);std::memcpy(&p.maximum,fields.data()+0x20,4);std::memcpy(&p.health,fields.data()+0x24,4);
        p.team=fields[0xbf];uint8_t dormant{};
        if (fields[0x2c] || p.health<=0 || p.maximum<=0 || !scene || !read(scene+0x103,dormant) || dormant) continue;
        if (!anchor(scene,p,options.skeletons)) {++result.missing_anchors;continue;}
        if (entity(chunks,handle).address!=pawn.address) continue;
        if(p.dot_valid[2]&&(velocities_.size()<512||velocities_.contains(handle))){auto& estimate=velocities_[handle];estimate.update(p.dots[2],now);p.velocity=estimate.velocity;p.velocity_valid=estimate.valid;}
        result.players.push_back(std::move(p));
    }
    for(const auto& object:extras_) {
        if(entity(chunks,object.handle).address!=object.address)continue;
        std::array<unsigned char,0xc0> fields{};uintptr_t scene{};uint8_t dormant{};
        if(!read(object.address+0x330,fields))continue;
        std::memcpy(&scene,fields.data(),8);
        if(!scene||fields[0x2c]||!read(scene+0x103,dormant)||dormant)continue;
        FocusTarget target;target.handle=object.handle;target.kind=object.kind;target.team=fields[0xbf];
        std::memcpy(&target.health,fields.data()+0x24,4);std::memcpy(&target.maximum,fields.data()+0x20,4);
        if(object.kind==TargetKind::Minion&&(target.health<=0||target.maximum<=0))continue;
        if(!extra_anchor(object,scene,target)||entity(chunks,object.handle).address!=object.address||pointer(object.address+0x330)!=scene)continue;
        if(velocities_.size()<512||velocities_.contains(object.handle)) {
            auto& estimate=velocities_[object.handle];estimate.update(target.dots[1],now);
            target.velocity=estimate.velocity;target.velocity_valid=estimate.valid;
        }
        result.focus_targets.push_back(std::move(target));
    }
    for(auto it=velocities_.begin();it!=velocities_.end();){
        bool found=false;for(const auto& p:result.players)if(p.handle==it->first){found=true;break;}
        if(!found)for(const auto& p:result.focus_targets)if(p.handle==it->first){found=true;break;}
        if(!found)it=velocities_.erase(it);else ++it;
    }
    const auto ray_started=std::chrono::steady_clock::now();
    if(result.visibility&&result.camera_valid) {
        auto visible=[&](FocusTarget& target) {
            if(target.kind==TargetKind::SoulOrb)target.dot_visible.fill(result.visibility->clear(result.camera_position,target.dots[1]));
            else for(int i=0;i<3;++i)if(target.dot_valid[i])target.dot_visible[i]=result.visibility->clear(result.camera_position,target.dots[i]);
            target.visible=target.dot_visible[0]||target.dot_visible[1]||target.dot_visible[2];
        };
        for(auto& player:result.players) {
            visible(player);
            for(auto& segment:player.skeleton) {
                segment.visibility_known=true;
                segment.visible=result.visibility->clear(result.camera_position,segment.a)&&result.visibility->clear(result.camera_position,segment.b)
                    &&result.visibility->clear(result.camera_position,mul(add(segment.a,segment.b),.5f));
            }
        }
        for(auto& target:result.focus_targets)visible(target);
    }
    result.visibility_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-ray_started).count();
    result.time=GetTickCount64();result.read_calls=read_calls_;
    result.sample_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();return result;
}
}
