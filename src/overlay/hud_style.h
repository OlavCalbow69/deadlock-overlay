#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/foundation/theme.h"
#include "ui/foundation/motion/motion.h"
#include <algorithm>
#include <array>
#include <string>

namespace overlay {
enum class HudGlyph {Boss,Rift,Shield,Eye,Activity};
inline constexpr float hud_card_width=248,hud_card_height=52,hud_card_gap=8;
inline ImU32 hud_muted(){return solace::c_muted_foreground;}
inline ImU32 hud_good(){return IM_COL32(108,194,154,255);}
inline ImU32 hud_warning(){return IM_COL32(226,191,111,255);}
inline void hud_glyph(ImDrawList* draw,HudGlyph glyph,ImVec2 center,float scale,ImU32 color) {
    auto point=[&](float x,float y){return center+ImVec2{x*scale,y*scale};};const float line=1.25f*scale;
    switch(glyph) {
    case HudGlyph::Boss:
        draw->AddPolyline(std::array<ImVec2,5>{point(-6,-3),point(-5,5),point(5,5),point(6,-3),point(0,0)}.data(),5,color,ImDrawFlags_Closed,line);
        for(int x:{-5,0,5})draw->AddCircleFilled(point(float(x),x? -5.f:-7.f),1.4f*scale,color);break;
    case HudGlyph::Rift:
        draw->AddEllipse(center,{5*scale,8*scale},color,-.35f,24,line);
        draw->AddLine(point(-1,-5),point(2,0),color,line);draw->AddLine(point(2,0),point(-2,5),color,line);break;
    case HudGlyph::Shield:
        draw->AddPolyline(std::array<ImVec2,6>{point(0,-7),point(6,-4),point(5,3),point(0,7),point(-5,3),point(-6,-4)}.data(),6,color,ImDrawFlags_Closed,line);break;
    case HudGlyph::Eye:
        draw->AddEllipse(center,{8*scale,5*scale},color,0,24,line);draw->AddCircle(center,2*scale,color,16,line);break;
    case HudGlyph::Activity:
        draw->AddPolyline(std::array<ImVec2,6>{point(-8,0),point(-4,0),point(-1,-6),point(2,6),point(5,0),point(8,0)}.data(),6,color,0,line);break;
    }
}
inline void hud_text(ImDrawList* draw,ImVec2 at,float size,ImU32 color,const char* text,float max_width) {
    auto font=ImGui::GetFont();std::string shown=text?text:"";
    if(font->CalcTextSizeA(size,FLT_MAX,0,shown.c_str()).x>max_width) {
        while(!shown.empty()&&font->CalcTextSizeA(size,FLT_MAX,0,(shown+"...").c_str()).x>max_width){
            auto last=shown.size()-1;while(last>0&&(static_cast<unsigned char>(shown[last])&0xc0)==0x80)--last;shown.resize(last);
        }shown+="...";
    }
    draw->AddText(font,size,at,color,shown.c_str());
}
inline void draw_hud_card(ImDrawList* draw,ImVec2 origin,float scale,const char* title,const char* value,const char* detail,
    HudGlyph glyph,ImU32 accent,float opacity=1,float width=hud_card_width) {
    const ImVec2 end=origin+ImVec2{width*scale,hud_card_height*scale};
    opacity=std::clamp(opacity,.05f,1.f);
    draw->AddRectFilled(origin,end,solace::mo::with_alpha(solace::c_card,opacity),14*scale);
    draw->AddRect(origin+ImVec2{.5f,.5f},end-ImVec2{.5f,.5f},solace::c_border,14*scale,0,scale);
    hud_glyph(draw,glyph,origin+ImVec2{18*scale,19*scale},scale,hud_muted());
    auto font=ImGui::GetFont();const float value_width=std::min(width*.45f*scale,font->CalcTextSizeA(15*scale,FLT_MAX,0,value).x);
    hud_text(draw,origin+ImVec2{34*scale,10*scale},13*scale,solace::c_foreground,title,width*scale-value_width-58*scale);
    hud_text(draw,{end.x-13*scale-value_width,origin.y+9*scale},15*scale,accent,value,value_width+1);
    hud_text(draw,origin+ImVec2{13*scale,32*scale},11*scale,hud_muted(),detail,(width-26)*scale);
}
}
