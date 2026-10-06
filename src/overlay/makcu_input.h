#pragma once
#include <windows.h>
#include <setupapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
};
inline bool makcu_fresh(uint64_t now,uint64_t created) {return created&&now>=created&&now-created<=20;}
class MakcuInput {
    std::atomic_bool stopping_{false},connected_{false};
    std::atomic<uint64_t> generation_{0},sent_{0},failures_{0},replaced_{0};
    std::mutex mutex_;std::condition_variable wake_;std::thread worker_;
    MakcuPending pending_;std::string status_{"MAKCU not started"};
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
        if(buffer.size()>4096)buffer.erase(0,buffer.size()-4096);
        return true;
    }
    void loop() {
        while(!stopping_) {
            HANDLE handle=INVALID_HANDLE_VALUE;std::wstring selected;DWORD selected_baud{};
            status("Waiting for MAKCU management USB / available COM port");
            for(const auto& port:ports()) {
                for(DWORD baud:{4000000UL,115200UL}) {
                    if(stopping_)break;
                    auto candidate=open(port,baud);if(candidate==INVALID_HANDLE_VALUE)continue;
                    bool ok=write(candidate,"km.version()\r\n");std::string reply;
                    auto deadline=GetTickCount64()+300;
                    while(ok&&!stopping_&&GetTickCount64()<deadline) {
                        ok=read(candidate,reply);
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
            cancel();std::string port_label;
            for(auto c:selected)port_label+=static_cast<char>(c); // COM names are ASCII.
            status("MAKCU "+port_label+" / "+std::to_string(selected_baud)+" baud");
            connected_=true;
            std::string received;uint64_t next_probe=GetTickCount64()+1000,last_identity=GetTickCount64();bool ok=true;
            while(ok&&!stopping_) {
                MakcuPending request;
                {std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopping_.load()||!pending_.command.empty();});request=std::move(pending_);pending_={};}
                if(stopping_)break;
                if(!request.command.empty()&&request.generation==generation_.load()&&makcu_fresh(GetTickCount64(),request.created)
                    &&IsWindow(request.window)&&!IsIconic(request.window)&&GetForegroundWindow()==request.window
                    &&((GetAsyncKeyState(VK_XBUTTON1)|GetAsyncKeyState(VK_XBUTTON2))&0x8000)) {
                    ok=write(handle,request.command);if(ok)++sent_;else ++failures_;
                }
                if(ok)ok=read(handle,received);
                if(received.find("ERR")!=std::string::npos){++failures_;ok=false;}
                for(auto end=received.find(">>> ");end!=std::string::npos;end=received.find(">>> ")) {
                    if(makcu_identity(received.substr(0,end)))last_identity=GetTickCount64();
                    received.erase(0,end+4);
                }
                if(GetTickCount64()>=next_probe){ok=ok&&write(handle,"km.version()\r\n");next_probe=GetTickCount64()+1000;}
                if(GetTickCount64()-last_identity>3000)ok=false;
            }
            connected_=false;cancel();CloseHandle(handle);
        }
        connected_=false;status("MAKCU stopped");
    }
public:
    ~MakcuInput(){stop();}
    void start(){if(!worker_.joinable()){stopping_=false;worker_=std::thread([this]{loop();});}}
    void stop(){stopping_=true;cancel();wake_.notify_all();if(worker_.joinable())worker_.join();}
    void cancel(){++generation_;std::lock_guard lock(mutex_);pending_={};}
    bool move(HWND game,long x,long y) {
        auto command=makcu_move_command(x,y);if(!connected_||command.empty())return false;
        {std::lock_guard lock(mutex_);if(!pending_.command.empty())++replaced_;pending_={game,std::move(command),GetTickCount64(),generation_.load()};}
        wake_.notify_one();return true;
    }
    bool ready()const{return connected_.load();}
    std::string status(){std::lock_guard lock(mutex_);return status_;}
    uint64_t sent()const{return sent_.load();}
    uint64_t failures()const{return failures_.load();}
    uint64_t replaced()const{return replaced_.load();}
};
}
