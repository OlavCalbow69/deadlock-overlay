#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace overlay {
struct BoneRig {
    int head{-1},head_end{-1},body{-1},pelvis{-1};
    std::string prefix;
    bool ambiguous{};
    int anchor() const {return head_end>=0?head_end:head;}
};
inline BoneRig resolve_bone_rig(const std::vector<std::string>& names,const std::vector<int16_t>& parents) {
    auto named_rig=[&](const std::string& prefix) {
        auto index=[&](std::string_view joint) {
            auto found=std::find(names.begin(),names.end(),prefix+std::string(joint));
            return found==names.end()?-1:int(found-names.begin());
        };
        BoneRig rig;rig.prefix=prefix;rig.head=index("head");rig.head_end=index("head_end");
        rig.body=index("spine_2");if(rig.body<0)rig.body=index("chest");rig.pelvis=index("pelvis");
        return rig;
    };
    auto standard=named_rig("");
    if(standard.anchor()>=0)return standard;
    if(names.empty()||names.size()>1024||names.size()!=parents.size())return {};
    auto descendant=[&](int child,int ancestor) {
        for(size_t steps=0;child>=0&&child<int(parents.size())&&steps<parents.size();++steps) {
            if(child==ancestor)return true;
            child=parents[child];
        }
        return false;
    };
    BoneRig selected;std::vector<std::string> prefixes;
    for(const auto& name:names) {
        size_t suffix=name.ends_with("_head_end")?8:name.ends_with("_head")?4:0;
        if(!suffix)continue;
        auto prefix=name.substr(0,name.size()-suffix);
        if(std::find(prefixes.begin(),prefixes.end(),prefix)!=prefixes.end())continue;
        prefixes.push_back(prefix);
        auto rig=named_rig(prefix);
        // A complete, connected rig prevents mixing a hero with a mount,
        // pet or weapon rig that happens to have similarly named bones.
        if(rig.head<0||rig.body<0||rig.pelvis<0
            ||!descendant(rig.body,rig.pelvis)||!descendant(rig.head,rig.body))continue;
        if(rig.head_end>=0&&!descendant(rig.head_end,rig.head))rig.head_end=-1;
        if(selected.anchor()>=0){BoneRig invalid;invalid.ambiguous=true;return invalid;}
        selected=std::move(rig);
    }
    return selected;
}
inline std::vector<std::string> rig_skeleton_names(const std::vector<std::string>& names,const BoneRig& rig) {
    if(rig.prefix.empty())return names;
    std::vector<std::string> result(names.size());
    for(size_t i=0;i<names.size();++i)if(names[i].starts_with(rig.prefix))result[i]=names[i].substr(rig.prefix.size());
    return result;
}
}
