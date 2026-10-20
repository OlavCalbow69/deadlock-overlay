#pragma once
#include <windows.h>
#include "focus_input.h"
#include "makcu_protocol.h"
#include "calibration_bindings.h"
#include <setupapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace overlay {
inline std::string makcu_move_command(long x,long y) {
    if((!x&&!y)||x<-32768||x>32767||y<-32768||y>32767)return {};
    char command[64]{};snprintf(command,sizeof(command),"km.move(%ld,%ld)\r\n",x,y);return command;
}
// Match an identity result line, never an echoed query or a substring in noise.
inline bool makcu_identity(const std::string& response) {
    size_t begin=0;
    while(begin<response.size()) {
        auto end=response.find_first_of("\r\n",begin);
        auto line=response.substr(begin,end==std::string::npos?end:end-begin);
        if(line=="km.MAKCU"||line.starts_with("km.MAKCU_"))return true;
        if(end==std::string::npos)break;begin=end+1;
    }return false;
}
struct MakcuPending {
    HWND window{};std::string command;uint64_t created{},generation{};
    FocusHoldButton hold_button{FocusHoldButton::SideButtons};
    double measured_ms{};
    uint64_t calibration{};
};
struct MakcuCalibrationControl {CalibrationBinding binding;bool down{};uint64_t session{},created{},id{};};
struct MakcuClickPending {
    HWND window{};uint64_t created{},generation{},valid_until{};unsigned interval_ms{150};
    bool hold_side{true};
};
struct MakcuParryPending {
    HWND window{};CalibrationBinding binding;uint64_t created{},generation{},valid_until{},id{};bool hold_side{};
};
inline bool makcu_fresh(uint64_t now,uint64_t created) {return created&&now>=created&&now-created<=20;}
class MakcuInput {
    static double milliseconds(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    std::atomic_bool stopping_{false},connected_{false},running_{false};
    std::atomic<uint64_t> backend_generation_{0},windows_moves_{0},windows_failures_{0};
    std::atomic<uint64_t> windows_clicks_{0},windows_parries_{0},windows_releases_{0};
    std::atomic<DWORD> windows_error_{0};std::atomic_bool windows_rejected_{false};
    std::atomic<unsigned> windows_owned_{0};std::atomic_bool windows_cleanup_{false};
    std::atomic<uint64_t> generation_{0},sent_{0},failures_{0},replaced_{0};
    std::atomic<uint64_t> click_generation_{0},clicks_{0},last_click_{0};
    std::atomic<uint64_t> parry_generation_{0},parries_{0},last_parry_{0},parry_id_{0},parry_sent_id_{0};
    std::atomic<uint64_t> motion_sent_{0};std::atomic<double> queue_delay_{0};
    std::atomic<uint64_t> calibration_{0},calibration_deadline_{0},control_sent_{0},control_id_{0};
    std::atomic<HWND> calibration_window_{nullptr};std::atomic<unsigned> calibration_owned_{0};
    std::atomic_bool calibration_error_{false};uint64_t next_calibration_{};
    std::mutex mutex_;std::condition_variable wake_,windows_wake_;std::thread worker_,windows_worker_;
    MakcuPending pending_;MakcuClickPending pending_click_;std::string status_{"MAKCU not started"};
    MakcuParryPending pending_parry_;
    std::deque<MakcuCalibrationControl> calibration_controls_;
    enum class WindowsKind : unsigned { Click=1, Parry=2, Calibration=4 };
    struct WindowsHeld {
        INPUT release{};HWND window{};WindowsKind kind{};uint64_t due{},session{};
        CalibrationBinding binding;bool release_now{};
    };
    std::vector<WindowsHeld> windows_held_;
    static bool input_window(HWND window) {
        return window&&IsWindow(window)&&!IsIconic(window)&&GetForegroundWindow()==window;
    }
    bool windows_send(INPUT event) {
        if(event.type==INPUT_MOUSE)event.mi.dwExtraInfo=static_cast<ULONG_PTR>(0x56414E54414D4F56ULL);
        else event.ki.dwExtraInfo=static_cast<ULONG_PTR>(0x56414E54414B4559ULL);
        SetLastError(ERROR_SUCCESS);
        if(SendInput(1,&event,sizeof(event))!=1) {
            windows_error_=GetLastError();windows_rejected_=true;++windows_failures_;return false;
        }
        windows_error_=0;windows_rejected_=false;return true;
    }
    static INPUT windows_binding(HWND game,const CalibrationBinding& binding,bool down) {
        INPUT event{};
        if(binding.mouse) {
            event.type=INPUT_MOUSE;
            switch(binding.code) {
            case 1:event.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;break;
            case 2:event.mi.dwFlags=down?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP;break;
            case 3:event.mi.dwFlags=down?MOUSEEVENTF_MIDDLEDOWN:MOUSEEVENTF_MIDDLEUP;break;
            case 4:case 5:event.mi.dwFlags=down?MOUSEEVENTF_XDOWN:MOUSEEVENTF_XUP;event.mi.mouseData=binding.code==4?XBUTTON1:XBUTTON2;break;
            }
        }else {
            event.type=INPUT_KEYBOARD;
            const auto layout=GetKeyboardLayout(GetWindowThreadProcessId(game,nullptr));
            const auto scan=MapVirtualKeyExW(UINT(binding.vk),MAPVK_VK_TO_VSC_EX,layout);
            // Scan codes preserve the game's physical key bindings. Keep the
            // exact key-up event, including its extended-key flag, for release.
            if(scan) {
                event.ki.wScan=WORD(scan&0xff);event.ki.dwFlags=KEYEVENTF_SCANCODE;
                if((scan&0xff00)==0xe000)event.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
            }else event.ki.wVk=WORD(binding.vk);
            if(!down)event.ki.dwFlags|=KEYEVENTF_KEYUP;
        }
        return event;
    }
    void windows_owned_update() {
        unsigned mask{};bool cleanup=false;
        for(const auto& held:windows_held_){mask|=unsigned(held.kind);cleanup|=held.release_now;}
        windows_owned_=mask;windows_cleanup_=cleanup;
    }
    void windows_release(bool all,uint64_t now) {
        // Called with mutex_ held. Accepted Windows downs are owned until an
        // accepted up, independently of the render loop and serial discovery.
        for(auto it=windows_held_.begin();it!=windows_held_.end();) {
            const bool expired=it->kind==WindowsKind::Calibration
                ?!calibration_valid(it->session,it->window):now>=it->due;
            if(all||it->release_now||expired||!input_window(it->window)||(GetAsyncKeyState(VK_ESCAPE)&0x8000)) {
                it->release_now=true;
                if(windows_send(it->release)){++windows_releases_;it=windows_held_.erase(it);continue;}
            }
            ++it;
        }
        windows_owned_update();
    }
    bool windows_down(HWND game,const CalibrationBinding& binding,WindowsKind kind,uint64_t due=0,uint64_t session=0) {
        if(windows_cleanup_.load()||windows_held_.size()>=8)return false;
        auto down=windows_binding(game,binding,true),up=windows_binding(game,binding,false);
        windows_held_.push_back({up,game,kind,due,session,binding});
        if(!windows_send(down)){windows_held_.pop_back();return false;}
        windows_owned_update();windows_wake_.notify_one();return true;
    }
    bool windows_move(long x,long y) {
        INPUT event{};event.type=INPUT_MOUSE;event.mi.dx=x;event.mi.dy=y;event.mi.dwFlags=MOUSEEVENTF_MOVE;
        if(!windows_send(event))return false;
        ++windows_moves_;++motion_sent_;++sent_;queue_delay_=0;return true;
    }
    void windows_loop() {
        uint64_t shutdown_deadline{};std::unique_lock lock(mutex_);
        for(;;) {
            const auto now=GetTickCount64();
            if(stopping_&&!shutdown_deadline)shutdown_deadline=now+250;
            windows_release(stopping_.load(),now);
            if(stopping_&&(windows_held_.empty()||now>=shutdown_deadline))break;
            if(windows_held_.empty())windows_wake_.wait(lock,[&]{return stopping_.load()||!windows_held_.empty();});
            else windows_wake_.wait_for(lock,std::chrono::milliseconds(2));
        }
    }
    void use_makcu(bool connected) {
        // Serialize backend changes with submission so a correction is never
        // replayed through both transports or left queued across a reconnect.
        std::lock_guard lock(mutex_);
        if(connected_.exchange(connected)==connected)return;
        ++backend_generation_;++generation_;++click_generation_;++parry_generation_;
        pending_={};pending_click_={};pending_parry_={};calibration_=0;queue_delay_=0;windows_error_=0;windows_rejected_=false;
        windows_release(true,GetTickCount64());windows_wake_.notify_one();
    }
    bool calibration_valid(uint64_t token,HWND window)const {
        return token&&token==calibration_.load()&&window==calibration_window_.load()&&GetTickCount64()<=calibration_deadline_.load()
            &&IsWindow(window)&&!IsIconic(window)&&GetForegroundWindow()==window;
    }
    void status(std::string value){std::lock_guard lock(mutex_);status_=std::move(value);}
    static std::vector<std::wstring> ports() {
        wchar_t explicit_port[32]{};
        if(GetEnvironmentVariableW(L"MAKCU_PORT",explicit_port,32)) {
            std::wstring name=explicit_port;
            if(name.size()>3&&name.size()<10&&name.starts_with(L"COM")
                &&std::all_of(name.begin()+3,name.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))return {name};
            return {};
        }
        // Present CH343 management ports only; do not probe unrelated serial equipment.
        const GUID port_class={0x4d36e978,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
        std::vector<std::wstring> found;
        auto devices=SetupDiGetClassDevsW(&port_class,nullptr,nullptr,DIGCF_PRESENT);
        if(devices==INVALID_HANDLE_VALUE)return found;
        SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
        for(DWORD i=0;SetupDiEnumDeviceInfo(devices,i,&info);++i) {
            wchar_t ids[2048]{};
            if(!SetupDiGetDeviceRegistryPropertyW(devices,&info,SPDRP_HARDWAREID,nullptr,
                reinterpret_cast<PBYTE>(ids),sizeof(ids),nullptr))continue;
            std::wstring id=ids;std::transform(id.begin(),id.end(),id.begin(),towupper);
            if(id.find(L"VID_1A86&PID_55D3")==std::wstring::npos)continue;
            auto key=SetupDiOpenDevRegKey(devices,&info,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_READ);
            if(key==INVALID_HANDLE_VALUE)continue;
            wchar_t name[64]{};DWORD bytes=sizeof(name),type{};
            if(RegQueryValueExW(key,L"PortName",nullptr,&type,reinterpret_cast<BYTE*>(name),&bytes)==ERROR_SUCCESS&&type==REG_SZ)
                found.emplace_back(name);
            RegCloseKey(key);
        }
        SetupDiDestroyDeviceInfoList(devices);return found;
    }
    static HANDLE open(const std::wstring& name,DWORD baud) {
        auto handle=CreateFileW((L"\\\\.\\"+name).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return handle;
        DCB dcb{};dcb.DCBlength=sizeof(dcb);
        if(!GetCommState(handle,&dcb)){CloseHandle(handle);return INVALID_HANDLE_VALUE;}
        dcb.BaudRate=baud;dcb.ByteSize=8;dcb.Parity=NOPARITY;dcb.StopBits=ONESTOPBIT;
        dcb.fBinary=TRUE;dcb.fParity=FALSE;dcb.fOutxCtsFlow=dcb.fOutxDsrFlow=FALSE;
        dcb.fDtrControl=DTR_CONTROL_DISABLE;dcb.fRtsControl=RTS_CONTROL_DISABLE;
        dcb.fDsrSensitivity=dcb.fOutX=dcb.fInX=dcb.fNull=dcb.fAbortOnError=FALSE;
        COMMTIMEOUTS timeouts{};timeouts.ReadIntervalTimeout=MAXDWORD;timeouts.WriteTotalTimeoutConstant=8;
        if(!SetCommState(handle,&dcb)||!SetCommTimeouts(handle,&timeouts)||!SetupComm(handle,4096,4096)) {
            CloseHandle(handle);return INVALID_HANDLE_VALUE;
        }
        PurgeComm(handle,PURGE_RXCLEAR|PURGE_TXCLEAR);return handle;
    }
    static bool write(HANDLE handle,const std::string& command) {
        DWORD written{};
        // A partial command must never be followed by another command on this connection.
        return WriteFile(handle,command.data(),static_cast<DWORD>(command.size()),&written,nullptr)&&written==command.size();
    }
    static bool read(HANDLE handle,std::string& buffer) {
        char bytes[512]{};DWORD got{};
        if(!ReadFile(handle,bytes,sizeof(bytes),&got,nullptr))return false;
        buffer.append(bytes,got);
        if(buffer.size()>8192)return false;
        return true;
    }
    void loop() {
        // Keep failed releases across a cable interruption. Reconnect performs
        // cleanup before advertising readiness or accepting any new motion.
        std::vector<CalibrationBinding> owned;uint64_t owned_session{};
        while(!stopping_) {
            HANDLE handle=INVALID_HANDLE_VALUE;std::wstring selected;DWORD selected_baud{};
            status("Waiting for MAKCU management USB / available COM port");
            for(const auto& port:ports()) {
                for(DWORD baud:{4000000UL,115200UL}) {
                    if(stopping_)break;
                    auto candidate=open(port,baud);if(candidate==INVALID_HANDLE_VALUE)continue;
                    bool ok=write(candidate,"\r\nkm.version()\r\n");std::string reply,raw;MakcuMixedParser handshake;
                    auto deadline=GetTickCount64()+300;
                    while(ok&&!stopping_&&GetTickCount64()<deadline) {
                        ok=read(candidate,raw);
                        if(ok)ok=handshake.feed(raw,[](uint8_t,std::span<const uint8_t>){},[&](std::string_view text){reply.append(text);});raw.clear();
                        if(reply.find(">>> ")!=std::string::npos)break;
                        std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopping_.load();});
                    }
                    if(ok&&makcu_identity(reply)&&reply.find(">>> ")!=std::string::npos
                        &&write(candidate,"km.echo(0)\r\n")) {
                        handle=candidate;selected=port;selected_baud=baud;break;
                    }
                    CloseHandle(candidate);
                }
                if(handle!=INVALID_HANDLE_VALUE)break;
            }
            if(handle==INVALID_HANDLE_VALUE) {
                std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::seconds(1),[&]{return stopping_.load();});continue;
            }
            cancel();queue_delay_=0;std::string port_label;
            for(auto c:selected)port_label+=static_cast<char>(c); // COM names are ASCII.
            status("MAKCU "+port_label+" / "+std::to_string(selected_baud)+" baud");
            std::string received;MakcuMixedParser parser;
            uint64_t next_probe=GetTickCount64()+1000,last_identity=GetTickCount64();bool ok=true;
            auto release_owned=[&]() {
                while(!owned.empty()) {
                    if(!write(handle,owned.back().up()))return false;
                    owned.pop_back();calibration_owned_=unsigned(owned.size());
                }
                owned_session=0;return true;
            };
            // Stop any mouse-event stream left by an earlier connection. This
            // worker only consumes command replies and never polls input history.
            if(!release_owned()||!write(handle,mak_frame(0x52,std::array<uint8_t,2>{1,0}))){CloseHandle(handle);continue;}
            use_makcu(true);
            while(ok&&!stopping_) {
                // Drain both wire formats before validating any queued correction.
                std::string raw;
                ok=read(handle,raw);
                if(ok)ok=parser.feed(raw,[](uint8_t,std::span<const uint8_t>){},[&](std::string_view text){received.append(text);});
                if(received.size()>4096)ok=false;
                for(auto end=received.find(">>> ");end!=std::string::npos;end=received.find(">>> ")) {
                    const auto reply=received.substr(0,end);
                    if(makcu_identity(reply))last_identity=GetTickCount64();
                    size_t start=0;
                    while(start<reply.size()) {
                        const auto line_end=reply.find_first_of("\r\n",start);auto line=reply.substr(start,line_end==std::string::npos?line_end:line_end-start);
                        if(line=="ERR"||line.starts_with("ERR ")||line.starts_with("ERR:")){++failures_;calibration_error_=calibration_.load()!=0;ok=false;}
                        if(line_end==std::string::npos)break;start=line_end+1;
                    }
                    received.erase(0,end+4);
                }
                if(!ok)break;
                MakcuPending request;MakcuClickPending click;MakcuCalibrationControl control;MakcuParryPending parry;
                {std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopping_.load()||!pending_.command.empty()||pending_click_.window||pending_parry_.window||!calibration_controls_.empty();});request=std::move(pending_);pending_={};click=pending_click_;pending_click_={};parry=std::move(pending_parry_);pending_parry_={};
                    if(!calibration_controls_.empty()){control=calibration_controls_.front();calibration_controls_.pop_front();}}
                if(stopping_)break;
                const auto active_session=calibration_.load();
                if(active_session&&!calibration_valid(active_session,calibration_window_.load())){calibration_=0;cancel_motion();}
                if(owned_session&&!calibration_valid(owned_session,calibration_window_.load()))ok=release_owned();
                if(control.id&&calibration_valid(control.session,calibration_window_.load())&&makcu_fresh(GetTickCount64(),control.created)) {
                    auto existing=std::find(owned.begin(),owned.end(),control.binding);
                    if(control.down&&existing==owned.end()) {
                        // Record ownership before writing: a partial write still needs release.
                        owned.push_back(control.binding);owned_session=control.session;calibration_owned_=unsigned(owned.size());
                        ok=ok&&write(handle,control.binding.down());
                    }else if(!control.down&&existing!=owned.end()) {
                        ok=ok&&write(handle,control.binding.up());if(ok){owned.erase(existing);calibration_owned_=unsigned(owned.size());}
                    }
                    if(ok){control_sent_=control.id;++sent_;}else {calibration_error_=true;++failures_;}
                }
                // The firmware owns the complete key pulse, including release.
                // Validate again immediately before the write, independently of rendering.
                const auto parry_now=GetTickCount64();
                if(ok&&!calibration_.load()&&parry.window&&parry.id&&parry.binding&&!parry.binding.mouse
                    &&parry.generation==parry_generation_.load()&&makcu_fresh(parry_now,parry.created)
                    &&parry_now<=parry.valid_until&&parry_now>=last_parry_.load()+250
                    &&IsWindow(parry.window)&&!IsIconic(parry.window)&&GetForegroundWindow()==parry.window
                    &&(!parry.hold_side||focus_button_held(FocusHoldButton::SideButtons))
                    &&!(GetAsyncKeyState(parry.binding.vk)&0x8000)&&!(GetAsyncKeyState(VK_ESCAPE)&0x8000)) {
                    std::lock_guard lock(mutex_);
                    if(parry.generation==parry_generation_.load()&&!calibration_.load()&&!windows_cleanup_.load()) {
                        ok=write(handle,"km.press("+std::to_string(parry.binding.code)+",35,0)\r\n");
                        if(ok){last_parry_=GetTickCount64();parry_sent_id_=parry.id;++parries_;++sent_;}else ++failures_;
                    }
                }
                bool moved=false;
                if(ok&&!request.command.empty()&&request.generation==generation_.load()&&makcu_fresh(GetTickCount64(),request.created)
                    &&IsWindow(request.window)&&!IsIconic(request.window)&&GetForegroundWindow()==request.window
                    &&(request.calibration?calibration_valid(request.calibration,request.window):!calibration_.load()&&focus_button_held(request.hold_button))) {
                    const auto delay=std::clamp(milliseconds()-request.measured_ms,0.,20.);
                    {std::lock_guard lock(mutex_);
                        if(request.generation==generation_.load()) {
                            ok=write(handle,request.command);if(ok){++sent_;++motion_sent_;moved=true;queue_delay_=queue_delay_.load()*.9+delay*.1;}
                            else ++failures_;
                        }
                    }
                }
                const uint64_t click_now=GetTickCount64();
                if(ok&&!calibration_.load()&&!moved&&!parry_pulse_active()&&click.window&&click.generation==click_generation_.load()&&makcu_fresh(click_now,click.created)
                    &&click_now<=click.valid_until&&click_now>=last_click_.load()+click.interval_ms
                    &&IsWindow(click.window)&&!IsIconic(click.window)&&GetForegroundWindow()==click.window
                    &&(!click.hold_side||focus_button_held(FocusHoldButton::SideButtons))&&!focus_button_held(FocusHoldButton::LeftMouse)&&!(GetAsyncKeyState(VK_ESCAPE)&0x8000)) {
                    // V4 schedules the complete pulse, including release. Cancellation,
                    // disconnect or shutdown cannot strand a separately queued down event.
                    std::lock_guard lock(mutex_);
                    if(click.generation==click_generation_.load()&&!calibration_.load()&&!windows_cleanup_.load()) {
                        ok=write(handle,"km.click(1,1,35)\r\n");
                        if(ok){last_click_=GetTickCount64();++clicks_;++sent_;}else ++failures_;
                    }
                }
                if(GetTickCount64()>=next_probe){ok=ok&&write(handle,"km.version()\r\n");next_probe=GetTickCount64()+1000;}
                if(GetTickCount64()-last_identity>3000)ok=false;
            }
            release_owned();calibration_=0;use_makcu(false);cancel();
            CloseHandle(handle);
        }
        use_makcu(false);status("Input stopped");
    }
public:
    ~MakcuInput(){stop();}
    void start(){if(!worker_.joinable()){stopping_=false;running_=true;++backend_generation_;windows_held_.reserve(8);
        try{windows_worker_=std::thread([this]{windows_loop();});worker_=std::thread([this]{loop();});}
        catch(...){running_=false;stopping_=true;windows_wake_.notify_all();if(windows_worker_.joinable())windows_worker_.join();throw;}}}
    void stop(){stopping_=true;if(running_.exchange(false))++backend_generation_;end_calibration();cancel();wake_.notify_all();windows_wake_.notify_all();
        if(worker_.joinable())worker_.join();if(windows_worker_.joinable())windows_worker_.join();}
    void cancel_motion(){++generation_;std::lock_guard lock(mutex_);pending_={};}
    void cancel_click(){++click_generation_;std::lock_guard lock(mutex_);pending_click_={};}
    void cancel_parry(){++parry_generation_;std::lock_guard lock(mutex_);pending_parry_={};}
    void cancel(){cancel_motion();cancel_click();cancel_parry();
        std::lock_guard lock(mutex_);windows_release(true,GetTickCount64());windows_wake_.notify_one();}
    uint64_t begin_calibration(HWND game) {
        if(!ready()||!input_window(game)||calibration_.load()||calibration_cleanup())return 0;
        cancel();std::lock_guard lock(mutex_);if(!ready())return 0;
        calibration_window_=game;calibration_error_=false;calibration_deadline_=GetTickCount64()+250;
        calibration_=++next_calibration_;wake_.notify_one();return next_calibration_;
    }
    void keep_calibration(uint64_t token){if(token&&token==calibration_.load())calibration_deadline_=GetTickCount64()+100;}
    void end_calibration(){calibration_=0;cancel();{std::lock_guard lock(mutex_);calibration_controls_.clear();}wake_.notify_one();}
    bool calibration_cleanup()const{return calibration_owned_.load()!=0||(windows_owned_.load()&unsigned(WindowsKind::Calibration))!=0;}
    bool calibration_error()const{return calibration_error_.load();}
    bool calibration_live(uint64_t token)const{return token&&token==calibration_.load();}
    uint64_t calibration_control(uint64_t token,const CalibrationBinding& binding,bool down) {
        if(!binding||!token||!ready())return 0;
        std::lock_guard lock(mutex_);if(!ready()||!calibration_valid(token,calibration_window_.load()))return 0;
        if(!connected_.load()) {
            auto existing=std::find_if(windows_held_.begin(),windows_held_.end(),[&](const auto& held){return held.kind==WindowsKind::Calibration&&held.session==token&&held.binding==binding;});
            if(down&&existing==windows_held_.end()) {
                if((GetAsyncKeyState(binding.vk)&0x8000)||!windows_down(calibration_window_.load(),binding,WindowsKind::Calibration,0,token)){calibration_error_=true;return 0;}
            }else if(!down&&existing!=windows_held_.end()) {
                if(!windows_send(existing->release)){existing->release_now=true;windows_owned_update();calibration_error_=true;windows_wake_.notify_one();return 0;}
                ++windows_releases_;windows_held_.erase(existing);windows_owned_update();
            }
            auto id=++control_id_;control_sent_=id;++sent_;return id;
        }
        if(calibration_controls_.size()>=8)return 0;
        auto id=++control_id_;calibration_controls_.push_back({binding,down,token,GetTickCount64(),id});wake_.notify_one();return id;
    }
    uint64_t control_sent()const{return control_sent_.load();}
    bool calibration_move(HWND game,long x,long y,uint64_t token) {
        auto command=makcu_move_command(x,y);if(command.empty()||!ready())return false;
        {std::lock_guard lock(mutex_);if(!ready()||!calibration_valid(token,game))return false;
            if(!connected_.load())return windows_move(x,y);
            pending_={game,std::move(command),GetTickCount64(),generation_.load(),FocusHoldButton::SideButtons,milliseconds(),token};}
        wake_.notify_one();return true;
    }
    bool move(HWND game,long x,long y,FocusHoldButton hold_button=FocusHoldButton::SideButtons) {
        auto command=makcu_move_command(x,y);if(command.empty()||!ready())return false;
        {std::lock_guard lock(mutex_);
            if(!ready()||!game||calibration_.load()||calibration_owned_.load())return false;
            if(!connected_.load()) {
                // The serial worker may be discovering ports or waiting for a
                // handshake. Submit Windows movement directly, without that wait.
                if(!IsWindow(game)||IsIconic(game)||GetForegroundWindow()!=game
                    ||!focus_button_held(hold_button)||(GetAsyncKeyState(VK_ESCAPE)&0x8000))return false;
                return windows_move(x,y);
            }
            if(!pending_.command.empty())++replaced_;pending_={game,std::move(command),GetTickCount64(),generation_.load(),hold_button,milliseconds()};}
        wake_.notify_one();return true;
    }
    bool click(HWND game,uint64_t valid_until,unsigned interval_ms=150,bool hold_side=true) {
        const uint64_t now=GetTickCount64();interval_ms=std::clamp(interval_ms,80u,1000u);
        if(!ready()||!game||now>valid_until||now<last_click_.load()+interval_ms)return false;
        {std::lock_guard lock(mutex_);const auto write_now=GetTickCount64();
            if(!ready()||calibration_.load()||calibration_owned_.load()||write_now>valid_until||write_now<last_click_.load()+interval_ms)return false;
            if(!connected_.load()) {
                if(!input_window(game)||(hold_side&&!focus_button_held(FocusHoldButton::SideButtons))||focus_button_held(FocusHoldButton::LeftMouse)
                    ||(GetAsyncKeyState(VK_ESCAPE)&0x8000)||parry_pulse_active()||windows_owned_.load())return false;
                CalibrationBinding left{1,true,VK_LBUTTON,"MOUSE1"};
                if(!windows_down(game,left,WindowsKind::Click,write_now+35))return false;
                last_click_=write_now;++windows_clicks_;++clicks_;++sent_;return true;
            }
            pending_click_={game,write_now,click_generation_.load(),valid_until,interval_ms,hold_side};}
        wake_.notify_one();return true;
    }
    uint64_t parry(HWND game,const CalibrationBinding& binding,uint64_t valid_until,bool hold_side) {
        const auto now=GetTickCount64();
        if(!ready()||!game||!binding||binding.mouse||calibration_.load()||now>valid_until||now<last_parry_.load()+250)return 0;
        std::lock_guard lock(mutex_);const auto write_now=GetTickCount64();
        if(!ready()||calibration_.load()||calibration_owned_.load()||write_now>valid_until||write_now<last_parry_.load()+250)return 0;
        if(!connected_.load()) {
            if(!input_window(game)||(hold_side&&!focus_button_held(FocusHoldButton::SideButtons))
                ||(GetAsyncKeyState(binding.vk)&0x8000)||(GetAsyncKeyState(VK_ESCAPE)&0x8000)||(windows_owned_.load()&~unsigned(WindowsKind::Click)))return 0;
            if(!windows_down(game,binding,WindowsKind::Parry,write_now+35))return 0;
            auto id=++parry_id_;last_parry_=write_now;parry_sent_id_=id;++windows_parries_;++parries_;++sent_;return id;
        }
        auto id=++parry_id_;pending_parry_={game,binding,write_now,parry_generation_.load(),valid_until,id,hold_side};wake_.notify_one();return id;
    }
    uint64_t parry_sent_id()const{return parry_sent_id_.load();}
    uint64_t parries()const{return parries_.load();}
    uint64_t last_parry()const{return last_parry_.load();}
    bool parry_pulse_active()const{auto last=last_parry_.load(),now=GetTickCount64();return (windows_owned_.load()&unsigned(WindowsKind::Parry))||(last&&now>=last&&now-last<90);}
    bool hardware_ready()const{return running_.load()&&!stopping_.load()&&connected_.load()&&!windows_cleanup_.load();}
    bool ready()const{return running_.load()&&!stopping_.load()&&!windows_cleanup_.load()&&(connected_.load()||!calibration_owned_.load());}
    const char* backend()const{return !ready()?"stopped":hardware_ready()?"makcu":"winapi";}
    const char* label()const{return !ready()?"Input unavailable":hardware_ready()?"MAKCU ready":"Windows fallback";}
    uint64_t backend_generation()const{return backend_generation_.load();}
    uint64_t windows_moves()const{return windows_moves_.load();}
    uint64_t windows_clicks()const{return windows_clicks_.load();}
    uint64_t windows_parries()const{return windows_parries_.load();}
    uint64_t windows_releases()const{return windows_releases_.load();}
    uint64_t windows_failures()const{return windows_failures_.load();}
    DWORD windows_error()const{return windows_error_.load();}
    std::string status(){std::lock_guard lock(mutex_);
        if(windows_cleanup_.load())return "Releasing Windows input / input rejected (error "+std::to_string(windows_error_.load())+")";
        if(!ready()||hardware_ready())return status_;
        std::string result="Windows input fallback";
        if(windows_rejected_.load())result+=" / input rejected (error "+std::to_string(windows_error_.load())+")";
        return result+" / "+status_;}
    uint64_t sent()const{return sent_.load();}
    uint64_t motion_sent()const{return motion_sent_.load();}
    double queue_delay_ms()const{return std::clamp(queue_delay_.load(),0.,20.);}
    uint64_t failures()const{return failures_.load();}
    uint64_t replaced()const{return replaced_.load();}
    uint64_t clicks()const{return clicks_.load();}
    uint64_t last_click()const{return last_click_.load();}
    bool click_pulse_active()const{auto last=last_click_.load();auto now=GetTickCount64();return (windows_owned_.load()&unsigned(WindowsKind::Click))||(last&&now>=last&&now-last<70);}
};
}
