#pragma once
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace overlay {
// DXGI Present::Start event, documented by Intel PresentMon's DXGI provider definitions.
// This observes submissions, not GPU completion or displayed/scanout frames.
class GameFrames {
    inline static constexpr GUID provider_{0xca11c036,0x0102,0x4a2d,{0xa6,0xad,0xf0,0x3c,0xfe,0xd5,0xd3,0xc9}};
    TRACEHANDLE session_{},consumer_{INVALID_PROCESSTRACE_HANDLE};
    std::wstring name_;std::vector<unsigned char> storage_;std::thread thread_;
    HANDLE ready_{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    std::atomic<DWORD> pid_{};std::atomic<int64_t> latest_{};
    std::atomic<uint64_t> count_{};std::atomic<double> delay_ms_{};
    LARGE_INTEGER frequency_{};ULONG error_{ERROR_NOT_READY};
    static void WINAPI event_record(EVENT_RECORD* record) {
        auto self=static_cast<GameFrames*>(record->UserContext);
        // Present(TEST) checks occlusion without submitting a new frame.
        if(record->EventHeader.EventDescriptor.Id==42&&record->UserDataLength>=16){
            uint32_t flags{};std::memcpy(&flags,static_cast<const unsigned char*>(record->UserData)+12,4);
            if(flags&1)return;
        }
        if(self)self->observe(record->EventHeader.ProcessId,record->EventHeader.ProviderId,
            record->EventHeader.EventDescriptor.Id,record->EventHeader.TimeStamp.QuadPart);
    }
public:
    GameFrames(){QueryPerformanceFrequency(&frequency_);}
    ~GameFrames(){stop();if(ready_)CloseHandle(ready_);}
    GameFrames(const GameFrames&)=delete;
    void observe(DWORD pid,const GUID& provider,USHORT id,int64_t timestamp) {
        if(!pid||pid!=pid_.load()||!IsEqualGUID(provider,provider_)||id!=42)return;
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        double delay=double(now.QuadPart-timestamp)*1000/double(frequency_.QuadPart);
        if(delay<0)return;
        delay_ms_=delay;++count_;latest_=timestamp;
        if(delay<=12&&ready_)SetEvent(ready_);
    }
    void target(DWORD pid) {
        if(pid_.exchange(pid)!=pid){latest_=0;count_=0;delay_ms_=0;if(ready_)ResetEvent(ready_);}
    }
    bool start() {
        if(session_)return true;
        name_=L"DeadlockOverlayFrames-"+std::to_wstring(GetCurrentProcessId());
        storage_.resize(sizeof(EVENT_TRACE_PROPERTIES)+(name_.size()+1)*sizeof(wchar_t));
        auto p=reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage_.data());*p={};
        p->Wnode.BufferSize=static_cast<ULONG>(storage_.size());p->Wnode.Flags=WNODE_FLAG_TRACED_GUID;p->Wnode.ClientContext=1;
        p->LogFileMode=EVENT_TRACE_REAL_TIME_MODE;p->BufferSize=4;p->MinimumBuffers=2;p->MaximumBuffers=16;p->FlushTimer=1;
        p->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
        error_=StartTraceW(&session_,name_.c_str(),p);if(error_!=ERROR_SUCCESS){session_=0;return false;}
        error_=EnableTraceEx2(session_,&provider_,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_VERBOSE,0x8000000000000002ULL,0,0,nullptr);
        if(error_!=ERROR_SUCCESS){stop();return false;}
        EVENT_TRACE_LOGFILEW log{};log.LoggerName=name_.data();log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        log.EventRecordCallback=event_record;log.Context=this;consumer_=OpenTraceW(&log);
        if(consumer_==INVALID_PROCESSTRACE_HANDLE){error_=GetLastError();stop();return false;}
        error_=ERROR_SUCCESS;thread_=std::thread([this]{ProcessTrace(&consumer_,1,nullptr,nullptr);});return true;
    }
    void stop() {
        if(session_){ControlTraceW(session_,name_.c_str(),reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage_.data()),EVENT_TRACE_CONTROL_STOP);session_=0;}
        if(consumer_!=INVALID_PROCESSTRACE_HANDLE)CloseTrace(consumer_);
        if(thread_.joinable())thread_.join();consumer_=INVALID_PROCESSTRACE_HANDLE;
    }
    bool fresh() const {
        auto timestamp=latest_.load();if(!timestamp)return false;
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        double age=double(now.QuadPart-timestamp)*1000/double(frequency_.QuadPart);
        return age>=0&&age<=12;
    }
    // Coalesce queued notifications rather than replaying old frames. A timeout lets UI/input proceed.
    bool wait(double hz) {
        if(!session_||!fresh()||!ready_)return false;
        DWORD timeout=static_cast<DWORD>(std::clamp(2000.0/hz,1.0,20.0));
        return WaitForSingleObject(ready_,timeout)==WAIT_OBJECT_0&&fresh();
    }
    std::string status() const {
        if(error_==ERROR_ACCESS_DENIED)return "Refresh pacing (frame events need administrator)";
        if(error_!=ERROR_SUCCESS)return "Refresh pacing (trace error "+std::to_string(error_)+")";
        return fresh()?"Game frame events (experimental)":"Refresh pacing (frame events delayed or absent)";
    }
    ULONG error() const{return error_;}
    uint64_t count() const{return count_.load();}
    double delay_ms() const{return delay_ms_.load();}
    static const GUID& provider(){return provider_;}
};
}
