#include "overlay/map_lanes.h"
#include <iostream>
#include <set>

int main(int argc,char** argv) {
    using namespace overlay;if(argc!=3)return 2;
    unsigned checked{},failed{};auto check=[&](bool valid,const char* message){++checked;if(!valid){++failed;std::cerr<<message<<'\n';}};
    MapLaneCache cache;auto root=std::filesystem::path(argv[1]);auto scratch=std::filesystem::path(argv[2]);
    const auto& main=cache.get(root,"dl_midtown",1000);std::set<int> numbers;
    for(const auto& path:main){numbers.insert(path.number);check(path.points.size()>2,"installed path has geometry");}
    check(main.size()==3&&numbers==std::set<int>{1,4,6},"main map has York, Broadway and Greenwich paths");
    const auto& practice=cache.get(root,"new_player_basics",1100);check(practice.size()==1&&practice.front().number==1,"map change replaces geometry");
    check(cache.get(root,"../dl_midtown",1200).empty(),"unsafe map name cannot reuse paths");
    std::filesystem::create_directories(scratch);auto file=scratch/"sample.tsv";
    auto write=[&](int number,bool valid=true){std::ofstream out(file);out<<"# Vanta map lanes v1 sample "<<std::string(64,'0')<<"\npath\t0\t"<<number<<"\t0\t0\t0\npath\t0\t"<<number<<"\t"<<(valid?100:300000)<<"\t0\t0\n";};
    write(1);auto first_stamp=std::filesystem::last_write_time(file);check(cache.get(scratch,"sample",10000).size()==1,"valid exported path loads");
    write(6);std::filesystem::last_write_time(file,first_stamp+std::chrono::seconds(2));
    check(cache.get(scratch,"sample",11000).front().number==1,"unchanged sample reuses the cached geometry");
    check(cache.get(scratch,"sample",15001).front().number==6,"updater replacement reloads after timestamp check");
    write(6,false);std::filesystem::last_write_time(file,first_stamp+std::chrono::seconds(4));check(cache.get(scratch,"sample",20002).empty(),"invalid points cannot create connecting lines");
    check(cache.get(scratch,"missing",21000).empty(),"missing map does not inherit another map's paths");
    write(1);check(cache.get(scratch,"sample",22000).size()==1,"returning to a map reloads its file");
    check(cache.get(root,"dl_midtown",10).size()==3,"clock reset reloads safely");
    std::cout<<"{\"checks\":"<<checked<<",\"failed\":"<<failed<<",\"game_inputs\":false}\n";return failed?1:0;
}
