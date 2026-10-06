#include "assets/avatars.h"
#include "assets/asset_io.h"
#include "graphics/dx11_helpers.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "../../thirdparty/stb/stb_image.h"

namespace solace::avatars
{
namespace
{

constexpr int k_size = 128;

struct entry
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
};

std::vector<entry> g_others;
std::vector<entry> g_logos;

struct named
{
    std::string name;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
};
std::vector<named> g_brands;
entry g_me;

bool is_png(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".png";
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> decode(ID3D11Device* device,
                                                        ID3D11DeviceContext* context,
                                                        const std::filesystem::path& path)
{
    const std::vector<unsigned char> bytes = asset_io::read_binary(path);
    if (bytes.empty())
        return {};

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0)
    {
        if (pixels)
            stbi_image_free(pixels);
        return {};
    }

    const int edge = (w < h) ? w : h;
    const int ox = (w - edge) / 2;
    const int oy = (h - edge) / 2;

    std::vector<unsigned char> out((size_t)k_size * k_size * 4);
    const float step = (float)edge / (float)k_size;

    for (int y = 0; y < k_size; y++)
    {
        for (int x = 0; x < k_size; x++)
        {

            const int sx0 = ox + (int)(x * step);
            const int sy0 = oy + (int)(y * step);
            const int sx1 = ox + (int)((x + 1) * step);
            const int sy1 = oy + (int)((y + 1) * step);

            int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < h; sy++)
                for (int sx = sx0; sx < sx1 && sx < w; sx++)
                {
                    const stbi_uc* p = pixels + ((size_t)sy * w + sx) * 4;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    n++;
                }
            if (n == 0)
                n = 1;

            unsigned char* dst = out.data() + ((size_t)y * k_size + x) * 4;
            dst[0] = (unsigned char)(r / n);
            dst[1] = (unsigned char)(g / n);
            dst[2] = (unsigned char)(b / n);

            dst[3] = (unsigned char)(a / n);
        }
    }
    stbi_image_free(pixels);

    return dx11::create_rgba_texture(device, context, out.data(), k_size, k_size);
}
} // namespace

void load(const std::filesystem::path& people_directory,
          const std::filesystem::path& logo_directory, const std::filesystem::path& brand_directory,
          ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context || g_me.srv || !g_others.empty() || !g_logos.empty() ||
        !g_brands.empty())
        return;

    if (!people_directory.empty())
    {
        for (const std::filesystem::path& path : asset_io::image_files(people_directory))
        {
            auto view = decode(device, context, path);
            if (!view)
                continue;

            std::string name = asset_io::stem_utf8(path);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value)
                           { return static_cast<char>(std::tolower(value)); });
            if (name == "me" || name.rfind("me_", 0) == 0)
                g_me.srv = std::move(view);
            else
                g_others.push_back(entry{std::move(view)});
        }
    }

    if (!logo_directory.empty())
    {
        for (const std::filesystem::path& path : asset_io::image_files(logo_directory))
            if (auto view = decode(device, context, path))
                g_logos.push_back(entry{std::move(view)});
    }

    if (!brand_directory.empty())
    {
        for (const std::filesystem::path& path : asset_io::image_files(brand_directory))
        {
            if (!is_png(path))
                continue;

            if (auto view = decode(device, context, path))
                g_brands.push_back(named{asset_io::stem_utf8(path), std::move(view)});
        }
    }
}

void shutdown()
{
    g_me.srv.Reset();
    g_brands.clear();
    g_others.clear();
    g_logos.clear();
}

ImTextureID me()
{
    return g_me.srv ? reinterpret_cast<ImTextureID>(g_me.srv.Get()) : ImTextureID_Invalid;
}

namespace
{
ImTextureID pick(const std::vector<entry>& set, int index)
{
    if (set.empty())
        return ImTextureID_Invalid;

    const int count = (int)set.size();
    const int wrapped = ((index % count) + count) % count;
    return reinterpret_cast<ImTextureID>(set[wrapped].srv.Get());
}
} // namespace

ImTextureID other(int index)
{
    return pick(g_others, index);
}
ImTextureID logo(int index)
{
    return pick(g_logos, index);
}

ImTextureID brand(const char* name)
{
    if (name)
        for (const named& b : g_brands)
            if (b.name == name)
                return reinterpret_cast<ImTextureID>(b.srv.Get());
    return ImTextureID_Invalid;
}

bool draw(ImDrawList* dl, ImTextureID texture, const ImVec2& top_left, float size, float alpha,
          float rounding)
{
    if (!dl || texture == ImTextureID_Invalid || alpha <= 0.004f)
        return false;

    const float radius = (rounding < 0.f) ? size * 0.5f : rounding;

    dl->AddImageRounded(ImTextureRef(texture), top_left,
                        ImVec2(top_left.x + size, top_left.y + size), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, (int)(alpha * 255.f + 0.5f)), radius);
    return true;
}
} // namespace solace::avatars
