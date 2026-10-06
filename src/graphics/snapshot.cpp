#include "graphics/snapshot.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <utility>

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

texture_copy g_snapshot;
texture_copy g_backdrop;
bool g_requested = false;
bool g_ready = false;
bool g_backdrop_ready = false;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;

void backdrop_callback(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_device || !g_context || !g_swap_chain)
        return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC description{};
    backbuffer->GetDesc(&description);
    if (!g_backdrop.ensure(g_device, description))
        return;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil;
    g_context->OMGetRenderTargets(1, &render_target, &depth_stencil);
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    g_context->CopyResource(g_backdrop.texture.Get(), backbuffer.Get());

    ID3D11RenderTargetView* target = render_target.Get();
    g_context->OMSetRenderTargets(1, &target, depth_stencil.Get());
    g_backdrop_ready = true;
}
} // namespace

void attach(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain)
{
    g_device = device;
    g_context = context;
    g_swap_chain = swap_chain;
}

void capture_backdrop(ImDrawList* draw_list)
{
    if (!draw_list || !g_device)
        return;

    draw_list->AddCallback(backdrop_callback, nullptr);
    draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void invalidate_backdrop()
{
    g_backdrop_ready = false;
    g_backdrop.reset();
}

bool backdrop_ready()
{
    return g_backdrop_ready && g_backdrop.view;
}

ImTextureID backdrop()
{
    return g_backdrop.view ? reinterpret_cast<ImTextureID>(g_backdrop.view.Get())
                           : ImTextureID_Invalid;
}

void request()
{
    g_requested = true;
    g_ready = false;
}

bool ready()
{
    return g_ready && g_snapshot.view;
}

ImTextureID texture()
{
    return g_snapshot.view ? reinterpret_cast<ImTextureID>(g_snapshot.view.Get())
                           : ImTextureID_Invalid;
}

void release()
{
    g_snapshot.reset();
    g_ready = false;
    g_requested = false;
}

void shutdown()
{
    g_snapshot.reset();
    g_backdrop.reset();
    g_requested = g_ready = g_backdrop_ready = false;
    g_device = nullptr;
    g_context = nullptr;
    g_swap_chain = nullptr;
}

void poll(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain)
{
    if (!g_requested || !device || !context || !swap_chain)
        return;
    g_requested = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC description{};
    backbuffer->GetDesc(&description);
    if (!g_snapshot.ensure(device, description))
        return;

    context->CopyResource(g_snapshot.texture.Get(), backbuffer.Get());
    g_ready = true;
}
} // namespace solace::snapshot
