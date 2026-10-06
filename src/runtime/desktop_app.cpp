#include "runtime/desktop_app.h"

#include "application/app.h"
#include "assets/asset_io.h"
#include "assets/avatars.h"
#include "assets/images.h"
#include "core/diagnostics.h"
#include "core/environment.h"
#include "core/product_info.h"
#include "generated/fonts/geist_data.h"
#include "graphics/snapshot.h"
#include "platform/d3d11_renderer.h"
#include "platform/win32_window.h"
#include "ui/controls/morph_slider.h"
#include "ui/controls/widgets.h"
#include "ui/effects/glass_cursor.h"
#include "ui/foundation/rounded_panel.h"
#include "ui/foundation/runtime.h"
#include "ui/foundation/theme.h"
#include "ui/foundation/typography/font_cache.h"
#include "ui/screens/search_overlay.h"
#include "ui/screens/shell_menus.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <array>
#include <optional>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM w_param, LPARAM l_param);

namespace solace::runtime
{
namespace
{
void set_ui_dpi(UINT dpi, bool rebuild_fonts)
{
    const UINT safe_dpi = dpi > 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    ui_runtime::set_scale(
        static_cast<float>(safe_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI), rebuild_fonts);
}

void warm_fonts()
{
    fonts.get(geist_regular, 12);
    fonts.get(geist_regular, 14);
    fonts.get(geist_regular, 16);
    fonts.get(geist_medium, 14);
    fonts.get(geist_medium, 16);
    fonts.get(geist_semibold, 20);
}

std::optional<LRESULT> handle_window_message(void*, HWND window, UINT message, WPARAM w_param,
                                             LPARAM l_param)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param))
        return static_cast<LRESULT>(true);

    switch (message)
    {
    case WM_DPICHANGED:
        set_ui_dpi(static_cast<UINT>(HIWORD(w_param)), true);
        break;

    case WM_SETCURSOR:
        if (LOWORD(l_param) == HTCLIENT && glass::enabled() && glass::cursor_live())
        {
            ::SetCursor(nullptr);
            return static_cast<LRESULT>(TRUE);
        }
        break;

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE && !solace::morphing_search_open() && !solace::overlay_open() &&
            !solace::target_menu_open() && !solace::profile_menu_open() &&
            !solace::notifications_open())
        {
            ::PostMessageW(window, WM_CLOSE, 0, 0);
            return static_cast<LRESULT>(0);
        }
        break;
    }
    return std::nullopt;
}

class imgui_session final
{
  public:
    imgui_session() = default;
    ~imgui_session()
    {
        shutdown();
    }

    imgui_session(const imgui_session&) = delete;
    imgui_session& operator=(const imgui_session&) = delete;

    [[nodiscard]] bool initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        context_created_ = true;

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.Fonts->TexMinWidth = 1024;
        io.Fonts->TexMinHeight = 1024;

        ImGui::StyleColorsDark();
        ImGui::GetStyle().CircleTessellationMaxError = 0.10f;

        win32_initialized_ = ImGui_ImplWin32_Init(window);
        if (!win32_initialized_)
            return false;

        dx11_initialized_ = ImGui_ImplDX11_Init(device, context);
        return dx11_initialized_;
    }

    void new_frame() const
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

  private:
    void shutdown() noexcept
    {
        if (dx11_initialized_)
        {
            ImGui_ImplDX11_Shutdown();
            dx11_initialized_ = false;
        }
        if (win32_initialized_)
        {
            ImGui_ImplWin32_Shutdown();
            win32_initialized_ = false;
        }
        if (context_created_)
        {
            ui_runtime::clear_animation_states();
            ImGui::DestroyContext();
            context_created_ = false;
        }
    }

    bool context_created_ = false;
    bool win32_initialized_ = false;
    bool dx11_initialized_ = false;
};

class ui_services final
{
  public:
    ui_services() = default;
    ~ui_services()
    {
        shutdown();
    }

    ui_services(const ui_services&) = delete;
    ui_services& operator=(const ui_services&) = delete;

    void initialize(const platform::d3d11_renderer& renderer)
    {
        active_ = true;

        const std::string theme = environment::value("THEME");
        if (!theme.empty())
            solace::set_dark(_stricmp(theme.c_str(), "light") != 0);

        fonts.install_kerning();
        warm_fonts();

        images::options slides;
        slides.max_edge = 1024;
        slides.aspect = 416.f / 650.f;
        slides.radius_ratio = 0.f;
        slides.saturate = 1.f;
        images::load_folder(asset_io::asset_directory(L"slides", L"SLIDES"), slides);

        const bool slider_initialized =
            slides::morph_slider_init(renderer.device(), renderer.context());
        const bool panel_initialized =
            solace::rounded_panel::init(renderer.device(), renderer.context());
        const bool cursor_initialized = glass::cursor_init(renderer.device(), renderer.context());

        avatars::load(asset_io::asset_directory(L"avatars", L"AVATARS"),
                      asset_io::asset_directory(L"logos", L"LOGOS"),
                      asset_io::asset_directory(L"brands", L"BRANDS"), renderer.device(),
                      renderer.context());

        if (!slider_initialized)
            diagnostics::warning("ui", "Morph slider shader unavailable; slides disabled.");
        if (!panel_initialized)
            diagnostics::warning("ui",
                                 "Rounded panel shader unavailable; using geometry fallback.");
        if (!cursor_initialized)
            diagnostics::warning("ui", "Glass cursor shader unavailable; custom cursor disabled.");
        else
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }

    void begin_frame(const platform::d3d11_renderer& renderer) const
    {
        snapshot::attach(renderer.device(), renderer.context(), renderer.swap_chain());
        fonts.update();
        images::update(renderer.device(), renderer.context());
    }

    void after_render(const platform::d3d11_renderer& renderer) const
    {
        snapshot::poll(renderer.device(), renderer.context(), renderer.swap_chain());
    }

  private:
    void shutdown() noexcept
    {
        if (!active_)
            return;

        images::shutdown();
        glass::cursor_shutdown();
        solace::rounded_panel::shutdown();
        slides::morph_slider_shutdown();
        avatars::shutdown();
        snapshot::shutdown();
        active_ = false;
    }

    bool active_ = false;
};

void draw_cursor()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    glass::cursor_options options = glass::settings();

    glass::cursor(ImGui::GetForegroundDrawList(), ImRect(ImVec2(0.f, 0.f), display), options);
}

void drag_host_window(HWND window)
{
    static bool dragging = false;
    static POINT drag_start_cursor{};
    static RECT drag_start_rect{};

    const bool can_start_drag = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
                                !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();
    if (can_start_drag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        dragging = true;
        ::GetCursorPos(&drag_start_cursor);
        ::GetWindowRect(window, &drag_start_rect);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        dragging = false;
    if (!dragging)
        return;

    POINT cursor{};
    ::GetCursorPos(&cursor);
    ::SetWindowPos(window, nullptr, drag_start_rect.left + (cursor.x - drag_start_cursor.x),
                   drag_start_rect.top + (cursor.y - drag_start_cursor.y), 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void sync_host_size(const platform::win32_window& window)
{
    const int width =
        ui_runtime::host_size.x > 1.f ? static_cast<int>(ui_runtime::host_size.x + 0.5f) : 1;
    const int height =
        ui_runtime::host_size.y > 1.f ? static_cast<int>(ui_runtime::host_size.y + 0.5f) : 1;
    window.set_client_size(
        platform::client_extent{static_cast<UINT>(width), static_cast<UINT>(height)});
}
} // namespace

int run_desktop_app()
{
    ImGui_ImplWin32_EnableDpiAwareness();
    const UINT initial_dpi = ::GetDpiForSystem();
    set_ui_dpi(initial_dpi, false);

    platform::window_config window_config;
    window_config.class_name = product_info::window_class;
    window_config.title = product_info::window_title;

    platform::win32_window window;
    if (!window.create(window_config, initial_dpi, handle_window_message))
    {
        diagnostics::error("window", "Failed to create the Win32 host window.",
                           static_cast<long>(::GetLastError()));
        return diagnostics::to_process_exit_code(diagnostics::exit_code::window_initialization);
    }

    platform::d3d11_renderer renderer;
    if (!renderer.initialize(window.native_handle()))
    {
        diagnostics::error("renderer", "Failed to initialize Direct3D 11.",
                           static_cast<long>(renderer.last_error()));
        return diagnostics::to_process_exit_code(diagnostics::exit_code::renderer_initialization);
    }

    window.show();

    imgui_session imgui;
    if (!imgui.initialize(window.native_handle(), renderer.device(), renderer.context()))
    {
        diagnostics::error("imgui", "Failed to initialize an ImGui platform backend.");
        return diagnostics::to_process_exit_code(diagnostics::exit_code::imgui_initialization);
    }

    ui_services services;
    services.initialize(renderer);
    diagnostics::info("runtime", "Solace started.");

    diagnostics::exit_code exit_code = diagnostics::exit_code::success;
    while (window.pump_messages())
    {
        const HRESULT visibility = renderer.test_occlusion();
        if (visibility == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        if (FAILED(visibility))
        {
            diagnostics::error("renderer", "Swap-chain visibility test failed.",
                               static_cast<long>(visibility));
            exit_code = diagnostics::exit_code::rendering_failure;
            break;
        }

        const std::optional<platform::client_extent> resize = window.take_pending_resize();
        if (resize && resize->width != 0 && resize->height != 0)
        {
            // Draw commands store raw ImTextureIDs. Release the old backdrop
            // before the swap-chain buffer and its SRV can be replaced.
            snapshot::invalidate_backdrop();
            if (!renderer.resize(resize->width, resize->height))
            {
                diagnostics::error("renderer", "Failed to resize the swap chain.",
                                   static_cast<long>(renderer.last_error()));
                exit_code = diagnostics::exit_code::rendering_failure;
                break;
            }
        }

        services.begin_frame(renderer);
        imgui.new_frame();

        solace::application::render_frame();
        ui_runtime::collect_animation_states();
        draw_cursor();
        drag_host_window(window.native_handle());
        sync_host_size(window);

        ImGui::Render();
        renderer.clear(std::array<float, 4>{0.f, 0.f, 0.f, 0.f});
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        services.after_render(renderer);
        const HRESULT present_result = renderer.present(1);
        if (FAILED(present_result))
        {
            diagnostics::error("renderer", "Failed to present the frame.",
                               static_cast<long>(present_result));
            exit_code = diagnostics::exit_code::rendering_failure;
            break;
        }
    }

    diagnostics::info("runtime", "Solace stopped.");
    return diagnostics::to_process_exit_code(exit_code);
}
} // namespace solace::runtime
