#pragma once
#include <windows.h>
#include <atomic>
#include <array>

namespace overlay {
inline bool gameplay_cursor_hidden() {
    CURSORINFO cursor{sizeof(CURSORINFO)};
    return GetCursorInfo(&cursor)&&!(cursor.flags&CURSOR_SHOWING);
}
// Keep the first two values compatible with existing settings.
enum class FocusHoldButton { SideButtons, LeftMouse, X1, X2, Lease };
inline std::atomic<uint64_t> focus_lease_until{};
inline std::atomic<int> focus_lease_key{},focus_lease_second{},focus_lease_button{};
inline std::atomic_bool focus_lease_toggle{};
inline std::array<std::atomic<uint64_t>,256> owned_input_until{};
inline bool key_program_owned(int key){return key>0&&key<256&&GetTickCount64()<=owned_input_until[key].load();}
inline bool focus_button_owned(FocusHoldButton button){
    return button==FocusHoldButton::SideButtons?key_program_owned(VK_XBUTTON1)||key_program_owned(VK_XBUTTON2):
        key_program_owned(button==FocusHoldButton::LeftMouse?VK_LBUTTON:button==FocusHoldButton::X1?VK_XBUTTON1:button==FocusHoldButton::X2?VK_XBUTTON2:0);
}
inline bool physical_key_down(int key){return key>0&&key<256&&GetTickCount64()>owned_input_until[key].load()&&(GetAsyncKeyState(key)&0x8000)!=0;}
inline void mark_owned_key(int key,uint64_t until){
    if(key>0&&key<256)owned_input_until[key]=until;
    for(auto pair:{std::array<int,3>{VK_SHIFT,VK_LSHIFT,VK_RSHIFT},std::array<int,3>{VK_CONTROL,VK_LCONTROL,VK_RCONTROL},std::array<int,3>{VK_MENU,VK_LMENU,VK_RMENU}})
        if(key==pair[0]||key==pair[1]||key==pair[2])for(int equivalent:pair)owned_input_until[equivalent]=until;
}

inline bool focus_button_held(FocusHoldButton button) {
    if(button==FocusHoldButton::Lease) {
        if(GetTickCount64()>focus_lease_until.load())return false;
        if(focus_lease_toggle.load())return true;
        return (focus_lease_key.load()?physical_key_down(focus_lease_key):focus_button_held(static_cast<FocusHoldButton>(focus_lease_button.load())))
            ||physical_key_down(focus_lease_second.load());
    }
    switch(button) {
    case FocusHoldButton::SideButtons:return physical_key_down(VK_XBUTTON1)||physical_key_down(VK_XBUTTON2);
    case FocusHoldButton::LeftMouse:return physical_key_down(VK_LBUTTON);
    case FocusHoldButton::X1:return physical_key_down(VK_XBUTTON1);
    case FocusHoldButton::X2:return physical_key_down(VK_XBUTTON2);
    default:return false;
    }
}
inline const char* focus_button_name(FocusHoldButton button) {
    switch(button) {
    case FocusHoldButton::SideButtons:return "side_buttons";
    case FocusHoldButton::LeftMouse:return "left_mouse";
    case FocusHoldButton::X1:return "x1";
    case FocusHoldButton::X2:return "x2";
    case FocusHoldButton::Lease:return "extended_activation";
    default:return "invalid";
    }
}
inline const char* focus_button_label(FocusHoldButton button) {
    switch(button) {
    case FocusHoldButton::SideButtons:return "either side button";
    case FocusHoldButton::LeftMouse:return "left mouse";
    case FocusHoldButton::X1:return "X1";
    case FocusHoldButton::X2:return "X2";
    default:return "invalid button";
    }
}
}
