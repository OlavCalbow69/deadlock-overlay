#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace solace::platform
{
struct client_extent
{
    UINT width = 0;
    UINT height = 0;
};

struct window_config
{
    const wchar_t* class_name = L"DesktopWindow";
    const wchar_t* title = L"Desktop application";
    int logical_width = 384;
    int logical_height = 558;
    DWORD style = WS_POPUP;
    DWORD extended_style = WS_EX_LAYERED;
};

using message_handler = std::optional<LRESULT> (*)(void* context, HWND window, UINT message,
                                                   WPARAM w_param, LPARAM l_param);

class win32_window final
{
  public:
    win32_window() = default;
    ~win32_window();

    win32_window(const win32_window&) = delete;
    win32_window& operator=(const win32_window&) = delete;

    [[nodiscard]] bool create(const window_config& config, UINT initial_dpi,
                              message_handler handler = nullptr, void* handler_context = nullptr);
    void show() const;

    [[nodiscard]] bool pump_messages() const;
    [[nodiscard]] std::optional<client_extent> take_pending_resize();

    void set_client_size(client_extent size) const;
    [[nodiscard]] HWND native_handle() const noexcept;

  private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT dispatch_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    void reset() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    ATOM class_atom_ = 0;
    std::wstring class_name_;
    message_handler handler_ = nullptr;
    void* handler_context_ = nullptr;
    std::optional<client_extent> pending_resize_;
};
} // namespace solace::platform
