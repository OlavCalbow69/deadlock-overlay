#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace overlay {
inline constexpr int default_health_visuals_key='H';
inline bool valid_health_visuals_key(int key) {
    if(key<=0||key>=255||key==VK_ESCAPE||key==VK_INSERT||key==VK_LWIN||key==VK_RWIN
        ||key==VK_PACKET||key==VK_PROCESSKEY)return false;
    if(key==VK_LBUTTON||key==VK_RBUTTON||key==VK_MBUTTON||key==VK_XBUTTON1||key==VK_XBUTTON2)return true;
    return MapVirtualKeyW(UINT(key),MAPVK_VK_TO_VSC_EX)!=0;
}
struct HealthVisualsActivation {
    bool hold_only{};
    int key{default_health_visuals_key};
};
inline void sanitize_health_visuals(HealthVisualsActivation& settings) {
    if(!valid_health_visuals_key(settings.key))settings.key=default_health_visuals_key;
}
inline bool health_visuals_active(const HealthVisualsActivation& settings,bool held) {return !settings.hold_only||held;}
inline bool health_visuals_active(const HealthVisualsActivation& settings) {
    return !settings.hold_only||(valid_health_visuals_key(settings.key)&&(GetAsyncKeyState(settings.key)&0x8000)!=0);
}
inline std::string health_visuals_key_name(int key) {
    switch(key) {
    case VK_LBUTTON:return "Left mouse";case VK_RBUTTON:return "Right mouse";
    case VK_MBUTTON:return "Middle mouse";case VK_XBUTTON1:return "Mouse X1";case VK_XBUTTON2:return "Mouse X2";
    }
    const UINT scan=MapVirtualKeyW(UINT(key),MAPVK_VK_TO_VSC_EX);
    const LONG param=LONG((scan&255)<<16)|((scan&0xff00)?(1L<<24):0);
    wchar_t name[64]{};char utf8[192]{};
    if(GetKeyNameTextW(param,name,64)>0&&WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof(utf8),nullptr,nullptr)>0)return utf8;
    return "Key "+std::to_string(key);
}
class HealthKeyCapture {
    using Keys=std::array<bool,256>;
    Keys previous_{};bool active_{},waiting_release_{},cancel_requested_{};
    static Keys read() {
        Keys keys{};for(int i=1;i<255;++i)keys[i]=(GetAsyncKeyState(i)&0x8000)!=0;return keys;
    }
public:
    void begin(){previous_=read();active_=waiting_release_=true;cancel_requested_=false;}
    bool active()const{return active_;}
    void request_cancel(){cancel_requested_=true;}
    void cancel(){active_=waiting_release_=cancel_requested_=false;}
    // 0 waits; -1 cancels; a positive value is the newly pressed virtual key.
    int poll() {
        if(cancel_requested_){cancel();return -1;}
        if(!active_)return 0;
        const auto keys=read();
        if(keys[VK_ESCAPE]){cancel();return -1;}
        if(waiting_release_) {
            bool held=false;for(int key=1;key<255;++key)held|=keys[key]&&valid_health_visuals_key(key);
            previous_=keys;if(!held)waiting_release_=false;return 0;
        }
        // Prefer the specific left/right modifier over its generic alias.
        for(int key=1;key<255;++key)if(key!=VK_SHIFT&&key!=VK_CONTROL&&key!=VK_MENU
            &&keys[key]&&!previous_[key]&&valid_health_visuals_key(key)){cancel();return key;}
        previous_=keys;return 0;
    }
};
}
