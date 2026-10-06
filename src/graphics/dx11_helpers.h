#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>

namespace solace::dx11
{
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> create_rgba_texture(
    ID3D11Device* device, ID3D11DeviceContext* context, const unsigned char* pixels,
    unsigned int width, unsigned int height, bool generate_mips = true);

class pixel_shader_pass
{
  public:
    pixel_shader_pass() = default;
    pixel_shader_pass(const pixel_shader_pass&) = delete;
    pixel_shader_pass& operator=(const pixel_shader_pass&) = delete;

    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context, const char* source,
                    std::size_t source_size, const char* debug_name, std::size_t constants_size,
                    bool linear_sampler, const char* entry_point = "main");

    void reset();
    bool ready() const;

    bool upload_constants(const void* data, std::size_t size) const;
    void bind(ID3D11ShaderResourceView* const* resources = nullptr, unsigned int resource_count = 0,
              unsigned int resource_slot = 1, unsigned int sampler_slot = 1) const;

  private:
    ID3D11DeviceContext* context_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    std::size_t constants_size_ = 0;
    bool requires_sampler_ = false;
};
} // namespace solace::dx11
