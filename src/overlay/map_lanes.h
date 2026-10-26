#pragma once
#include "world_data.h"
#include <locale>

namespace overlay {
inline bool map_lane_number(int number){return number==1||number==3||number==4||number==6;}

// Shared by the minimap and rift reader. Installed paths are static; only check
// their timestamp occasionally, then reload after the updater replaces a file.
class MapLaneCache {
    std::filesystem::path root_;
    std::string map_;
    std::vector<MapLane> lanes_;
    std::filesystem::file_time_type stamp_{};
    uint64_t next_check_{},checked_{};
    bool have_stamp_{};
public:
    void reset(){root_.clear();map_.clear();lanes_.clear();next_check_=checked_=0;have_stamp_=false;}
    const std::vector<MapLane>& get(const std::filesystem::path& root,const std::string& map,uint64_t now) {
        if(root!=root_||map!=map_||now<checked_){reset();root_=root;map_=map;}
        if(now<next_check_)return lanes_;
        checked_=now;next_check_=now+5000;
        if(map.empty()||map.size()>80||map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos){lanes_.clear();return lanes_;}
        auto file=root/(map+".tsv");std::error_code error;auto stamp=std::filesystem::last_write_time(file,error);
        if(error){lanes_.clear();have_stamp_=false;return lanes_;}
        if(have_stamp_&&stamp==stamp_)return lanes_;
        have_stamp_=false;lanes_.clear();
        std::ifstream input(file);std::string line,word,source_map,hash;
        if(!std::getline(input,line))return lanes_;
        std::istringstream header(line);std::string brand,subject,kind,version;
        // Header: # Vanta map lanes v1 <map> <source SHA256>.
        if(!(header>>word>>brand>>subject>>kind>>version>>source_map>>hash)||word!="#"||brand!="Vanta"||subject!="map"||kind!="lanes"||version!="v1"
            ||source_map!=map||hash.size()!=64||hash.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)return lanes_;
        std::vector<MapLane> loaded;unsigned rows{};int previous=-1;bool valid=true;
        while(std::getline(input,line)) {
            if(++rows>32768||line.size()>256){valid=false;break;}
            if(line.empty()||line[0]=='#')continue;
            std::istringstream row(line);row.imbue(std::locale::classic());int id{},number{};Vec3 point{};
            if(!(row>>word>>id>>number>>point.x>>point.y>>point.z)||word!="path"||id<0||id>=128||!map_lane_number(number)||!finite(point)
                ||std::abs(point.x)>200000||std::abs(point.y)>200000||std::abs(point.z)>200000){valid=false;break;}
            if(id!=previous){if(id<=previous){valid=false;break;}loaded.push_back({number});previous=id;}
            auto& path=loaded.back();if(path.number!=number||path.points.size()>=4096){valid=false;break;}path.points.push_back(point);
        }
        if(!valid||input.bad())return lanes_;
        std::erase_if(loaded,[](const MapLane& path){return path.points.size()<2;});lanes_=std::move(loaded);stamp_=stamp;have_stamp_=true;return lanes_;
    }
};
}
