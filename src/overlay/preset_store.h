#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace overlay {
inline bool valid_preset_name(const std::string& name) {
    if(name.empty()||name.size()>32||name.front()==' '||name.back()==' ')return false;
    for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='_'||c=='-'))return false;
    std::string upper=name;for(char& c:upper)if(c>='a'&&c<='z')c-=32;
    return upper!="CON"&&upper!="PRN"&&upper!="AUX"&&upper!="NUL"&&!(upper.size()==4&&(upper.starts_with("COM")||upper.starts_with("LPT"))&&upper[3]>='1'&&upper[3]<='9');
}
inline std::string active_preset(const std::filesystem::path& root) {
    std::ifstream in(root/L"active-preset.txt");std::string name;std::getline(in,name);return valid_preset_name(name)?name:"";
}
inline bool remember_preset(const std::filesystem::path& root,const std::string& name) {
    if(!name.empty()&&!valid_preset_name(name))return false;
    auto path=root/L"active-preset.txt",temp=root/L"active-preset.tmp";
    std::ofstream out(temp);out<<name;out.close();
    return out&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
inline std::filesystem::path preset_path(const std::filesystem::path& root,const std::string& name) {
    return valid_preset_name(name)?root/L"configs"/(name+".ini"):std::filesystem::path{};
}
inline bool valid_preset_file(const std::filesystem::path& path) {
    std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size<64||size>65536)return false;
    std::ifstream in(path);std::string line;bool enabled=false,camera=false;
    while(std::getline(in,line)){enabled|=line.starts_with("enabled=");camera|=line.starts_with("focus_bone=");}
    return in.eof()&&enabled&&camera;
}
inline void mirror_active_preset(const std::filesystem::path& settings) {
    auto root=settings.parent_path();auto name=active_preset(root);if(name.empty())return;
    auto target=preset_path(root,name),temp=target;temp+=L".tmp";std::error_code ec;
    std::filesystem::create_directories(target.parent_path(),ec);if(ec)return;
    if(CopyFileW(settings.c_str(),temp.c_str(),FALSE))MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
inline std::vector<std::string> preset_names(const std::filesystem::path& root) {
    std::vector<std::string> names;std::error_code ec;
    for(std::filesystem::directory_iterator it(root/L"configs",ec),end;it!=end&&!ec;it.increment(ec))
        if(it->is_regular_file(ec)&&it->path().extension()==L".ini"&&valid_preset_name(it->path().stem().string()))names.push_back(it->path().stem().string());
    std::sort(names.begin(),names.end());return names;
}
template<class Settings> inline void disable_preset_automation(Settings& s) {
    s.replay_focus=s.auto_click=false;s.vindicta_ult.replay_focus=s.vindicta_ult.auto_click=false;s.auto_parry.enabled=false;
    s.vindicta_auto_select.enabled=false;
}
}
