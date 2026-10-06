#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <array>

namespace solace::platform
{
class d3d11_renderer final
{
  public:
    d3d11_renderer() = default;
    ~d3d11_renderer();

    d3d11_renderer(const d3d11_renderer&) = delete;
    d3d11_renderer& operator=(const d3d11_renderer&) = delete;

    [[nodiscard]] bool initialize(HWND window,bool prefer_composition=true);
    [[nodiscard]] bool resize(UINT width, UINT height);

    [[nodiscard]] HRESULT test_occlusion();
    void clear(const std::array<float, 4>& color) const;
    [[nodiscard]] HRESULT present(UINT sync_interval = 1);
    [[nodiscard]] bool composition() const { return composition_device_!=nullptr; }
    [[nodiscard]] bool wait_for_frame(DWORD timeout_ms) const;
    [[nodiscard]] HRESULT last_error() const noexcept;

    [[nodiscard]] ID3D11Device* device() const noexcept;
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept;
    [[nodiscard]] IDXGISwapChain* swap_chain() const noexcept;

  private:
    [[nodiscard]] bool create_render_target();
    void reset() noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> composition_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> composition_visual_;
    bool occluded_ = false;
    HANDLE frame_ready_{};
    HRESULT last_error_ = S_OK;
};
} // namespace solace::platform
