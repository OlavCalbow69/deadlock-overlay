#pragma once
#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <string>

namespace overlay {
struct AutoParrySettings {
    bool enabled{},melee{true},light{true},heavy{true},counterspell{};
    bool observe_only{},show_hud{true},show_threat{true};
    float lead_ms{120};
    std::string key{"F"};
    bool operator==(const AutoParrySettings&) const=default;
};
inline void sanitize_parry_settings(AutoParrySettings& s) {
    s.lead_ms=std::clamp(std::isfinite(s.lead_ms)?s.lead_ms:120.f,30.f,300.f);
    if(s.key.empty()||s.key.size()>16)s.key="F";
}
inline void write_parry_settings(std::ostream& out,const AutoParrySettings& s) {
    out<<"auto_parry="<<s.enabled<<"\nparry_melee="<<s.melee<<"\nparry_light="<<s.light<<"\nparry_heavy="<<s.heavy
        <<"\nparry_counterspell="<<s.counterspell<<"\nparry_observe="<<s.observe_only
        <<"\nparry_hud="<<s.show_hud<<"\nparry_threat="<<s.show_threat<<"\nparry_lead_ms="<<s.lead_ms<<"\nparry_key="<<s.key<<'\n';
}
inline bool read_parry_setting(AutoParrySettings& s,const std::string& key,std::istream& in) {
    bool* value=key=="auto_parry"?&s.enabled:key=="parry_melee"?&s.melee:key=="parry_light"?&s.light:key=="parry_heavy"?&s.heavy
        :key=="parry_counterspell"?&s.counterspell:key=="parry_observe"?&s.observe_only
        :key=="parry_hud"?&s.show_hud:key=="parry_threat"?&s.show_threat:nullptr;
    if(value){int n{};if(in>>n)*value=n!=0;return true;}
    if(key=="parry_lead_ms"){float n{};if(in>>n&&std::isfinite(n))s.lead_ms=n;return true;}
    if(key=="parry_key"){std::string n;if(in>>n&&n.size()<=16)s.key=n;return true;}
    return false;
}
}
