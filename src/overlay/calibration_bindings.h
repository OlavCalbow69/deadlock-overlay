#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace overlay {
struct CalibrationBinding {
    unsigned code{};bool mouse{};int vk{};std::string label;
    explicit operator bool()const{return code!=0;}
    bool operator==(const CalibrationBinding& other)const{return code==other.code&&mouse==other.mouse;}
    std::string down()const {
        if(!*this)return {};
        if(mouse){static const char* names[]={"","left","right","middle","side1","side2"};return std::string("km.")+names[code]+"(1)\r\n";}
        return "km.down("+std::to_string(code)+")\r\n";
    }
    std::string up()const {
        if(!*this)return {};
        if(mouse){static const char* names[]={"","left","right","middle","side1","side2"};return std::string("km.")+names[code]+"(0)\r\n";}
        return "km.up("+std::to_string(code)+")\r\n";
    }
};
inline CalibrationBinding calibration_binding(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::toupper(c));});
    CalibrationBinding b;b.label=name;
    if(name.size()==6&&name.starts_with("MOUSE")&&name[5]>='2'&&name[5]<='5') {
        b.code=unsigned(name[5]-'0');b.mouse=true;static const int keys[]={0,0,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};b.vk=keys[b.code];return b;
    }
    if(name.size()==1&&name[0]>='A'&&name[0]<='Z'){b.code=unsigned(name[0]-'A')+4;b.vk=name[0];return b;}
    if(name.size()==1&&name[0]>='0'&&name[0]<='9'){b.code=name[0]=='0'?39:unsigned(name[0]-'1')+30;b.vk=name[0];return b;}
    if(name.size()>=2&&name.size()<=3&&name[0]=='F'&&std::all_of(name.begin()+1,name.end(),[](char c){return c>='0'&&c<='9';})) {
        int n=std::stoi(name.substr(1));if(n>=1&&n<=12){b.code=unsigned(n)+57;b.vk=VK_F1+n-1;return b;}
    }
    struct Key {const char* name;unsigned hid;int vk;};
    static constexpr Key keys[]={{"SPACE",44,VK_SPACE},{"TAB",43,VK_TAB},{"SHIFT",225,VK_LSHIFT},{"CTRL",224,VK_LCONTROL},{"ALT",226,VK_LMENU},
        {"CAPSLOCK",57,VK_CAPITAL},{"BACKSPACE",42,VK_BACK},{"SEMICOLON",51,VK_OEM_1},{"'",52,VK_OEM_7},{",",54,VK_OEM_COMMA},
        {".",55,VK_OEM_PERIOD},{"/",56,VK_OEM_2},{"[",47,VK_OEM_4},{"]",48,VK_OEM_6},{"\\",49,VK_OEM_5},{"-",45,VK_OEM_MINUS},{"=",46,VK_OEM_PLUS}};
    for(auto k:keys)if(name==k.name){b.code=k.hid;b.vk=k.vk;return b;}return {};
}
namespace calibration_keys {
struct Node {std::string value;std::map<std::string,Node> children;};
inline std::vector<std::string> tokens(std::string_view text) {
    std::vector<std::string> out;
    for(size_t i=0;i<text.size()&&out.size()<16384;) {
        if(std::isspace(static_cast<unsigned char>(text[i]))){++i;continue;}
        if(text.substr(i,2)=="//"){auto n=text.find('\n',i);i=n==text.npos?text.size():n+1;continue;}
        if(text[i]=='{'||text[i]=='}'){out.emplace_back(1,text[i++]);continue;}
        std::string token;
        if(text[i]=='"'){++i;while(i<text.size()&&text[i]!='"'){if(text[i]=='\\'&&i+1<text.size()&&(text[i+1]=='"'||text[i+1]=='\\'))++i;token+=text[i++];}if(i<text.size())++i;}
        else while(i<text.size()&&!std::isspace(static_cast<unsigned char>(text[i]))&&text[i]!='{'&&text[i]!='}')token+=text[i++];
        out.push_back(std::move(token));
    }return out;
}
inline bool parse(const std::vector<std::string>& t,size_t& at,Node& node,unsigned depth=0) {
    if(depth>12)return false;
    while(at<t.size()&&t[at]!="}") {
        auto key=t[at++];if(key=="{"||at==t.size())return false;Node next;
        if(t[at]=="{"){++at;if(!parse(t,at,next,depth+1)||at>=t.size()||t[at++]!="}")return false;}
        else next.value=t[at++];node.children[key]=std::move(next);
    }return true;
}
inline Node file(const std::filesystem::path& p) {
    std::error_code ec;if(std::filesystem::file_size(p,ec)>1048576||ec)return {};
    std::ifstream in(p);std::string content((std::istreambuf_iterator<char>(in)),{});auto t=tokens(content);size_t at{};Node n;
    return parse(t,at,n)&&at==t.size()?n:Node{};
}
inline const Node* child(const Node* n,const std::string& key){if(!n)return nullptr;auto i=n->children.find(key);return i==n->children.end()?nullptr:&i->second;}
inline CalibrationBinding action(const Node* keys,const std::string& name) {
    const auto* n=child(keys,name);if(!n)return {};
    for(const auto& pair:{std::pair{"Key","Modifier"},std::pair{"Key2","Modifier2"}}) {
        const auto* key=child(n,pair.first);const auto* modifier=child(n,pair.second);
        if(key&&(!modifier||modifier->value.empty()||modifier->value=="NONE")){auto b=calibration_binding(key->value);if(b)return b;}
    }return {};
}
inline std::filesystem::path steam_path() {
    wchar_t buffer[32768]{};DWORD size=sizeof(buffer);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,buffer,&size)==ERROR_SUCCESS?std::filesystem::path(buffer):std::filesystem::path{};
}
}
struct CalibrationBindings {
    CalibrationBinding zoom,ultimate,cancel;std::string source{"Game bindings unavailable"};
};
inline CalibrationBindings find_calibration_bindings(const std::string& hero) {
    using namespace calibration_keys;CalibrationBindings result;auto steam=steam_path();if(steam.empty())return result;
    DWORD active{},size=sizeof(active);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&active,&size);
    std::filesystem::path selected;std::error_code ec;auto users=steam/L"userdata";
    if(active)selected=users/std::to_wstring(active)/L"1422450/remote/cfg/citadelkeys_personal.lst";
    if(!std::filesystem::is_regular_file(selected,ec)) {
        selected.clear();size_t count{};
        for(std::filesystem::directory_iterator i(users,ec),end;i!=end&&!ec;i.increment(ec)) {
            auto candidate=i->path()/L"1422450/remote/cfg/citadelkeys_personal.lst";
            if(std::filesystem::is_regular_file(candidate,ec)){selected=candidate;++count;}
            ec.clear();
        }
        if(count!=1){result.source="Could not select the active Steam account's game bindings";return result;}
    }
    auto root=file(selected);auto* bindings=child(&root,"KeyBindings");auto* keys=child(bindings,"Keys");
    auto* overrides=child(child(bindings,"Units"),hero);
    result.zoom=action(child(overrides,"ADS")?overrides:keys,"ADS");
    result.ultimate=action(child(overrides,"Ability4")?overrides:keys,"Ability4");
    result.cancel=action(child(overrides,"CancelAbility")?overrides:keys,"CancelAbility");
    result.source="Saved game bindings";return result;
}
// Raw input includes the device's own injected counts. Consume only the exact
// expected signed pulse; any excess, off-axis movement or wheel input cancels.
class CalibrationMouseWatch {
    HWND sink_{};bool armed_{},unexpected_{};long remaining_x_{},remaining_y_{};
    static bool consume(long& remaining,long value) {
        if(!value)return true;
        if((remaining>0&&value>0&&value<=remaining)||(remaining<0&&value<0&&value>=remaining)){remaining-=value;return true;}return false;
    }
public:
    bool attach(HWND sink) {RAWINPUTDEVICE device{1,2,RIDEV_INPUTSINK,sink};if(!RegisterRawInputDevices(&device,1,sizeof(device)))return false;sink_=sink;return true;}
    void start(){armed_=true;unexpected_=false;remaining_x_=remaining_y_=0;}
    void stop(){armed_=false;remaining_x_=remaining_y_=0;}
    bool armed()const{return armed_;}
    bool ready()const{return sink_!=nullptr;}
    bool unexpected()const{return unexpected_;}
    bool complete()const{return !remaining_x_&&!remaining_y_;}
    std::array<long,2> remaining()const{return {remaining_x_,remaining_y_};}
    bool expect(long x,long y){if(!complete()||unexpected_)return false;remaining_x_=x;remaining_y_=y;return true;}
    void packet(long x,long y,bool wheel=false){if(armed_&&(wheel||!consume(remaining_x_,x)||!consume(remaining_y_,y)))unexpected_=true;}
    void message(HRAWINPUT handle) {
        if(!armed_)return;RAWINPUT raw{};UINT size=sizeof(raw);
        if(GetRawInputData(handle,RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))==UINT(-1)){unexpected_=true;return;}
        if(raw.header.dwType==RIM_TYPEMOUSE) {
            if(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE){unexpected_=true;return;}
            packet(raw.data.mouse.lLastX,raw.data.mouse.lLastY,(raw.data.mouse.usButtonFlags&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL))!=0);
        }
    }
};
}
