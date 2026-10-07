#include "game_reader.h"
#include "bone_rig.h"
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
// Build 6759 client: field layouts checked against the fresh 7 October dump,
// globals checked against unique signatures and RIP-relative references.
constexpr char supported[] = "b48636d0282a3f6916725e1701c0454738bb5a4903e83fc96a27b01dce800d23";
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
    hero_names_.clear();heroes_.clear();ability_handles_.clear();ability_pawn_=0;sniper_handle_=UINT32_MAX;
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
    // Retry transient RTTI read failures instead of hiding this class until detach.
    if(!name.empty())types_[vtable]=name;
    return name;
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
    if(sha256(engine_file)!="084c45473667c65174a9a19c428359ac335c3e990008dbf26c0eef91be44784c")engine_base_=0;
    std::ifstream world_input(std::filesystem::path(world_path),std::ios::binary);
    std::vector<unsigned char> world_file{std::istreambuf_iterator<char>(world_input),{}};
    if(sha256(world_file)!="bae38ed919214dcd74d49f9f01419f7ab2c86a57af666b4308f51241e9ff9888")world_renderer_base_=0;
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
    if (matches!=1 || entity_slot_-base_!=0x3c4ba40 || matrix_address_-base_!=0x3c80ae0) {
        status_="Resolved addresses differ from validated profile";detach();return false;
    }
    // ScreenTransform uses the legacy CViewSetup aspect override. The actual
    // renderer uses CViewRender's compact view at +0x10, whose final world-to-clip
    // matrix is +0x288. Its native horizontal FOV already includes zoom/overrides.
    // CViewRender initializer client+0x092810, matrix product client+0x2195400.
    if(pointer(base_+0x3689a70)!=base_+0x26896b8) {
        status_="Cannot validate final render view";detach();return false;
    }
    matrix_address_=base_+0x3689a70+0x298;
    // Hero IDs and names come from the current game's hero table and localization,
    // rather than a list that becomes stale when heroes are added or renamed.
    auto game_directory=std::filesystem::path(path).parent_path().parent_path().parent_path();
    std::ifstream names(game_directory/L"resource/localization/citadel_gc_hero_names/citadel_gc_hero_names_english.txt");
    std::string line;
    while(std::getline(names,line)&&hero_names_.size()<1024) {
        std::istringstream tokens(line);std::string key,value;
        if(!(tokens>>std::quoted(key)>>std::quoted(value))||!key.starts_with("hero_")||value.empty()||value.size()>128)continue;
        if(key.ends_with(":n"))key.resize(key.size()-2);
        hero_names_.try_emplace(std::move(key),std::move(value));
    }
    status_="Connected to validated client.dll"; return true;
}
bool GameReader::read_projection(Snapshot& result) const {
    const auto view=base_+0x3689a70;
    for(int attempt=0;attempt<3;++attempt) {
        uint8_t updating{},after{};std::array<float,8> camera{};
        if(!read(view+0x1330,updating)||updating)continue;
        if(!read(matrix_address_,result.matrix)||!read(view+0x10,camera)||!read(view+0x1330,after)||after)continue;
        auto info=projection_info(result.matrix);
        // Reject a partial update rather than combining two different views.
        if(!info.valid||!std::isfinite(camera[6])||!std::isfinite(camera[7])
            ||std::abs(info.horizontal_fov-camera[6])>.25f||std::abs(info.aspect-camera[7])>.005f)continue;
        result.projection=info;return true;
    }
    return false;
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
bool GameReader::load_hitboxes(uintptr_t model,BoneInfo& info) {
    // Build 6759 CModel::GetHitboxSets (0x21BE930): mesh count +0x70,
    // pointers +0x78, body-group masks +0x90. Each mesh stores 0x48-byte
    // hitbox-set records; CHitBoxSet begins at record +0x18.
    std::array<unsigned char,0x28> header{},after{};
    int mesh_count{};uintptr_t meshes{},masks{};
    if(!read(model+0x70,header))return false;
    std::memcpy(&mesh_count,header.data(),4);std::memcpy(&meshes,header.data()+8,8);std::memcpy(&masks,header.data()+0x20,8);
    if(mesh_count<1||mesh_count>64||!meshes||!masks)return false;
    std::vector<uintptr_t> mesh_array(mesh_count);std::vector<uint64_t> mesh_masks(mesh_count);
    if(!read(meshes,mesh_array.data(),mesh_array.size()*8)||!read(masks,mesh_masks.data(),mesh_masks.size()*8))return false;
    std::vector<BoneInfo::HitboxSet> sets;
    std::vector<uint32_t> set_hashes;
    size_t total_boxes=0;
    for(int mi=0;mi<mesh_count;++mi) {
        if(!mesh_masks[mi]&&mi!=0)continue;
        std::array<unsigned char,24> mesh_header{},mesh_after{};
        int set_count{};uint32_t allocation{};uintptr_t set_array{};
        if(!mesh_array[mi]||!read(mesh_array[mi]+0x160,mesh_header))return false;
        std::memcpy(&allocation,mesh_header.data()+4,4);std::memcpy(&set_array,mesh_header.data()+8,8);std::memcpy(&set_count,mesh_header.data()+20,4);
        allocation&=0x7fffffff;
        if(set_count<0||set_count>64||allocation<uint32_t(set_count)||allocation>1024||(!set_array&&set_count))return false;
        std::vector<unsigned char> records(size_t(set_count)*0x48);
        if(set_count&&!read(set_array,records.data(),records.size()))return false;
        for(int si=0;si<set_count;++si) {
            const auto record=records.data()+size_t(si)*0x48;
            BoneInfo::HitboxSet set;set.mesh_mask=mesh_masks[mi];std::memcpy(&set.hash,record+0x20,4);
            // CSkeletonInstance resolves m_nHitboxSet against mesh zero's
            // set names (0x21CE7A0 -> 0x21BE8D0), then filters by name hash.
            if(mi==0)set_hashes.push_back(set.hash);
            // The game deduplicates sets by name hash after applying the mesh
            // mask. Retain the first copy for each mask/hash combination.
            if(std::any_of(sets.begin(),sets.end(),[&](const auto& cached){return cached.hash==set.hash&&cached.mesh_mask==set.mesh_mask;}))continue;
            int count{};uint32_t capacity{};uintptr_t boxes{};
            std::memcpy(&count,record+0x28,4);std::memcpy(&boxes,record+0x30,8);std::memcpy(&capacity,record+0x38,4);capacity&=0x3fffffff;
            if(count<0||count>128||capacity<uint32_t(count)||capacity>1024||(!boxes&&count)||sets.size()>=256||total_boxes+size_t(count)>2048)return false;
            std::vector<unsigned char> data(size_t(count)*0x70);
            if(count&&!read(boxes,data.data(),data.size()))return false;
            set.boxes.reserve(count);
            for(int bi=0;bi<count;++bi) {
                const auto raw=data.data()+size_t(bi)*0x70;ModelHitbox box;uintptr_t bone_name{};
                std::memcpy(&bone_name,raw+0x10,8);auto name=text(bone_name,100);
                if(bone_name&&name.empty()){char first{};if(!read(bone_name,first)||first)return false;}
                if(!name.empty()) {
                    auto bone=std::find(info.names.begin(),info.names.end(),name);
                    // Match the model's own bone names, never skeleton-list indices.
                    // GetHitboxTransforms uses the scene transform if the
                    // model has no corresponding bone (0x162278A).
                    if(bone!=info.names.end())box.bone=int(bone-info.names.begin());
                }
                std::memcpy(&box.low,raw+0x18,12);std::memcpy(&box.high,raw+0x24,12);
                std::memcpy(&box.radius,raw+0x30,4);std::memcpy(&box.group,raw+0x38,4);
                box.shape=static_cast<HitboxShape>(raw[0x3c]);box.translation_only=raw[0x3d]!=0;
                if(!valid_hitbox(box))return false;
                set.boxes.push_back(box);
            }
            std::array<unsigned char,0x30> set_after{};
            if(!read(set_array+size_t(si)*0x48+0x18,set_after)||std::memcmp(record+0x18,set_after.data(),set_after.size()))return false;
            total_boxes+=set.boxes.size();sets.push_back(std::move(set));
        }
        if(!read(mesh_array[mi]+0x160,mesh_after)||mesh_after!=mesh_header)return false;
    }
    if(!read(model+0x70,after)||after!=header||sets.empty())return false;
    info.hitbox_sets=std::move(sets);info.hitbox_hashes=std::move(set_hashes);info.hitboxes_loaded=true;return true;
}
bool GameReader::anchor(uintptr_t scene, Player& player,bool skeleton,bool hitboxes,uint32_t disabled_groups) {
    auto fail=[&](const char* reason){player.anchor_status=reason;return false;};
    std::array<unsigned char,0x28> model_state{};
    if(!read(scene+0x1c0,model_state))return fail("Cannot read model state");
    uintptr_t binding{},transforms{};
    std::memcpy(&transforms,model_state.data(),8);std::memcpy(&binding,model_state.data()+0x20,8);
    auto model=pointer(binding);
    if (!model || !transforms) return fail("Model or transforms unavailable");
    auto found=models_.find(model);
    if (found==models_.end()) {
        if(models_.size()>=256)models_.clear();
        BoneInfo info; if (!read(model+0x178,info.count) || info.count<1 || info.count>1024) return fail("Invalid model bone count");
        auto names=pointer(model+0x168); if (!names) return fail("Bone names unavailable");
        std::vector<uintptr_t> pointers(info.count);
        std::vector<std::string> bone_names(info.count);
        if (!read(names,pointers.data(),pointers.size()*8)) return fail("Cannot read bone name table");
        for (int i=0;i<info.count;++i) {
            bone_names[i]=text(pointers[i]);
            if(bone_names[i].empty()) {
                char first{};
                if(!read(pointers[i],first)||first)return fail("Incomplete bone names; retrying");
            }
        }
        std::vector<int16_t> parents(info.count);auto parent_array=pointer(model+0x180);
        if(!parent_array||!read(parent_array,parents.data(),parents.size()*sizeof(int16_t)))return fail("Bone parents unavailable; retrying");
        auto rig=resolve_bone_rig(bone_names,parents);
        info.index=rig.anchor();info.dots={rig.head,rig.body,rig.pelvis};info.rig=rig.prefix;
        if(info.index>=0)info.name=bone_names[info.index];
        else info.error=rig.ambiguous?"Ambiguous player skeleton rig":"No supported head bone";
        info.edges=skeleton_edges(rig_skeleton_names(bone_names,rig),parents);
        info.model_name=text(pointer(model+8),160);
        // Hitbox definitions must still bind against the original bone names.
        info.names=std::move(bone_names);
        found=models_.emplace(model,std::move(info)).first;
    }
    auto& info=found->second; int16_t count{};
    player.model_name=info.model_name;player.bone_rig=info.rig;
    if(info.index<0)return fail(info.error.c_str());
    if(!read(scene+0x3e0,count)||count!=info.count||info.index>=count)return fail("Model and live bone counts differ");
    const BoneInfo::HitboxSet* hitbox_set=nullptr;uint64_t body_mask{};uint8_t set_index{};
    if(hitboxes) {
        auto now=GetTickCount64();
        if(!info.hitboxes_loaded&&now>=info.next_hitbox_load) {info.next_hitbox_load=now+1000;load_hitboxes(model,info);}
        if(info.hitboxes_loaded&&read(scene+0x348,body_mask)&&read(scene+0x40c,set_index)) {
            if(set_index<info.hitbox_hashes.size())for(const auto& set:info.hitbox_sets)
                if((set.mesh_mask&body_mask)&&set.hash==info.hitbox_hashes[set_index]){hitbox_set=&set;break;}
            player.hitbox_set=set_index;
            player.hitbox_status=hitbox_set?"Ready":"Active hitbox set unavailable";
        } else player.hitbox_status="Model hitboxes unavailable";
    }
    int first=info.index,last=info.index;
    for(auto index:info.dots)if(index>=0&&index<count){first=std::min(first,index);last=std::max(last,index);}
    if(skeleton)for(auto [a,b]:info.edges){first=std::min(first,std::min(a,b));last=std::max(last,std::max(a,b));}
    if(hitbox_set)for(const auto& box:hitbox_set->boxes)if(box.bone>=0){first=std::min(first,box.bone);last=std::max(last,box.bone);}
    std::array<unsigned char,1024*0x20> positions;
    if(!read(transforms+first*0x20,positions.data(),size_t(last-first)*0x20+(hitbox_set?0x20:sizeof(Vec3))))return fail("Cannot read bone transforms");
    auto position=[&](int index,Vec3& out){if(index<first||index>last)return false;std::memcpy(&out,positions.data()+(index-first)*0x20,sizeof(out));return finite(out);};
    if(!position(info.index,player.head))return fail("Invalid head transform");
    for(size_t i=0;i<info.dots.size();++i)player.dot_valid[i]=position(info.dots[i],player.dots[i]);
    if(skeleton)for(auto [a,b]:info.edges) {
        SkeletonSegment segment;
        if(position(a,segment.a)&&position(b,segment.b)&&dot(sub(segment.a,segment.b),sub(segment.a,segment.b))<512.f*512.f)player.skeleton.push_back(segment);
    }
    if(hitbox_set) {
        std::array<float,8> root{};bool root_valid=false;
        if(std::any_of(hitbox_set->boxes.begin(),hitbox_set->boxes.end(),[](const auto& box){return box.bone<0;}))root_valid=read(scene+0x10,root);
        player.hitboxes.reserve(hitbox_set->boxes.size());
        for(const auto& box:hitbox_set->boxes) {
            if(disabled_groups&(1u<<box.group))continue;
            std::array<float,8> transform{};Hitbox world;
            if(box.bone<0){if(!root_valid)continue;transform=root;}
            else std::memcpy(transform.data(),positions.data()+size_t(box.bone-first)*0x20,0x20);
            if(world_hitbox(box,transform,world))player.hitboxes.push_back(world);
        }
        uint64_t mask_after{};uint8_t set_after{};
        if(!read(scene+0x348,mask_after)||!read(scene+0x40c,set_after)||mask_after!=body_mask||set_after!=set_index) {
            player.hitboxes.clear();player.hitbox_status="Hitbox set changed during sample";
        }
    }
    // Do not publish a transform if the model/array changed during the sample.
    std::array<unsigned char,0x28> current_state{};
    if(!read(scene+0x1c0,current_state))return fail("Cannot recheck model state");
    uintptr_t current_binding{},current_transforms{};
    std::memcpy(&current_transforms,current_state.data(),8);std::memcpy(&current_binding,current_state.data()+0x20,8);
    if(current_binding!=binding||current_transforms!=transforms||pointer(binding)!=model)return fail("Model changed during sample");
    player.anchor=info.name;player.anchor_status="Ready";return true;
}
bool GameReader::extra_anchor(const Entity& object,uintptr_t scene,FocusTarget& target) {
    if(object.kind==TargetKind::Minion) {
        Player bones;uint32_t disabled_groups{};
        bool hitboxes=options_.hitboxes&&read(object.address+0xb98,disabled_groups);
        if(anchor(scene,bones,false,hitboxes,disabled_groups)&&bones.dot_valid[0]&&bones.dot_valid[2]) {
            if(hitboxes){uint32_t after{};if(!read(object.address+0xb98,after)||after!=disabled_groups)bones.hitboxes.clear();}
            if(!bones.dot_valid[1]){bones.dots[1]=mul(add(bones.dots[0],bones.dots[2]),.5f);bones.dot_valid[1]=true;}
            target.hitboxes=std::move(bones.hitboxes);
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
    if(object.kind==TargetKind::Minion&&options_.hitboxes&&bounds_valid) {
        ModelHitbox model{bounds[0],bounds[1],0,-1,0,HitboxShape::Box};Hitbox box;
        if(world_hitbox(model,world_transform,box))target.hitboxes.push_back(box);
    }
    return true;
}
HeroProfile GameReader::hero_profile(const std::array<uintptr_t,64>& chunks,const Entity& pawn) {
    HeroProfile result;
    if(type(pawn.address)!=".?AVC_CitadelPlayerPawn@@")return result;
    // CCitadelHeroComponent::GetHeroData, client+0x767600: spawned, loading,
    // then no-spawn ID. CitadelHeroSpawnData_t has a vtable before its ID.
    std::array<unsigned char,0x40> component{},again{};
    if(!read(pawn.address+0x1620,component))return result;
    for(auto offset:{0x20,0x30,0x38}) {
        int id{};std::memcpy(&id,component.data()+offset,4);
        if(id){result.id=id;break;}
    }
    std::array<uintptr_t,2> table{}; // count/padding and data; cache capacity follows.
    if(result.id<=0||!read(base_+0x36eded8,table))return result;
    auto count=uint32_t(table[0]);auto array=table[1];
    if(count>1024||uint32_t(result.id)>=count||!array)return result;
    auto record=pointer(array+size_t(result.id)*8);int record_id{};
    if(!record||!read(record+0x28,record_id)||record_id!=result.id)return result;
    auto cached=heroes_.find(result.id);
    if(cached==heroes_.end()||cached->second.address!=record) {
        if(heroes_.size()>=256)heroes_.clear();
        auto sort=text(pointer(record+0x30),100);auto token=hero_token(sort);
        if(token.empty())token=hero_token(text(pointer(record+0x38),100));
        if(token.empty())return result;
        auto label=hero_names_.find(token);
        if(label==hero_names_.end())label=hero_names_.find(token+"_sort");
        if(label==hero_names_.end())label=hero_names_.find(token+"_search");
        auto name=label!=hero_names_.end()?label->second:"Hero "+std::to_string(result.id);
        cached=heroes_.insert_or_assign(result.id,HeroMetadata{record,std::move(name),std::move(token)}).first;
    }
    result.name=cached->second.name;result.token=cached->second.token;result.valid=true;
    result.ability_status="No hero-specific focus profile";
    if(vindicta(result)) {
        result.ability_status="Waiting for Assassinate";
        auto abilities=pawn.address+0x1440+0x68;
        std::array<unsigned char,24> header{},header_after{};
        int size{};uint32_t capacity{};uintptr_t data{};
        if(read(abilities,header)) {
            std::memcpy(&size,header.data(),4);std::memcpy(&data,header.data()+8,8);std::memcpy(&capacity,header.data()+16,4);capacity&=0x7fffffff;
            if(size>0&&size<=128&&capacity>=uint32_t(size)&&capacity<=128&&data) {
                std::vector<uint32_t> handles(size);
                if(read(data,handles.data(),handles.size()*4)&&read(abilities,header_after)&&header==header_after) {
                    if(ability_pawn_!=pawn.address||handles!=ability_handles_) {
                        ability_pawn_=pawn.address;ability_handles_=handles;sniper_handle_=UINT32_MAX;
                        for(auto handle:handles) {
                            auto ability=entity(chunks,handle);
                            if(ability.address&&type(ability.address)==".?AVCCitadel_Ability_Hornet_Snipe@@") {sniper_handle_=handle;break;}
                        }
                    }
                    auto sniper=entity(chunks,sniper_handle_);
                    if(sniper.address&&type(sniper.address)==".?AVCCitadel_Ability_Hornet_Snipe@@") {
                        result.sniper_present=true;result.sniper_handle=sniper_handle_;
                        // The actual game tests != 0 here and clears it on unscope:
                        // scope client+0xFB5DB0, unscope client+0xFD1FF0.
                        float after{};
                        if(read(sniper.address+0x1fe4,result.scope_start)&&std::isfinite(result.scope_start)&&result.scope_start>=0
                            &&read(sniper.address+0x1fe4,after)&&after==result.scope_start
                            &&entity(chunks,sniper_handle_).address==sniper.address) {
                            result.sniper_valid=true;result.sniper_scoped=sniper_scope(result.scope_start);
                            result.ability_status=result.sniper_scoped?"Assassinate scoped":"Assassinate idle";
                        }else result.ability_status="Waiting for stable sniper state";
                    }
                }
            }
        }
    }else {ability_pawn_=0;sniper_handle_=UINT32_MAX;ability_handles_.clear();}
    std::array<uintptr_t,2> table_after{};
    if(!read(pawn.address+0x1620,again)||again!=component||!read(base_+0x36eded8,table_after)||table_after!=table
        ||pointer(array+size_t(result.id)*8)!=record||entity(chunks,pawn.handle).address!=pawn.address)return {};
    return result;
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
    auto primary=pointer(base_+0x34883a8);uintptr_t info{};
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
    // Build 6759 EModifierValue: bonus bullet speed=171, base override=172.
    // Aggregate reader client+0x129EF30 uses +0x210 version, +0x214 dirty
    // words and 0x30-byte mirrors beginning at +0x400.
    constexpr uint32_t speed_index=171,version_offset=0x210,dirty_offset=0x214,cache_offset=0x400;
    auto prop=pointer(pawn.address+0x348);uint32_t version_before{},version_after{};
    std::array<uint8_t,2> groups{};
    if(!prop||!read(prop+version_offset,version_before)||!read(prop+160+speed_index,groups))return result;
    if(groups[1]!=255){result.status="Weapon speed override active";return result;}
    if(groups[0]!=255){
        result.status="Waiting for fresh bullet-speed modifier";
        uint8_t policy{},fallback{};uint16_t dirty{};
        // The engine writes this mirror even when its cache-read optimization is disabled.
        if(!read(base_+0x3bee0c0+speed_index,policy)||!read(base_+0x3bee1bc,fallback)||!read(prop+dirty_offset+2*speed_index,dirty))return result;
        if(!policy)policy=fallback;
        std::array<uint32_t,12> cache{},again{};
        if(!read(prop+cache_offset+48*speed_index,cache))return result;
        float bonus{};std::memcpy(&bonus,&cache[8],4);uint32_t tick{};
        if(policy==4){auto globals=pointer(base_+0x32c0670);if(!globals||!read(globals+68,tick))return result;}
        if(!read(prop+version_offset,version_after)||!read(prop+cache_offset+48*speed_index,again)||cache!=again
            ||!valid_modifier_cache(cache[0],cache[2],version_before,version_after,policy,cache[3],tick,dirty,cache[4],bonus,cache[1]))return result;
        result.bonus_percent=cache[4]?bonus:0;
    }
    if(!read(prop+version_offset,version_after)||version_before!=version_after||pointer(pawn.address+0x348)!=prop
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
    auto rules=pointer(base_+0x3c7aee0);std::array<int,2> modes{};
    if(rules&&pointer(rules)==base_+0x26be898&&read(rules+0xa8,modes)&&pointer(base_+0x3c7aee0)==rules) {
        result.match_mode=modes[0];result.game_mode=modes[1];
        // Verified mode enum: game 3=Sandbox; match 3=CoopBot.
        result.practice=(modes[1]==3||modes[0]==3);
        auto testing=pointer(base_+0x36cddc0);uint8_t hero_testing{};
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
    if (!system || !read_projection(result)) {result.status="Waiting for a stable render view";return result;}
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
    if(!read_projection(result)){result.status="Waiting for a stable render view";return result;}
    result.camera_valid=camera_origin(result.matrix,result.camera_position);
    if (now>=next_enumerate_) {if (!enumerate(system)) {result.status="Cannot read entity list";return result;} next_enumerate_=now+((options.minions||options.orbs)?100:1000);}
    result.controllers=static_cast<int>(controllers_.size());
    std::array<uintptr_t,64> chunks{};
    if(!read(system+0x10,chunks)){result.status="Cannot read current entity chunks";return result;}
    for (const auto& controller:controllers_) {
        uint32_t handle=UINT32_MAX;
        auto skip=[&](std::string_view reason,std::string_view model={}) {
            result.skipped_players.push_back({controller.handle,handle,std::string(reason),std::string(model)});
        };
        auto current=entity(chunks,controller.handle);
        if (current.address!=controller.address) {++result.invalid_handles;next_enumerate_=0;skip("Controller identity changed");continue;}
        std::array<unsigned char,0xd5> controller_fields{};
        if(!read(controller.address+0x6bc,controller_fields)){skip("Cannot read controller fields");continue;}
        std::memcpy(&handle,controller_fields.data(),4);
        auto pawn=entity(chunks,handle);if (!pawn.address) {++result.invalid_handles;skip("Controller has no valid pawn");continue;}
        if(controller_fields[0xd4]) {
            result.hero=hero_profile(chunks,pawn);
            result.weapon=weapon_profile(chunks,pawn);
            uint8_t team{};
            if(read(pawn.address+0x3ef,team)&&entity(chunks,handle).address==pawn.address)result.local_team=team;
            Player local;auto scene=pointer(pawn.address+0x330);
            if(local_handle_!=handle){local_motion_={};local_handle_=handle;}
            if(scene&&anchor(scene,local)&&local.dot_valid[2]&&entity(chunks,handle).address==pawn.address){
                local_motion_.update(local.dots[2],now);result.local_velocity=local_motion_.velocity;result.local_velocity_valid=local_motion_.valid;
            }else local_motion_={};
            uint32_t current_handle{};
            if(!read(controller.address+0x6bc,current_handle)||current_handle!=handle){result.hero={};result.weapon={};result.local_velocity_valid=false;local_motion_={};}
            skip("Local player");
            continue;
        }
        auto pawn_type=type(pawn.address);
        if (pawn_type.find("CitadelPlayerPawn")==std::string::npos) {skip(pawn_type.empty()?"Pawn type unavailable":"Unsupported pawn type: "+pawn_type);continue;}
        std::array<unsigned char,0xc0> fields{};
        if (!read(pawn.address+0x330,fields)) {skip("Cannot read pawn fields");continue;}
        uintptr_t scene{}; Player p; p.handle=handle;
        std::memcpy(&scene,fields.data(),8);std::memcpy(&p.maximum,fields.data()+0x20,4);std::memcpy(&p.health,fields.data()+0x24,4);
        p.team=fields[0xbf];uint8_t dormant{};
        if(fields[0x2c]||p.health<=0){skip("Dead player");continue;}
        if(p.maximum<=0){skip("Invalid maximum health");continue;}
        if(!scene){skip("Scene unavailable");continue;}
        if(!read(scene+0x103,dormant)){skip("Cannot read scene state");continue;}
        if(dormant){skip("Dormant player");continue;}
        uint32_t disabled_groups{};
        bool hitboxes=options.hitboxes&&read(pawn.address+0xb98,disabled_groups);
        if (!anchor(scene,p,options.skeletons,hitboxes,disabled_groups)) {++result.missing_anchors;skip(p.anchor_status,p.model_name);continue;}
        if(hitboxes){uint32_t after{};if(!read(pawn.address+0xb98,after)||after!=disabled_groups){p.hitboxes.clear();p.hitbox_status="Hit groups changed during sample";}}
        if (entity(chunks,handle).address!=pawn.address) {skip("Pawn identity changed",p.model_name);continue;}
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
            for(auto& box:player.hitboxes) {
                box.visibility_known=true;box.visible=result.visibility->clear(result.camera_position,hitbox_center(box));
            }
        }
        for(auto& target:result.focus_targets) {
            visible(target);
            for(auto& box:target.hitboxes){box.visibility_known=true;box.visible=result.visibility->clear(result.camera_position,hitbox_center(box));}
        }
    }
    result.visibility_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-ray_started).count();
    result.time=GetTickCount64();result.read_calls=read_calls_;
    result.sample_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();return result;
}
}
