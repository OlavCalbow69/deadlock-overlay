#pragma once
#include <windows.h>

namespace overlay {
// Keep the first two values compatible with existing settings.
enum class FocusHoldButton { SideButtons, LeftMouse, X1, X2 };
inline bool focus_button_held(FocusHoldButton button) {
    switch(button) {
    case FocusHoldButton::SideButtons:return ((GetAsyncKeyState(VK_XBUTTON1)|GetAsyncKeyState(VK_XBUTTON2))&0x8000)!=0;
    case FocusHoldButton::LeftMouse:return (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    case FocusHoldButton::X1:return (GetAsyncKeyState(VK_XBUTTON1)&0x8000)!=0;
    case FocusHoldButton::X2:return (GetAsyncKeyState(VK_XBUTTON2)&0x8000)!=0;
    default:return false;
    }
}
inline const char* focus_button_name(FocusHoldButton button) {
    switch(button) {
    case FocusHoldButton::SideButtons:return "side_buttons";
    case FocusHoldButton::LeftMouse:return "left_mouse";
    case FocusHoldButton::X1:return "x1";
    case FocusHoldButton::X2:return "x2";
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
