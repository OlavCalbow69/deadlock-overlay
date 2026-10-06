#pragma once
#include <windows.h>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>

namespace overlay {
inline double valid_refresh(double hz) { return std::isfinite(hz)&&hz>=10&&hz<=1000?hz:60.0; }
struct DisplayTiming { double hz{60}; std::string device{"Unknown display"}; };
inline DisplayTiming display_timing(HWND window) {
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    HMONITOR handle=window?MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY):MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
    if(!GetMonitorInfoW(handle,&monitor))return {};
    DisplayTiming result;result.device.clear();for(auto c:monitor.szDevice){if(!c)break;result.device+=char(c);}
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 paths_count{},modes_count{};
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths_count,&modes_count)!=ERROR_SUCCESS)break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(paths_count);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modes_count);
        LONG error=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths_count,paths.data(),&modes_count,modes.data(),nullptr);
        if(error==ERROR_INSUFFICIENT_BUFFER)continue;if(error!=ERROR_SUCCESS)break;
        for(UINT32 i=0;i<paths_count;++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),paths[i].sourceInfo.adapterId,paths[i].sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)==ERROR_SUCCESS&&!wcscmp(source.viewGdiDeviceName,monitor.szDevice)) {
                auto rate=paths[i].targetInfo.refreshRate;
                double hz=rate.Denominator?double(rate.Numerator)/rate.Denominator:0;
                if(hz>=10&&hz<=1000){result.hz=hz;return result;}
            }
        }
        break;
    }
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(EnumDisplaySettingsExW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode,0))result.hz=valid_refresh(mode.dmDisplayFrequency);
    return result;
}
// Absolute deadlines include work time; an overrun starts a fresh schedule without an extra wait.
class FramePacer {
    using Clock=std::chrono::steady_clock;
    Clock::time_point next_{}; double hz_{}; HANDLE timer_{};
public:
    FramePacer() {
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_)timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
    }
    ~FramePacer(){if(timer_)CloseHandle(timer_);}
    FramePacer(const FramePacer&)=delete;
    void wait(double hz) {
        hz=std::isfinite(hz)?std::clamp(hz,1.0,1000.0):60;
        auto now=Clock::now();auto period=std::chrono::nanoseconds(static_cast<int64_t>(1e9/hz));
        if(hz!=hz_||next_==Clock::time_point{}){hz_=hz;next_=now;}
        next_+=period;
        if(next_<=now){next_=now;return;}
        auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(next_-now);
        if(timer_) {
            LARGE_INTEGER due;due.QuadPart=-std::max<int64_t>(1,(remaining.count()+99)/100);
            if(SetWaitableTimerEx(timer_,&due,0,nullptr,nullptr,nullptr,0)) {
                WaitForSingleObject(timer_,static_cast<DWORD>(remaining.count()/1000000+50));return;
            }
        }
        std::this_thread::sleep_until(next_);
    }
};
}
