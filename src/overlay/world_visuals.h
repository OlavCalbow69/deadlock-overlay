#pragma once
#include "game_reader.h"
#include "game_icons.h"
#include "external_settings.h"
#include "ui/foundation/theme.h"
#include "ui/foundation/primitives.h"

namespace overlay {
inline ImU32 minimap_lane_color(int number) {
    switch(number){
        case 1:return IM_COL32(227,191,57,210);
        case 3:case 6:return IM_COL32(80,183,106,210); // Greenwich retains legacy lane ID 6.
        case 4:return IM_COL32(88,158,244,210);
        default:return 0;
    }
}
inline bool world_category_enabled(const WorldObject& object,const ExternalSettings& e,uint8_t team) {
    if(object.availability!=WorldAvailability::Alive&&!e.world_destroyed)return false;
    switch(object.kind) {
        case WorldKind::Lane:return e.world_lane&&(e.world_allies||enemy_teams(team,object.team));
        case WorldKind::Jungle:case WorldKind::Camp:return e.world_jungle;
        case WorldKind::Breakable:return e.world_breakables;
        case WorldKind::Souls:case WorldKind::Pickup:return e.world_pickups;
        case WorldKind::Spawner:return e.world_spawners;
        default:return false;
    }
}
inline ImU32 world_marker_color(const WorldObject& object,float alpha=1) {
    if(object.availability!=WorldAvailability::Alive)return IM_COL32(174,178,190,int(180*alpha));
    if(object.kind==WorldKind::Lane) {
        switch(object.team) {
            case 2:return IM_COL32(255,169,79,int(255*alpha)); // Amber / Hidden King.
            case 3:return IM_COL32(133,207,255,int(255*alpha)); // Sapphire / Archmother.
            default:return IM_COL32(174,178,190,int(255*alpha));
        }
    }
    if(object.kind==WorldKind::Jungle||object.kind==WorldKind::Camp)return IM_COL32(217,159,255,int(255*alpha));
    return IM_COL32(239,204,127,int(255*alpha));
}
inline std::string world_timer_text(const WorldObject& object) {
    if(!std::isfinite(object.remaining)||object.remaining<0)return {};
    int seconds=int(std::ceil(object.remaining));char buffer[32]{};
    if(seconds>=60)std::snprintf(buffer,sizeof(buffer),"%s%d:%02d",object.authoritative?"":"~",seconds/60,seconds%60);
    else std::snprintf(buffer,sizeof(buffer),"%s%ds",object.authoritative?"":"~",seconds);
    return buffer;
}
inline void marker_text(ImDrawList* draw,ImVec2 center,const std::string& text,float size,ImU32 color=IM_COL32_WHITE) {
    auto font=ImGui::GetFont();auto bounds=font->CalcTextSizeA(size,FLT_MAX,0,text.c_str());auto pos=center-ImVec2{bounds.x*.5f,0};
    draw->AddText(font,size,pos+ImVec2{1,1},IM_COL32(0,0,0,230),text.c_str());draw->AddText(font,size,pos,color,text.c_str());
}
inline unsigned draw_world_markers(ImDrawList* draw,const Snapshot& snapshot,const ExternalSettings& e,GameIcons& icons,float width,float height,float scale,float frame_opacity) {
    if(!e.world)return 0;unsigned count{};Vec3 from=snapshot.local_player_valid?snapshot.local_player.origin:snapshot.camera_position;
    if(!snapshot.local_player_valid&&!snapshot.camera_valid)return 0;
    for(const auto& object:snapshot.world.objects) {
        if(!world_category_enabled(object,e,snapshot.local_team))continue;float distance=world_distance_meters(from,object.position);if(distance>e.world_range)continue;
        Vec3 world=object.position;world.z+=20;ScreenPoint p;if(!project(snapshot.matrix,world,width,height,p))continue;
        const float size=e.world_icon_size*scale;ImVec2 at{p.x,p.y};if(at.x<size||at.x>width-size||at.y<size||at.y>height-size)continue;
        const ImVec2 half{size*.5f+4*scale,size*.5f+4*scale};
        draw->AddRectFilled(at-half,at+half,solace::mo::with_alpha(solace::c_background,frame_opacity),6*scale);
        draw->AddRect(at-half,at+half,world_marker_color(object,.55f),6*scale);
        auto image=icons.texture(icons.world_icon(object));auto tint=object.availability==WorldAvailability::Alive?IM_COL32_WHITE:IM_COL32(150,152,160,150);
        if(image)GameIcons::draw(draw,*image,at,size,tint);else draw->AddCircle(at,size*.24f,world_marker_color(object),12,2*scale);
        std::string label=e.world_timers?world_timer_text(object):std::string{};
        if(e.world_distance){char text[24]{};std::snprintf(text,sizeof(text),"%.0fm",distance);if(!label.empty())label+="  ";label+=text;}
        if(!label.empty())marker_text(draw,{at.x,at.y+half.y+3*scale},label,12*scale);
        ++count;
    }
    return count;
}
inline unsigned draw_game_minimap(ImDrawList* draw,const Snapshot& snapshot,const ExternalSettings& e,GameIcons& icons,float width,float height,float scale,float frame_opacity) {
    if(!e.minimap)return 0;
    const float size=std::min(e.minimap_size*scale,std::max(100.f,std::min(width,height)-48*scale)),margin=24*scale;
    ImVec2 at{(e.minimap_corner&1)?width-margin-size:margin,(e.minimap_corner&2)?height-margin-size:margin},end=at+ImVec2{size,size};
    draw->AddRectFilled(at-ImVec2{4,4},end+ImVec2{4,4},solace::mo::with_alpha(solace::c_background,e.minimap_opacity*frame_opacity),12*scale);
    draw->AddRect(at-ImVec2{4,4},end+ImVec2{4,4},solace::c_border,12*scale);
    if(!snapshot.world.bounds_valid){marker_text(draw,at+ImVec2{size*.5f,size*.5f},"Waiting for map bounds",12*scale);return 0;}
    auto map_key=visibility_map_key(snapshot.map_name);auto map=icons.catalog.maps.find(map_key);bool has_texture=false;
    // The installed icon catalog keeps the legacy overview key for Midtown.
    if(map==icons.catalog.maps.end()&&map_key=="dl_midtown") {
        auto legacy=icons.catalog.maps.find("caldera");
        if(legacy!=icons.catalog.maps.end()&&legacy->second=="minimap/base/minimap_midtown_mid_psd.png")map=legacy;
    }
    if(map!=icons.catalog.maps.end())if(auto image=icons.texture(map->second)){draw->AddImage((ImTextureID)(intptr_t)image->view.Get(),at,end,{0,0},{1,1},IM_COL32(255,255,255,int(255*e.minimap_opacity)));has_texture=true;}
    if(!has_texture)for(int i=1;i<4;++i){float k=size*i/4;draw->AddLine(at+ImVec2{k,0},at+ImVec2{k,size},IM_COL32(120,130,150,25));draw->AddLine(at+ImVec2{0,k},at+ImVec2{size,k},IM_COL32(120,130,150,25));}
    auto point=[&](Vec3 world,ImVec2& screen,bool clamp)->bool {ScreenPoint uv;if(!minimap_uv(world,snapshot.world.mins,snapshot.world.maxs,uv))return false;if(!clamp&&(uv.x<0||uv.x>1||uv.y<0||uv.y>1))return false;
        const float inset=clamp?e.minimap_icon_size*scale*.5f/size:0;screen=at+ImVec2{std::clamp(uv.x,inset,1-inset)*size,std::clamp(uv.y,inset,1-inset)*size};return true;};
    draw->PushClipRect(at,end,true);
    auto lane_point=[&](Vec3 world,ImVec2& screen){ScreenPoint uv;if(!minimap_uv(world,snapshot.world.mins,snapshot.world.maxs,uv))return false;screen=at+ImVec2{uv.x*size,uv.y*size};return true;};
    if(e.minimap_lanes)for(const auto& lane:snapshot.world.lanes)for(size_t i=1;i<lane.points.size();++i) {
        const auto color=minimap_lane_color(lane.number);if(!color)continue;
        ImVec2 a,b;if(!lane_point(lane.points[i-1],a)||!lane_point(lane.points[i],b))continue;
        if(lane.tangent_in.size()==lane.points.size()&&lane.tangent_out.size()==lane.points.size()) {
            // Replicated tangent vectors are offsets from their corresponding nodes.
            ImVec2 control_a,control_b;if(lane_point(add(lane.points[i-1],lane.tangent_out[i-1]),control_a)&&lane_point(add(lane.points[i],lane.tangent_in[i]),control_b)) {
                draw->AddBezierCubic(a,control_a,control_b,b,color,2*scale);continue;
            }
        }
        draw->AddLine(a,b,color,2*scale);
    }
    unsigned count{};
    if(e.minimap_world)for(const auto& object:snapshot.world.objects) {
        if(!world_category_enabled(object,e,snapshot.local_team))continue;ImVec2 pos;if(!point(object.position,pos,false))continue;
        auto color=world_marker_color(object);float icon_size=(object.kind==WorldKind::Lane?9:14)*scale;
        if(auto image=icons.texture(icons.world_icon(object)))GameIcons::draw(draw,*image,pos,icon_size,object.availability==WorldAvailability::Alive?IM_COL32_WHITE:IM_COL32(155,155,165,150));
        else draw->AddCircleFilled(pos,2.5f*scale,color,8);
        if(object.kind==WorldKind::Lane)draw->AddCircle(pos,5*scale,color,8,scale);
        if(e.world_timers){auto label=world_timer_text(object);if(!label.empty())marker_text(draw,pos+ImVec2{0,icon_size*.5f},label,10*scale,color);}
        ++count;
    }
    auto player_icon=[&](const Player& player,bool self) {
        if(!player.origin_valid)return;ImVec2 pos;if(!point(player.origin,pos,true))return;
        float radius=e.minimap_icon_size*scale*.5f;auto color=self?IM_COL32(238,231,198,255):enemy_teams(snapshot.local_team,player.team)?IM_COL32(255,101,87,255):IM_COL32(102,171,255,255);
        draw->AddCircleFilled(pos,radius+2*scale,IM_COL32(10,12,18,235),24);
        if(auto image=icons.hero(player.hero_id,true))GameIcons::draw(draw,*image,pos,radius*2);
        else draw->AddCircleFilled(pos,radius*.55f,color,16);
        draw->AddCircle(pos,radius+scale,color,24,1.5f*scale);
        if(self&&snapshot.projection.valid) {
            Vec3 forward{snapshot.matrix[12],snapshot.matrix[13],0};float length=std::sqrt(dot(forward,forward));
            if(length>1e-5f){ImVec2 dir{forward.x/length,-forward.y/length},tip=pos+dir*(radius+7*scale),side{-dir.y,dir.x};draw->AddTriangleFilled(tip,tip-dir*(5*scale)+side*(3*scale),tip-dir*(5*scale)-side*(3*scale),color);}
        }
        ++count;
    };
    // Deliberately no visibility filter: the user chose all detected enemies.
    if(e.minimap_players){for(const auto& player:snapshot.players)if(e.minimap_allies||enemy_teams(snapshot.local_team,player.team))player_icon(player,false);if(snapshot.local_player_valid)player_icon(snapshot.local_player,true);}
    draw->PopClipRect();return count;
}
}
