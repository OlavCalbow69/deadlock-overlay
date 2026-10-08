#pragma once
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

namespace overlay {
// The helper owns background work; rendering never waits for map parsing or dumping.
class DataUpdate {
    HANDLE process_{};
    std::filesystem::path root_, project_, job_;
    uint64_t next_poll_{};
    bool finished_{};
    static std::wstring quote(std::wstring text) {
        // Windows paths cannot contain quotes; trailing backslashes must be doubled.
        std::wstring result=L"\""+text;
        for(auto i=text.size();i>0&&text[i-1]==L'\\';--i)result+=L'\\';
        return result+L"\"";
    }
    void read_status(const std::filesystem::path& path) {
        // Allow the helper's atomic replacement while the UI reads its progress.
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        char buffer[16384]{};DWORD count{};bool ok=ReadFile(file,buffer,sizeof(buffer),&count,nullptr)!=FALSE;CloseHandle(file);
        if(!ok)return;
        std::istringstream input(std::string(buffer,count));std::string line;
        while(std::getline(input,line)) {
            if(line.size()>4096)continue;
            auto at=line.find('=');if(at==std::string::npos)continue;
            auto key=line.substr(0,at),value=line.substr(at+1);
            if(!value.empty()&&value.back()=='\r')value.pop_back();
            if(key=="state")state=value;else if(key=="phase")phase=value;else if(key=="message")message=value;
            else if(key=="progress")try{progress=std::clamp(std::stoi(value),0,100);}catch(...){}
        }
    }
public:
    std::string state="idle",phase="Game data",message="Update maps, schema and the reader profile.";
    int progress{};
    DWORD exit_code{};
    bool running()const{return process_!=nullptr;}
    ~DataUpdate(){cancel();if(process_)CloseHandle(process_);}
    void initialize(const std::filesystem::path& root) {
        root_=root;
        project_=std::filesystem::exists(root/L"CMakeLists.txt")?root:root.parent_path().parent_path();
        if(!std::filesystem::exists(project_/L"CMakeLists.txt"))project_=root;
        read_status(root_/L"data"/L"last-update.ini");
        if(state=="running"){state="idle";phase="Previous update interrupted";message="Press an update button to try again.";}
    }
    bool start(const wchar_t* mode,HWND owner) {
        if(running())return false;
        const auto tool=root_/L"tools"/L"data-update"/L"DeadlockDataUpdate.exe";
        if(!std::filesystem::exists(tool)){state="failed";phase="Tools missing";message="Build the bundled data tools first.";return false;}
        const bool elevated=wcscmp(mode,L"maps")!=0;
        job_=root_/L"data"/L"updates"/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId()));
        try{std::filesystem::create_directories(job_);}catch(...){state="failed";message="Cannot create the update report directory.";return false;}
        auto arguments=L"--mode "+std::wstring(mode)+L" --root "+quote(root_.wstring())+L" --project "+quote(project_.wstring())+L" --job "+quote(job_.wstring())+L" --parent "+std::to_wstring(GetCurrentProcessId());
        SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
        info.hwnd=owner;info.lpVerb=elevated?L"runas":L"open";info.lpFile=tool.c_str();info.lpParameters=arguments.c_str();info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info)||!info.hProcess){auto error=GetLastError();state="failed";phase="Update not started";message=error==ERROR_CANCELLED?"Windows administrator approval was cancelled.":"Could not start the updater (Windows error "+std::to_string(error)+").";return false;}
        process_=info.hProcess;state="running";phase="Starting update";message=elevated?"Preparing game data tools...":"Discovering installed maps...";progress=0;next_poll_=0;finished_=false;return true;
    }
    void poll() {
        if(!process_)return;
        auto now=GetTickCount64();if(now<next_poll_)return;next_poll_=now+300;
        read_status(job_/L"status.ini");
        if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0) {
            read_status(job_/L"status.ini");
            if(state=="running"){state="failed";phase="Update interrupted";message="The helper exited before finishing. Open the update report.";}
            GetExitCodeProcess(process_,&exit_code);CloseHandle(process_);process_=nullptr;finished_=true;
        }
    }
    bool consume_finished(){bool value=finished_;finished_=false;return value;}
    void cancel() {
        if(process_&&!job_.empty()){std::ofstream marker(job_/L"cancel");marker<<"cancel\n";message="Cancelling update...";}
    }
    void open_report(HWND owner) const {
        auto report=job_.empty()?root_/L"data"/L"updates":job_;
        ShellExecuteW(owner,L"open",report.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
};
}
