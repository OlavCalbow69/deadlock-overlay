#include "assets/images.h"
#include "assets/asset_io.h"
#include "graphics/dx11_helpers.h"

#include "imgui_internal.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <random>
#include <thread>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "../../thirdparty/stb/stb_image.h"

namespace solace::images
{
namespace
{

constexpr float k_edge_margin = 4.f;

struct decoded
{
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    std::string name;
};

struct loader
{
    std::vector<std::thread> workers;
    std::vector<std::filesystem::path> files;
    std::atomic<int> next{0};
    std::mutex mutex;
    std::vector<decoded> finished;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> views;
    std::vector<texture> textures;
    std::atomic<bool> quit{false};
    bool started = false;
    options opts;
};

loader& store()
{
    static loader l;
    return l;
}

decoded downscale(const unsigned char* src, int sw, int sh, int sx0, int sy0, int crop_w,
                  int crop_h, int dw, int dh, const options& o)
{
    decoded out;
    out.width = dw;
    out.height = dh;
    out.rgba.resize((size_t)dw * dh * 4);

    const float radius = o.radius_ratio * (float)dw;

    for (int y = 0; y < dh; y++)
    {
        const int y0 = sy0 + (int)((int64_t)y * crop_h / dh);
        const int y1 = ImMax(y0 + 1, sy0 + (int)((int64_t)(y + 1) * crop_h / dh));

        for (int x = 0; x < dw; x++)
        {
            const int x0 = sx0 + (int)((int64_t)x * crop_w / dw);
            const int x1 = ImMax(x0 + 1, sx0 + (int)((int64_t)(x + 1) * crop_w / dw));

            unsigned int acc[3] = {0, 0, 0};
            unsigned int n = 0;
            for (int sy = y0; sy < ImMin(y1, sh); sy++)
            {
                const unsigned char* row = src + ((size_t)sy * sw + x0) * 4;
                for (int sx = x0; sx < ImMin(x1, sw); sx++, row += 4)
                {
                    acc[0] += row[0];
                    acc[1] += row[1];
                    acc[2] += row[2];
                    n++;
                }
            }

            unsigned char* dst = &out.rgba[((size_t)y * dw + x) * 4];
            for (int i = 0; i < 3; i++)
                dst[i] = (unsigned char)(acc[i] / ImMax(n, 1u));

            const float luma = 0.2126f * dst[0] + 0.7152f * dst[1] + 0.0722f * dst[2];
            for (int i = 0; i < 3; i++)
                dst[i] = (unsigned char)ImClamp(luma + (dst[i] - luma) * o.saturate, 0.f, 255.f);

            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            const float ex =
                ImFabs(px - (float)dw * 0.5f) - ((float)dw * 0.5f - k_edge_margin - radius);
            const float ey =
                ImFabs(py - (float)dh * 0.5f) - ((float)dh * 0.5f - k_edge_margin - radius);
            const float qx = ImMax(ex, 0.f);
            const float qy = ImMax(ey, 0.f);
            const float dist = ImSqrt(qx * qx + qy * qy) - radius;
            dst[3] = (unsigned char)(ImClamp(0.5f - dist, 0.f, 1.f) * 255.f);
        }
    }

    return out;
}

void run()
{
    loader& l = store();

    for (;;)
    {
        const int index = l.next.fetch_add(1);
        if (index >= (int)l.files.size() || l.quit.load())
            return;

        const std::filesystem::path& path = l.files[(size_t)index];

        const std::vector<unsigned char> bytes = asset_io::read_binary(path);
        if (bytes.empty())
            continue;

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels =
            stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
        if (!pixels)
            continue;

        int crop_w = w, crop_h = h;
        if ((float)w / (float)h > l.opts.aspect)
            crop_w = ImMax(1, (int)((float)h * l.opts.aspect + 0.5f));
        else
            crop_h = ImMax(1, (int)((float)w / l.opts.aspect + 0.5f));

        const int sx0 = (w - crop_w) / 2;
        const int sy0 = (h - crop_h) / 2;

        const int dw = ImMin(l.opts.max_edge, crop_w);
        const int dh = ImMax(1, (int)((float)dw / l.opts.aspect + 0.5f));

        decoded reduced = downscale(pixels, w, h, sx0, sy0, crop_w, crop_h, dw, dh, l.opts);
        stbi_image_free(pixels);

        reduced.name = asset_io::stem_utf8(path);

        std::lock_guard<std::mutex> lock(l.mutex);
        l.finished.push_back(std::move(reduced));
    }
}
} // namespace

void load_folder(const std::filesystem::path& directory, const options& opts)
{
    loader& l = store();
    if (l.started || directory.empty())
        return;

    l.started = true;
    l.opts = opts;
    l.files = asset_io::image_files(directory);

    std::shuffle(l.files.begin(), l.files.end(), std::mt19937(std::random_device{}()));

    const unsigned int hardware = std::thread::hardware_concurrency();
    const int count = ImClamp(hardware ? (int)hardware / 2 : 2, 1, 4);
    for (int i = 0; i < count; i++)
        l.workers.emplace_back(run);
}

void update(ID3D11Device* device, ID3D11DeviceContext* context)
{
    loader& l = store();
    if (device == nullptr || context == nullptr)
        return;

    std::vector<decoded> pending;
    {
        std::lock_guard<std::mutex> lock(l.mutex);
        pending.swap(l.finished);
    }

    for (const decoded& d : pending)
    {

        auto view = dx11::create_rgba_texture(device, context, d.rgba.data(),
                                              static_cast<unsigned int>(d.width),
                                              static_cast<unsigned int>(d.height));
        if (!view)
            continue;

        texture t;
        t.id = reinterpret_cast<ImTextureID>(view.Get());
        t.width = d.width;
        t.height = d.height;
        t.name = d.name;
        l.views.push_back(std::move(view));
        l.textures.push_back(t);
    }
}

const std::vector<texture>& ready()
{
    return store().textures;
}

void shutdown()
{
    loader& l = store();
    l.quit.store(true);
    for (std::thread& t : l.workers)
        if (t.joinable())
            t.join();
    l.workers.clear();

    l.textures.clear();
    l.views.clear();

    {
        std::lock_guard<std::mutex> lock(l.mutex);
        l.finished.clear();
    }
    l.files.clear();
    l.next.store(0);
    l.quit.store(false);
    l.started = false;
    l.opts = options{};
}
} // namespace solace::images
