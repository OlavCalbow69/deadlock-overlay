#include "graphics/snapshot.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <utility>
#include <memory>
#include <unordered_map>

namespace solace::snapshot
{
namespace
{
struct texture_copy
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    void reset()
    {
        view.Reset();
        texture.Reset();
        width = height = 0;
        format = DXGI_FORMAT_UNKNOWN;
    }

    bool ensure(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source)
    {
        if (texture && width == source.Width && height == source.Height && format == source.Format)
            return true;

        reset();

        D3D11_TEXTURE2D_DESC copy = source;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy.CPUAccessFlags = 0;
        copy.MiscFlags = 0;
        copy.MipLevels = 1;
        copy.SampleDesc.Count = 1;
        copy.SampleDesc.Quality = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> next_texture;
        if (FAILED(device->CreateTexture2D(&copy, nullptr, &next_texture)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
        view_description.Format = copy.Format;
        view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_description.Texture2D.MipLevels = 1;

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> next_view;
        if (FAILED(device->CreateShaderResourceView(next_texture.Get(), &view_description,
                                                    &next_view)))
            return false;

        texture = std::move(next_texture);
        view = std::move(next_view);
        width = source.Width;
        height = source.Height;
        format = source.Format;
        return true;
    }
};

struct surface_state {
    texture_copy snapshot, backdrop;
    bool requested{}, ready{}, backdrop_ready{};
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    IDXGISwapChain* swap_chain{};
};
std::unordered_map<ImGuiContext*,std::unique_ptr<surface_state>> surfaces;
surface_state& state() {
    auto& value=surfaces[ImGui::GetCurrentContext()];
    if(!value)value=std::make_unique<surface_state>();
    return *value;
}

void backdrop_callback(const ImDrawList*, const ImDrawCmd*)
{
    if (!state().device || !state().context || !state().swap_chain)
        return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(state().swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC description{};
    backbuffer->GetDesc(&description);
    if (!state().backdrop.ensure(state().device, description))
        return;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil;
    state().context->OMGetRenderTargets(1, &render_target, &depth_stencil);
    state().context->OMSetRenderTargets(0, nullptr, nullptr);
    state().context->CopyResource(state().backdrop.texture.Get(), backbuffer.Get());

    ID3D11RenderTargetView* target = render_target.Get();
    state().context->OMSetRenderTargets(1, &target, depth_stencil.Get());
    state().backdrop_ready = true;
}
} // namespace

void attach(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain)
{
    state().device = device;
    state().context = context;
    state().swap_chain = swap_chain;
}

void capture_backdrop(ImDrawList* draw_list)
{
    if (!draw_list || !state().device)
        return;

    draw_list->AddCallback(backdrop_callback, nullptr);
    draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void invalidate_backdrop()
{
    state().backdrop_ready = false;
    state().backdrop.reset();
}

bool backdrop_ready()
{
    return state().backdrop_ready && state().backdrop.view;
}

ImTextureID backdrop()
{
    return state().backdrop.view ? reinterpret_cast<ImTextureID>(state().backdrop.view.Get())
                           : ImTextureID_Invalid;
}

void request()
{
    state().requested = true;
    state().ready = false;
}

bool ready()
{
    return state().ready && state().snapshot.view;
}

ImTextureID texture()
{
    return state().snapshot.view ? reinterpret_cast<ImTextureID>(state().snapshot.view.Get())
                           : ImTextureID_Invalid;
}

void release()
{
    state().snapshot.reset();
    state().ready = false;
    state().requested = false;
}

void shutdown()
{
    surfaces.erase(ImGui::GetCurrentContext());
}

void poll(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain)
{
    if (!state().requested || !device || !context || !swap_chain)
        return;
    state().requested = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC description{};
    backbuffer->GetDesc(&description);
    if (!state().snapshot.ensure(device, description))
        return;

    context->CopyResource(state().snapshot.texture.Get(), backbuffer.Get());
    state().ready = true;
}
} // namespace solace::snapshot
