#include "graphics/dx11_helpers.h"

#include <d3dcompiler.h>

#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace solace::dx11
{
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> create_rgba_texture(
    ID3D11Device* device, ID3D11DeviceContext* context, const unsigned char* pixels,
    unsigned int width, unsigned int height, bool generate_mips)
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if (!device || !context || !pixels || width == 0 || height == 0)
        return view;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = generate_mips ? 0u : 1u;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | (generate_mips ? D3D11_BIND_RENDER_TARGET : 0u);
    description.MiscFlags = generate_mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0u;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &texture)))
        return view;

    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels, width * 4u, 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
    view_description.Format = description.Format;
    view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_description.Texture2D.MipLevels = generate_mips ? UINT_MAX : 1u;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), &view_description, &view)))
        return {};

    if (generate_mips)
        context->GenerateMips(view.Get());
    return view;
}

namespace
{
void log_compile_error(const char* debug_name, ID3DBlob* errors)
{
    std::string message = "[Solace] ";
    message += debug_name ? debug_name : "pixel shader";
    message += " failed to compile:\n";
    ::OutputDebugStringA(message.c_str());

    if (errors && errors->GetBufferPointer())
        ::OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
}

bool create_pixel_shader(ID3D11Device* device, const char* source, std::size_t source_size,
                         const char* debug_name, const char* entry_point,
                         Microsoft::WRL::ComPtr<ID3D11PixelShader>& shader)
{
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT compiled = ::D3DCompile(source, source_size, nullptr, nullptr, nullptr,
                                          entry_point, "ps_4_0", 0, 0, &bytecode, &errors);
    if (FAILED(compiled) || !bytecode)
    {
        log_compile_error(debug_name, errors.Get());
        return false;
    }

    return SUCCEEDED(device->CreatePixelShader(bytecode->GetBufferPointer(),
                                               bytecode->GetBufferSize(), nullptr, &shader));
}

bool create_constant_buffer(ID3D11Device* device, std::size_t size,
                            Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer)
{
    if (size == 0 || size % 16 != 0 || size > (std::numeric_limits<unsigned int>::max)())
        return false;

    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<unsigned int>(size);
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(device->CreateBuffer(&description, nullptr, &buffer));
}

bool create_linear_sampler(ID3D11Device* device,
                           Microsoft::WRL::ComPtr<ID3D11SamplerState>& sampler)
{
    D3D11_SAMPLER_DESC description{};
    description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    description.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    description.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(device->CreateSamplerState(&description, &sampler));
}
} // namespace

bool pixel_shader_pass::initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                                   const char* source, std::size_t source_size,
                                   const char* debug_name, std::size_t constants_size,
                                   bool linear_sampler, const char* entry_point)
{
    if (ready())
        return true;
    if (!device || !context || !source || source_size == 0 || !entry_point ||
        entry_point[0] == '\0')
        return false;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;

    if (!create_pixel_shader(device, source, source_size, debug_name, entry_point, shader) ||
        !create_constant_buffer(device, constants_size, constants) ||
        (linear_sampler && !create_linear_sampler(device, sampler)))
        return false;

    context_ = context;
    shader_ = std::move(shader);
    constants_ = std::move(constants);
    sampler_ = std::move(sampler);
    constants_size_ = constants_size;
    requires_sampler_ = linear_sampler;
    return true;
}

void pixel_shader_pass::reset()
{
    sampler_.Reset();
    constants_.Reset();
    shader_.Reset();
    context_ = nullptr;
    constants_size_ = 0;
    requires_sampler_ = false;
}

bool pixel_shader_pass::ready() const
{
    return context_ && shader_ && constants_ && (!requires_sampler_ || sampler_);
}

bool pixel_shader_pass::upload_constants(const void* data, std::size_t size) const
{
    if (!ready() || !data || size != constants_size_)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;

    std::memcpy(mapped.pData, data, size);
    context_->Unmap(constants_.Get(), 0);
    return true;
}

void pixel_shader_pass::bind(ID3D11ShaderResourceView* const* resources,
                             unsigned int resource_count, unsigned int resource_slot,
                             unsigned int sampler_slot) const
{
    if (!ready())
        return;

    ID3D11Buffer* constants = constants_.Get();
    context_->PSSetShader(shader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &constants);

    if (resources && resource_count > 0)
        context_->PSSetShaderResources(resource_slot, resource_count, resources);

    if (sampler_)
    {
        ID3D11SamplerState* sampler = sampler_.Get();
        context_->PSSetSamplers(sampler_slot, 1, &sampler);
    }
}
} // namespace solace::dx11
