#pragma once
#include "world_data.h"
#include "graphics/dx11_helpers.h"
#include "../../thirdparty/stb/stb_image.h"
#include "imgui.h"
#include <chrono>

namespace overlay {
class GameIcons {
public:
    struct Texture {Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;int width{},height{};};
    GameAssetCatalog catalog;
    void initialize(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& root,uint64_t now) {
        if(device_!=device||root_!=root){clear();device_=device;context_=context;root_=root;next_reload_=0;}
        if(now<next_reload_)return;next_reload_=now+5000;
        std::error_code ec;auto stamp=std::filesystem::last_write_time(root/"runtime.tsv",ec);
        if(!ec&&(!catalog.loaded||stamp!=stamp_)){textures_.clear();catalog.load(root);stamp_=stamp;}
    }
    void clear(){textures_.clear();catalog={};device_=nullptr;context_=nullptr;root_.clear();next_reload_=0;}
    const Texture* texture(const std::string& path) {
        if(!device_||!context_||!GameAssetCatalog::safe_asset_path(path))return nullptr;
        auto found=textures_.find(path);if(found!=textures_.end())return found->second.view?&found->second:nullptr;
        // Bounded positive and negative caches; icons are decoded only once.
        if(textures_.size()>=256)return nullptr;
        auto& result=textures_[path];std::error_code ec;auto file=root_/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),path.size()));auto count=std::filesystem::file_size(file,ec);
        if(ec||count<8||count>16*1024*1024)return nullptr;std::vector<unsigned char> data(size_t(count),0);
        std::ifstream input(file,std::ios::binary);if(!input.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))return nullptr;
        int channels{};if(!stbi_info_from_memory(data.data(),int(data.size()),&result.width,&result.height,&channels)||result.width<1||result.height<1||result.width>4096||result.height>4096)return nullptr;
        auto pixels=stbi_load_from_memory(data.data(),int(data.size()),&result.width,&result.height,&channels,4);if(!pixels)return nullptr;
        result.view=solace::dx11::create_rgba_texture(device_,context_,pixels,result.width,result.height,false);stbi_image_free(pixels);
        return result.view?&result:nullptr;
    }
    const Texture* hero(int id,bool map=false) {
        auto it=catalog.heroes.find(id);if(it==catalog.heroes.end())return nullptr;
        auto preferred=map?it->second.minimap:it->second.portrait;
        if(auto image=texture(preferred))return image;return texture(map?it->second.portrait:it->second.minimap);
    }
    std::string world_icon(const WorldObject& object)const {
        if(auto unit=catalog.unit(object.subclass);unit&&!unit->icon.empty())return unit->icon;
        switch(object.kind) {
            case WorldKind::Lane:return "npcs/trooper_psd.png";
            case WorldKind::Jungle:case WorldKind::Camp:return object.midboss?"minimap/boss_marker_neutral_psd.png":"npcs/neutral_bot_psd.png";
            case WorldKind::Breakable:return "minimap/gold_crate_marker_psd.png";
            case WorldKind::Souls:return "minimap/gold_psd.png";
            case WorldKind::Pickup:return "minimap/health_pad_psd.png";
            case WorldKind::Spawner:return "minimap/powerup_souls_png.png";
            default:return {};
        }
    }
    size_t cached()const{return textures_.size();}
    static void draw(ImDrawList* draw,const Texture& image,ImVec2 center,float size,ImU32 tint=IM_COL32_WHITE) {
        const float ratio=float(image.width)/image.height;ImVec2 half{size*.5f,size*.5f};
        if(ratio>1)half.y/=ratio;else half.x*=ratio;
        draw->AddImage((ImTextureID)(intptr_t)image.view.Get(),center-half,center+half,{0,0},{1,1},tint);
    }
private:
    ID3D11Device* device_{};ID3D11DeviceContext* context_{};
    std::filesystem::path root_;std::filesystem::file_time_type stamp_{};uint64_t next_reload_{};
    std::unordered_map<std::string,Texture> textures_;
};
}
