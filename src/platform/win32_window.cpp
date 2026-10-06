#include "platform/win32_window.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace solace::platform
{
win32_window::~win32_window()
{
    reset();
}

bool win32_window::create(const window_config& config, UINT initial_dpi, message_handler handler,
                          void* handler_context)
{
    reset();

    if (!config.class_name || !config.title || config.logical_width <= 0 ||
        config.logical_height <= 0)
        return false;

    instance_ = ::GetModuleHandleW(nullptr);
    class_name_ = config.class_name;
    handler_ = handler;
    handler_context_ = handler_context;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance_;
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name_.c_str();

    class_atom_ = ::RegisterClassExW(&window_class);
    if (!class_atom_)
    {
        reset();
        return false;
    }

    const UINT dpi = initial_dpi > 0 ? initial_dpi : USER_DEFAULT_SCREEN_DPI;
    const int width =
        ::MulDiv(config.logical_width, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int height =
        ::MulDiv(config.logical_height, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int x = (::GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (::GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    const HWND created =
        ::CreateWindowExW(config.extended_style, class_name_.c_str(), config.title, config.style, x,
                          y, width, height, nullptr, nullptr, instance_, this);
    if (!created)
    {
        reset();
        return false;
    }
    window_ = created;

    ::SetLayeredWindowAttributes(window_, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margins{-1};
    ::DwmExtendFrameIntoClientArea(window_, &margins);
    return true;
}

void win32_window::show() const
{
    if (!window_)
        return;

    ::ShowWindow(window_, SW_SHOWDEFAULT);
    ::UpdateWindow(window_);
}

bool win32_window::pump_messages() const
{
    bool quit = false;
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE))
    {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
        quit = quit || message.message == WM_QUIT;
    }
    return !quit;
}

std::optional<client_extent> win32_window::take_pending_resize()
{
    std::optional<client_extent> result = pending_resize_;
    pending_resize_.reset();
    return result;
}

void win32_window::set_client_size(client_extent size) const
{
    if (!window_)
        return;

    RECT client_rect{};
    if (::GetClientRect(window_, &client_rect) &&
        static_cast<UINT>(client_rect.right - client_rect.left) == size.width &&
        static_cast<UINT>(client_rect.bottom - client_rect.top) == size.height)
        return;

    ::SetWindowPos(window_, nullptr, 0, 0, static_cast<int>(size.width),
                   static_cast<int>(size.height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND win32_window::native_handle() const noexcept
{
    return window_;
}

LRESULT CALLBACK win32_window::window_proc(HWND window, UINT message, WPARAM w_param,
                                           LPARAM l_param)
{
    win32_window* self =
        reinterpret_cast<win32_window*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        self = create ? static_cast<win32_window*>(create->lpCreateParams) : nullptr;
        if (self)
        {
            self->window_ = window;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }

    if (!self)
        return ::DefWindowProcW(window, message, w_param, l_param);

    const LRESULT result = self->dispatch_message(window, message, w_param, l_param);
    if (message == WM_NCDESTROY)
    {
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->window_ = nullptr;
    }
    return result;
}

LRESULT win32_window::dispatch_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (handler_)
    {
        const std::optional<LRESULT> handled =
            handler_(handler_context_, window, message, w_param, l_param);
        if (handled)
            return *handled;
    }

    switch (message)
    {
    case WM_DPICHANGED:
    {
        const RECT* suggested = reinterpret_cast<const RECT*>(l_param);
        if (suggested)
        {
            ::SetWindowPos(window, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_SIZE:
        if (w_param == SIZE_MINIMIZED)
            return 0;
        pending_resize_ =
            client_extent{static_cast<UINT>(LOWORD(l_param)), static_cast<UINT>(HIWORD(l_param))};
        return 0;

    case WM_SYSCOMMAND:
        if ((w_param & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(window, message, w_param, l_param);
}

void win32_window::reset() noexcept
{
    if (window_)
    {
        if (::IsWindow(window_))
            ::DestroyWindow(window_);
        window_ = nullptr;
    }

    if (class_atom_ && instance_ && !class_name_.empty())
        ::UnregisterClassW(class_name_.c_str(), instance_);

    class_atom_ = 0;
    instance_ = nullptr;
    class_name_.clear();
    handler_ = nullptr;
    handler_context_ = nullptr;
    pending_resize_.reset();
}
} // namespace solace::platform
