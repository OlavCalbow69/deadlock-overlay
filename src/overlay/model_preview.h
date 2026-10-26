#pragma once
#include "game_reader.h"
#include "bone_rig.h"
#include "game_icons.h"
#include <d3dcompiler.h>
#include <cstring>

namespace overlay {
class ModelPreview {
    template<class T> using Com=Microsoft::WRL::ComPtr<T>;
    struct Vertex {Vec3 position,normal;float u,v;};
    struct Part {uint32_t first{},count{};std::array<float,4> color{};Com<ID3D11ShaderResourceView> texture;};
    struct Bone {std::string name;int32_t parent{};std::array<float,8> transform{};};
    struct Constants {Matrix matrix;std::array<float,4> color,eye;};
    ID3D11Device* device_{};ID3D11DeviceContext* context_{};
    Com<ID3D11Buffer> vertices_,indices_,constants_;Com<ID3D11VertexShader> vertex_shader_;Com<ID3D11PixelShader> pixel_shader_;
    Com<ID3D11InputLayout> layout_;Com<ID3D11RasterizerState> raster_;Com<ID3D11DepthStencilState> depth_state_;Com<ID3D11SamplerState> sampler_;
    Com<ID3D11Texture2D> color_,resolved_,depth_;Com<ID3D11RenderTargetView> target_;Com<ID3D11DepthStencilView> depth_view_;Com<ID3D11ShaderResourceView> image_;
    std::vector<Part> parts_;std::vector<Bone> bones_;std::vector<ModelHitbox> boxes_;
    Vec3 low_{},high_{};Player player_;unsigned width_{},height_{},samples_{1};float rendered_yaw_{10000};
    bool attempted_{};uint64_t next_reload_{};std::filesystem::file_time_type stamp_{};
    bool load(const std::filesystem::path& path) {
        std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size<128||size>64*1024*1024){status="Preview model missing - Install all maps";return false;}
        std::ifstream file(path,std::ios::binary);size_t remaining=size;
        auto bytes=[&](void* value,size_t count){if(count>remaining||!file.read(static_cast<char*>(value),std::streamsize(count)))return false;remaining-=count;return true;};
        auto value=[&]<class T>(T& out){return bytes(&out,sizeof(out));};
        auto text=[&](std::string& out){uint32_t count{};if(!value(count)||count>256)return false;out.resize(count);return !count||bytes(out.data(),count);};
        char magic[8]{};uint32_t version{},nv{},ni{},np{},nb{},nh{};
        if(!bytes(magic,8)||std::memcmp(magic,"VANTAPRV",8)||!value(version)||version!=1||!value(nv)||!value(ni)||!value(np)||!value(nb)||!value(nh)
            ||nv<3||nv>1000000||ni<3||ni>3000000||ni%3||!np||np>64||!nb||nb>1024||nh>256)return false;
        if(!text(name)||!text(source)||!value(hero_id))return false;
        std::vector<Vertex> vertices(nv);std::vector<uint32_t> indices(ni);
        if(!bytes(vertices.data(),vertices.size()*sizeof(Vertex))||!bytes(indices.data(),indices.size()*4))return false;
        low_={FLT_MAX,FLT_MAX,FLT_MAX};high_={-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(const auto& v:vertices){if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)||world_distance(v.position,{})>10000)return false;
            low_={std::min(low_.x,v.position.x),std::min(low_.y,v.position.y),std::min(low_.z,v.position.z)};high_={std::max(high_.x,v.position.x),std::max(high_.y,v.position.y),std::max(high_.z,v.position.z)};}
        for(auto i:indices)if(i>=nv)return false;
        std::vector<Part> parts(np);
        for(auto& part:parts) {
            uint32_t count{};if(!value(part.first)||!value(part.count)||part.first>ni||part.count>ni-part.first||part.count%3||!value(part.color)||!value(count)||count>4*1024*1024)return false;
            for(float color:part.color)if(!std::isfinite(color))return false;
            if(count){std::vector<unsigned char> data(count);if(!bytes(data.data(),count))return false;int w{},h{},channels{};
                if(!stbi_info_from_memory(data.data(),int(count),&w,&h,&channels)||w<1||h<1||w>1024||h>1024)return false;
                auto pixels=stbi_load_from_memory(data.data(),int(count),&w,&h,&channels,4);if(!pixels)return false;
                part.texture=solace::dx11::create_rgba_texture(device_,context_,pixels,w,h,false);stbi_image_free(pixels);
            }else{const unsigned char white[]={255,255,255,255};part.texture=solace::dx11::create_rgba_texture(device_,context_,white,1,1,false);}
            if(!part.texture)return false;
        }
        std::vector<Bone> bones(nb);std::vector<std::string> names;std::vector<int16_t> parents;
        for(auto& bone:bones){Vec3 p;std::array<float,4> q{};if(!text(bone.name)||!value(bone.parent)||bone.parent<-1||bone.parent>=int(nb)||!value(p)||!value(q)||!finite(p))return false;
            const float norm=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];if(!std::isfinite(norm)||std::abs(norm-1)>.05f)return false;
            bone.transform={p.x,p.y,p.z,1,q[0],q[1],q[2],q[3]};names.push_back(bone.name);parents.push_back(int16_t(bone.parent));}
        auto rig=resolve_bone_rig(names,parents);if(rig.anchor()<0)return false;
        auto regions=rig_regions(names,parents,rig);std::vector<ModelHitbox> boxes(nh);
        for(size_t i=0;i<boxes.size();++i){auto& b=boxes[i];int32_t shape{};uint32_t translation{};
            if(!value(b.bone)||!value(shape)||shape<0||shape>2||b.bone<0||b.bone>=int(nb)||!value(b.low)||!value(b.high)||!value(b.radius)||!value(b.group)||!value(translation)||translation>1)return false;
            b.shape=HitboxShape(shape);b.translation_only=translation!=0;b.region=regions[b.bone];b.source_id=i+1;if(!valid_hitbox(b))return false;}
        if(remaining)return false;
        D3D11_BUFFER_DESC desc{};desc.Usage=D3D11_USAGE_IMMUTABLE;desc.ByteWidth=UINT(vertices.size()*sizeof(Vertex));desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA data{vertices.data()};
        if(FAILED(device_->CreateBuffer(&desc,&data,&vertices_)))return false;desc.ByteWidth=UINT(indices.size()*4);desc.BindFlags=D3D11_BIND_INDEX_BUFFER;data.pSysMem=indices.data();
        if(FAILED(device_->CreateBuffer(&desc,&data,&indices_)))return false;
        Player player;player.handle=1;player.team=3;player.maximum=1000;player.hero_id=hero_id;player.hero_name=name;player.model_name=source;player.feet={0,0,0};player.feet_valid=player.origin_valid=true;
        auto point=[&](int index){const auto& t=bones[index].transform;return Vec3{t[0],t[1],t[2]};};
        player.head=point(rig.anchor());int dots[]={rig.head,rig.body,rig.pelvis};for(int i=0;i<3;++i)if(dots[i]>=0){player.dots[i]=point(dots[i]);player.dot_valid[i]=true;}
        auto edges=skeleton_edges(rig_skeleton_names(names,rig),parents);
        for(auto [a,b]:edges)player.skeleton.push_back({point(a),point(b),true,true});
        std::vector<int> bounds{rig.anchor(),rig.body,rig.pelvis};for(auto [a,b]:edges){bounds.push_back(a);bounds.push_back(b);}std::sort(bounds.begin(),bounds.end());bounds.erase(std::unique(bounds.begin(),bounds.end()),bounds.end());
        for(int b:bounds)if(b>=0&&player.body_point_count<player.body_points.size())player.body_points[player.body_point_count++]=point(b);
        for(const auto& box:boxes){Hitbox world;if(world_hitbox(box,bones[box.bone].transform,world))player.hitboxes.push_back(world);}
        if(player.body_point_count<2||player.hitboxes.empty())return false;
        player_=std::move(player);parts_=std::move(parts);bones_=std::move(bones);boxes_=std::move(boxes);triangle_count=ni/3;status="Game model ready";return true;
    }
    bool pipeline() {
        static constexpr char shader[]=R"(
cbuffer Data : register(b0) { float4 rows[4]; float4 color; float4 eye; };
struct V {float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;};
struct P {float4 p:SV_POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float3 world:TEXCOORD1;};
P vs(V v){P o;float4 p=float4(v.p,1);o.p=float4(dot(rows[0],p),dot(rows[1],p),dot(rows[2],p),dot(rows[3],p));o.n=v.n;o.uv=v.uv;o.world=v.p;return o;}
Texture2D image:register(t0);SamplerState sample_image:register(s0);
float4 ps(P p):SV_TARGET{float4 albedo=image.Sample(sample_image,p.uv)*color;clip(albedo.a-.2);float3 n=normalize(p.n);float light=.42+.58*saturate(dot(n,normalize(float3(.4,-.5,.85))));float rim=.08*pow(1-abs(dot(n,normalize(eye.xyz-p.world))),3);return float4(pow(max(pow(max(albedo.rgb,0),2.2)*light+rim,0),1/2.2),1);}
)";
        Com<ID3DBlob> vs,ps,errors;
        if(FAILED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors))
            ||FAILED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors)))return false;
        if(FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_shader_))||FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_shader_)))return false;
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(FAILED(device_->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_)))return false;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.Usage=D3D11_USAGE_DYNAMIC;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device_->CreateBuffer(&cb,nullptr,&constants_)))return false;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=true;raster.MultisampleEnable=true;
        if(FAILED(device_->CreateRasterizerState(&raster,&raster_)))return false;
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=true;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        if(FAILED(device_->CreateDepthStencilState(&depth,&depth_state_)))return false;
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        return SUCCEEDED(device_->CreateSamplerState(&sampler,&sampler_));
    }
    bool resize(unsigned width,unsigned height) {
        image_.Reset();target_.Reset();color_.Reset();resolved_.Reset();depth_.Reset();depth_view_.Reset();width_=height_=0;
        UINT quality{};samples_=SUCCEEDED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,4,&quality))&&quality?4:1;
        D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=samples_;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(device_->CreateTexture2D(&texture,nullptr,&color_))||FAILED(device_->CreateRenderTargetView(color_.Get(),nullptr,&target_)))return false;
        texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device_->CreateTexture2D(&texture,nullptr,&resolved_))||FAILED(device_->CreateShaderResourceView(resolved_.Get(),nullptr,&image_)))return false;
        texture.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;texture.SampleDesc.Count=samples_;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device_->CreateTexture2D(&texture,nullptr,&depth_))||FAILED(device_->CreateDepthStencilView(depth_.Get(),nullptr,&depth_view_)))return false;
        width_=width;height_=height;rendered_yaw_=10000;return true;
    }
public:
    std::string status{"Preview not loaded"},name,source;int hero_id{};unsigned triangle_count{};uint64_t model_renders{};
    void reset(){*this=ModelPreview{};}
    bool initialize(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& file,uint64_t now) {
        if(device_!=device){reset();device_=device;context_=context;}
        if(now<next_reload_)return ready();next_reload_=now+5000;std::error_code ec;auto stamp=std::filesystem::last_write_time(file,ec);
        if(!ec&&(!attempted_||stamp!=stamp_)){attempted_=true;stamp_=stamp;parts_.clear();vertices_.Reset();indices_.Reset();status="Invalid preview model";if(load(file)&&!vertex_shader_&&!pipeline()){vertices_.Reset();status="Preview shader unavailable";}rendered_yaw_=10000;}
        else if(ec&&!attempted_)status="Preview model missing - Install all maps";
        return ready();
    }
    bool ready()const{return vertices_&&indices_&&vertex_shader_&&!parts_.empty();}
    Snapshot scene(unsigned width,unsigned height,float yaw,bool occluded,float health)const {
        Snapshot result;if(!ready()||width<1||height<1)return result;
        // Keep the Turn control's viewing direction after correcting the mesh basis.
        const Vec3 center=mul(add(low_,high_),.5f);float angle=(yaw+90)*.01745329251994f;
        Vec3 away{std::cos(angle),std::sin(angle),.12f};away=mul(away,1/std::sqrt(dot(away,away)));auto forward=mul(away,-1.f);
        auto right=cross(forward,{0,0,1});right=mul(right,1/std::sqrt(dot(right,right)));auto up=cross(right,forward);
        const float tangent_y=.32f,tangent_x=tangent_y*float(width)/height;float distance=80;
        for(int i=0;i<8;++i){Vec3 corner{(i&1)?high_.x:low_.x,(i&2)?high_.y:low_.y,(i&4)?high_.z:low_.z};auto relative=sub(corner,center);
            distance=std::max(distance,std::max(std::abs(dot(relative,right))/tangent_x,std::abs(dot(relative,up))/tangent_y)/.78f-dot(relative,forward));}
        result.camera_position=add(center,mul(away,distance));result.camera_valid=true;auto& m=result.matrix;
        auto row=[&](int at,Vec3 axis,float factor){m[at]=axis.x*factor;m[at+1]=axis.y*factor;m[at+2]=axis.z*factor;m[at+3]=-dot(axis,result.camera_position)*factor;};
        row(0,right,1/tangent_x);row(4,up,1/tangent_y);row(8,forward,2000.f/1999.f);m[11]-=2000.f/1999.f;row(12,forward,1);
        result.projection=projection_info(m);result.local_team=2;result.local_player_valid=true;result.local_player.origin=result.camera_position;
        Player player=player_;player.health=std::clamp(int(std::lround(1000*health)),1,1000);player.visible=!occluded;player.dot_visible.fill(!occluded);
        for(auto& segment:player.skeleton)segment.visible=!occluded;
        for(auto& box:player.hitboxes){box.visible=!occluded;box.visibility_known=true;}
        // Visibility helpers require a valid mesh in addition to known point state.
        result.visibility=std::make_shared<VisibilityMesh>();result.players.push_back(std::move(player));return result;
    }
    ID3D11ShaderResourceView* render(unsigned width,unsigned height,const Snapshot& scene,float yaw) {
        if(!ready()||width<1||height<1||width>2048||height>2048)return nullptr;
        if((width_!=width||height_!=height)&&!resize(width,height))return nullptr;
        if(rendered_yaw_==yaw)return image_.Get();
        ID3D11ShaderResourceView* none{};context_->PSSetShaderResources(0,1,&none);auto target=target_.Get();context_->OMSetRenderTargets(1,&target,depth_view_.Get());
        constexpr float clear[]={0,0,0,0};context_->ClearRenderTargetView(target_.Get(),clear);context_->ClearDepthStencilView(depth_view_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        UINT stride=sizeof(Vertex),offset{};auto vertex=vertices_.Get();context_->IASetVertexBuffers(0,1,&vertex,&stride,&offset);context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R32_UINT,0);context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_shader_.Get(),nullptr,0);context_->PSSetShader(pixel_shader_.Get(),nullptr,0);context_->GSSetShader(nullptr,nullptr,0);
        auto cb=constants_.Get();context_->VSSetConstantBuffers(0,1,&cb);context_->PSSetConstantBuffers(0,1,&cb);auto sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
        for(const auto& part:parts_){Constants constants{scene.matrix,part.color,{scene.camera_position.x,scene.camera_position.y,scene.camera_position.z,0}};D3D11_MAPPED_SUBRESOURCE data{};
            if(FAILED(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&data)))return nullptr;std::memcpy(data.pData,&constants,sizeof(constants));context_->Unmap(constants_.Get(),0);
            auto texture=part.texture.Get();context_->PSSetShaderResources(0,1,&texture);context_->DrawIndexed(part.count,part.first,0);}
        context_->OMSetRenderTargets(0,nullptr,nullptr);if(samples_>1)context_->ResolveSubresource(resolved_.Get(),0,color_.Get(),0,DXGI_FORMAT_R8G8B8A8_UNORM);else context_->CopyResource(resolved_.Get(),color_.Get());
        rendered_yaw_=yaw;++model_renders;return image_.Get();
    }
};
}
