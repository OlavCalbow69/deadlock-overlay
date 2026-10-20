#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace overlay {
inline std::string mak_frame(uint8_t op,std::span<const uint8_t> p={}) {
    std::string value;value.reserve(p.size()+5);value.push_back(static_cast<char>(uint8_t(0xde)));value.push_back(static_cast<char>(uint8_t(0xad)));
    value+=char(p.size()&255);value+=char(p.size()>>8);value+=char(op);
    value.append(reinterpret_cast<const char*>(p.data()),p.size());return value;
}
// Binary payloads are never examined as text (including ERR and >>> bytes).
// No resynchronization by silently discarding data: malformed/oversized input
// fails the serial connection.
class MakcuMixedParser {
    std::string bytes_;
public:
    template<class Binary,class Text> bool feed(std::string_view bytes,Binary binary,Text text) {
        if(bytes_.size()+bytes.size()>8192)return false;
        bytes_.append(bytes);
        while(!bytes_.empty()) {
            if(uint8_t(bytes_[0])==0xde) {
                if(bytes_.size()<2)return true;
                if(uint8_t(bytes_[1])!=0xad)return false;
                if(bytes_.size()<5)return true;
                const size_t count=uint8_t(bytes_[2])|(size_t(uint8_t(bytes_[3]))<<8);
                if(count>4096)return false;
                if(bytes_.size()<count+5)return true;
                binary(uint8_t(bytes_[4]),std::span(reinterpret_cast<const uint8_t*>(bytes_.data()+5),count));
                bytes_.erase(0,count+5);
            } else {
                auto end=bytes_.find(static_cast<char>(uint8_t(0xde)));
                const auto count=end==std::string::npos?bytes_.size():end;
                for(size_t i=0;i<count;++i)if((uint8_t(bytes_[i])<32&&bytes_[i]!='\r'&&bytes_[i]!='\n'&&bytes_[i]!='\t')||uint8_t(bytes_[i])>=127)return false;
                text(std::string_view(bytes_).substr(0,count));bytes_.erase(0,count);
            }
        }return true;
    }
};

} // namespace overlay
