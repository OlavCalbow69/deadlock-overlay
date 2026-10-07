#pragma once
#include <windows.h>

namespace overlay {
enum class FocusHoldButton { SideButtons, LeftMouse };
inline bool focus_button_held(FocusHoldButton button) {
    switch(button) {
    case FocusHoldButton::SideButtons:return ((GetAsyncKeyState(VK_XBUTTON1)|GetAsyncKeyState(VK_XBUTTON2))&0x8000)!=0;
    case FocusHoldButton::LeftMouse:return (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    default:return false;
    }
}
inline const char* focus_button_name(FocusHoldButton button) {
    return button==FocusHoldButton::LeftMouse?"left_mouse":button==FocusHoldButton::SideButtons?"side_buttons":"invalid";
}
inline const char* focus_button_label(FocusHoldButton button) {
    return button==FocusHoldButton::LeftMouse?"left mouse":"either side button";
}
}
