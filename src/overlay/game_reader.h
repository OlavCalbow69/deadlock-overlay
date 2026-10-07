#pragma once
#include "logic.h"
#include "focus_math.h"
#include "weapon_profile.h"
#include "hero_profile.h"
#include "visibility.h"
#include "hitboxes.h"
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace overlay {
enum class TargetKind { None, Player, Minion, SoulOrb };
inline const char* target_kind_name(TargetKind kind) {
    switch(kind) {case TargetKind::Player:return "player";case TargetKind::Minion:return "minion";case TargetKind::SoulOrb:return "soul_orb";default:return "none";}
}
inline TargetKind extra_target_kind(std::string_view name) {
    if(name==".?AVCItemXP@@")return TargetKind::SoulOrb;
    if(name==".?AVC_NPC_Trooper@@"||name==".?AVC_NPC_TrooperNeutral@@"||name==".?AVC_NPC_TrooperNeutralNodeMover@@")return TargetKind::Minion;
    return TargetKind::None;
}
struct ReadOptions { bool skeletons{},minions{},orbs{},hitboxes{}; };
struct FocusTarget {
    uint32_t handle{};
    TargetKind kind{TargetKind::Player};
    uint8_t team{};
    int health{},maximum{}; // maximum <= 0 means health is unknown (e.g. some soul orbs).
    Vec3 velocity{};bool velocity_valid{};
    std::array<Vec3,3> dots{}; // head, torso, pelvis
    std::array<bool,3> dot_valid{};
    std::array<bool,3> dot_visible{};
    bool visible{};
};
struct SkeletonSegment { Vec3 a{},b{}; bool visible{},visibility_known{}; };
struct Player : FocusTarget {
    Vec3 head{};
    std::vector<SkeletonSegment> skeleton;
    std::vector<Hitbox> hitboxes;
    int hitbox_set{-1};
    std::string hitbox_status;
    std::string anchor;
};
struct Snapshot {
    DWORD pid{};
    uint64_t time{};
    uint8_t local_team{};
    Vec3 local_velocity{};bool local_velocity_valid{};
    WeaponProfile weapon;
    HeroProfile hero;
    bool replay{};
    bool practice{};int match_mode{-1},game_mode{-1};
    std::string map_name;
    std::string level_name,map_source;
    Matrix matrix{};
    ProjectionInfo projection;
    std::shared_ptr<const VisibilityMesh> visibility;
    Vec3 camera_position{};bool camera_valid{};
    std::string visibility_status{"Waiting for map"};
    double visibility_us{};
    std::vector<Player> players;
    std::vector<FocusTarget> focus_targets;
    int controllers{}, invalid_handles{}, missing_anchors{};
    double sample_us{};
    uint64_t read_calls{};
    std::string status{"Waiting for Deadlock"};
};
class GameReader {
public:
    ~GameReader();
    Snapshot sample(ReadOptions options={});
    void refresh_data(){detach();next_attach_=0;}
private:
    HANDLE process_{};
    DWORD pid_{};
    uintptr_t base_{}, entity_slot_{}, matrix_address_{};
    uintptr_t engine_base_{};
    uintptr_t world_renderer_base_{};
    uintptr_t map_array_{},map_rep_{},map_world_{},map_path_{};
    int map_count_{},map_index_{-1};uint64_t next_map_resolve_{};
    std::string loaded_map_,map_resolution_status_;
    uint64_t next_attach_{}, next_enumerate_{};
    struct Entity { uintptr_t address{}; uint32_t handle{}; TargetKind kind{TargetKind::None}; };
    struct BoneInfo {
        int count{}, index{-1}; std::array<int,3> dots{-1,-1,-1};std::string name;
        std::vector<std::pair<int,int>> edges;
        std::vector<std::string> names;
        struct HitboxSet {uint64_t mesh_mask{};uint32_t hash{};std::vector<ModelHitbox> boxes;};
        std::vector<HitboxSet> hitbox_sets;
        std::vector<uint32_t> hitbox_hashes;
        bool hitboxes_loaded{};uint64_t next_hitbox_load{};
    };
    std::unordered_map<uintptr_t, std::string> types_;
    std::unordered_map<uintptr_t, BoneInfo> models_;
    std::unordered_map<std::string,std::string> hero_names_;
    struct HeroMetadata {uintptr_t address{};std::string name,token;};
    std::unordered_map<int,HeroMetadata> heroes_;
    uintptr_t ability_pawn_{};uint32_t sniper_handle_{UINT32_MAX};
    std::vector<uint32_t> ability_handles_;
    std::vector<Entity> controllers_;
    std::vector<Entity> extras_;
    ReadOptions options_;
    std::unordered_map<uint32_t,VelocityEstimate> velocities_;
    VelocityEstimate local_motion_;uint32_t local_handle_{};
    std::string status_;
    std::string visibility_map_,visibility_status_;
    std::shared_ptr<const VisibilityMesh> visibility_;
    uint64_t next_visibility_load_{};
    mutable uint64_t read_calls_{};
    bool read(uintptr_t address, void* data, size_t count) const;
    template<class T> bool read(uintptr_t address, T& value) const {
        return read(address, &value, sizeof(value));
    }
    uintptr_t pointer(uintptr_t address) const;
    std::string text(uintptr_t address, size_t limit=128) const;
    std::string type(uintptr_t entity);
    bool attach();
    bool read_projection(Snapshot& result) const;
    void detach();
    void clear_map_cache();
    std::string loaded_arena(uint64_t now);
    bool enumerate(uintptr_t system);
    Entity entity(const std::array<uintptr_t,64>& chunks, uint32_t handle) const;
    bool load_hitboxes(uintptr_t model,BoneInfo& info);
    bool anchor(uintptr_t scene, Player& player,bool skeleton=false,bool hitboxes=false,uint32_t disabled_groups=0);
    bool extra_anchor(const Entity& object,uintptr_t scene,FocusTarget& target);
    WeaponProfile weapon_profile(const std::array<uintptr_t,64>& chunks,const Entity& pawn);
    HeroProfile hero_profile(const std::array<uintptr_t,64>& chunks,const Entity& pawn);
};
}
