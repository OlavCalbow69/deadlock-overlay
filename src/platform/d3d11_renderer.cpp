#include "platform/d3d11_renderer.h"

#include <iterator>
#include <utility>
#include <dxgi1_3.h>
#include <algorithm>

namespace solace::platform
{
d3d11_renderer::~d3d11_renderer()
{
    reset();
}

bool d3d11_renderer::initialize(HWND window,bool prefer_composition)
{
    reset();
    if (!window)
    {
        last_error_ = E_INVALIDARG;
        return false;
    }

    if(prefer_composition) {
        D3D_FEATURE_LEVEL level;
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_);
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> chain;
        if(SUCCEEDED(hr))hr=device_.As(&dxgi);
        if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
        if(SUCCEEDED(hr))hr=adapter->GetParent(IID_PPV_ARGS(&factory));
        RECT bounds{};GetClientRect(window,&bounds);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=std::max<LONG>(1,bounds.right);desc.Height=std::max<LONG>(1,bounds.bottom);
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if(SUCCEEDED(hr))hr=factory->CreateSwapChainForComposition(device_.Get(),&desc,nullptr,&chain);
        if(SUCCEEDED(hr))hr=chain.As(&swap_chain_);
        Microsoft::WRL::ComPtr<IDXGISwapChain2> paced_chain;
        if(SUCCEEDED(hr))hr=chain.As(&paced_chain);
        if(SUCCEEDED(hr))hr=paced_chain->SetMaximumFrameLatency(1);
        if(SUCCEEDED(hr)){frame_ready_=paced_chain->GetFrameLatencyWaitableObject();if(!frame_ready_)hr=E_FAIL;}
        if(SUCCEEDED(hr))hr=DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_device_));
        if(SUCCEEDED(hr))hr=composition_device_->CreateTargetForHwnd(window,TRUE,&composition_target_);
        if(SUCCEEDED(hr))hr=composition_device_->CreateVisual(&composition_visual_);
        if(SUCCEEDED(hr))hr=composition_visual_->SetContent(swap_chain_.Get());
        if(SUCCEEDED(hr))hr=composition_target_->SetRoot(composition_visual_.Get());
        if(SUCCEEDED(hr))hr=composition_device_->Commit();
        if(SUCCEEDED(hr)&&create_render_target()){last_error_=S_OK;return true;}
        reset(); // Retain the working legacy path if DirectComposition is unavailable.
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Width = 0;
    description.BufferDesc.Height = 0;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 0;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    D3D_FEATURE_LEVEL feature_level{};

    const auto create = [&](D3D_DRIVER_TYPE driver_type)
    {
        device.Reset();
        context.Reset();
        swap_chain.Reset();
        return ::D3D11CreateDeviceAndSwapChain(nullptr, driver_type, nullptr, 0, feature_levels,
                                               static_cast<UINT>(std::size(feature_levels)),
                                               D3D11_SDK_VERSION, &description,
                                               swap_chain.GetAddressOf(), device.GetAddressOf(),
                                               &feature_level, context.GetAddressOf());
    };

    HRESULT result = create(D3D_DRIVER_TYPE_HARDWARE);
    if (result == DXGI_ERROR_UNSUPPORTED)
        result = create(D3D_DRIVER_TYPE_WARP);
    if (FAILED(result))
    {
        last_error_ = result;
        return false;
    }

    device_ = std::move(device);
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device;
    if (SUCCEEDED(device_.As(&dxgi_device))) dxgi_device->SetMaximumFrameLatency(1);
    context_ = std::move(context);
    swap_chain_ = std::move(swap_chain);
    if (!create_render_target())
    {
        const HRESULT failure = last_error_;
        reset();
        last_error_ = failure;
        return false;
    }
    last_error_ = S_OK;
    return true;
}

bool d3d11_renderer::resize(UINT width, UINT height)
{
    if (!swap_chain_ || !context_ || width == 0 || height == 0)
    {
        last_error_ = E_INVALIDARG;
        return false;
    }

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    render_target_.Reset();

    const HRESULT resize_result =
        swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, frame_ready_?DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT:0);
    if (FAILED(resize_result))
    {
        last_error_ = resize_result;
        return false;
    }

    const bool created = create_render_target();
    if (created)
        last_error_ = S_OK;
    return created;
}

HRESULT d3d11_renderer::test_occlusion()
{
    if (!swap_chain_)
        return E_POINTER;
    if (!occluded_)
        return S_OK;

    const HRESULT result = swap_chain_->Present(0, DXGI_PRESENT_TEST);
    if (result != DXGI_STATUS_OCCLUDED)
        occluded_ = false;
    return result;
}

void d3d11_renderer::clear(const std::array<float, 4>& color) const
{
    if (!context_ || !render_target_)
        return;

    ID3D11RenderTargetView* target = render_target_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->ClearRenderTargetView(target, color.data());
}

HRESULT d3d11_renderer::present(UINT sync_interval)
{
    if (!swap_chain_)
        return E_POINTER;

    const HRESULT result = swap_chain_->Present(sync_interval, 0);
    occluded_ = result == DXGI_STATUS_OCCLUDED;
    return result;
}

bool d3d11_renderer::wait_for_frame(DWORD timeout_ms) const
{
    return !frame_ready_ || WaitForSingleObject(frame_ready_,timeout_ms)==WAIT_OBJECT_0;
}

HRESULT d3d11_renderer::last_error() const noexcept
{
    return last_error_;
}

ID3D11Device* d3d11_renderer::device() const noexcept
{
    return device_.Get();
}

ID3D11DeviceContext* d3d11_renderer::context() const noexcept
{
    return context_.Get();
}

IDXGISwapChain* d3d11_renderer::swap_chain() const noexcept
{
    return swap_chain_.Get();
}

bool d3d11_renderer::create_render_target()
{
    if (!swap_chain_ || !device_)
    {
        last_error_ = E_POINTER;
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    const HRESULT buffer_result = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(buffer_result))
    {
        last_error_ = buffer_result;
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    const HRESULT target_result =
        device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);
    if (FAILED(target_result))
    {
        last_error_ = target_result;
        return false;
    }

    render_target_ = std::move(target);
    return true;
}

void d3d11_renderer::reset() noexcept
{
    if(frame_ready_){CloseHandle(frame_ready_);frame_ready_=nullptr;}
    if(composition_visual_)composition_visual_->SetContent(nullptr);
    if(composition_target_)composition_target_->SetRoot(nullptr);
    if(composition_device_)composition_device_->Commit();
    composition_visual_.Reset();composition_target_.Reset();composition_device_.Reset();
    if (context_)
        context_->ClearState();
    render_target_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device_.Reset();
    occluded_ = false;
    last_error_ = S_OK;
}
} // namespace solace::platform
