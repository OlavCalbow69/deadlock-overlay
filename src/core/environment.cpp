#include "core/environment.h"

#include "core/product_info.h"

#include <windows.h>

#include <vector>

namespace solace::environment
{
namespace
{
std::string read_variable(const std::string& name)
{
    const DWORD size = ::GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (size == 0)
        return {};

    std::vector<char> buffer(size);
    const DWORD written =
        ::GetEnvironmentVariableA(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    return written > 0 && written < buffer.size() ? std::string(buffer.data(), written)
                                                  : std::string{};
}

std::wstring read_variable(const std::wstring& name)
{
    const DWORD size = ::GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (size == 0)
        return {};

    std::vector<wchar_t> buffer(size);
    const DWORD written =
        ::GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    return written > 0 && written < buffer.size() ? std::wstring(buffer.data(), written)
                                                  : std::wstring{};
}
} // namespace

std::string value(std::string_view key)
{
    return read_variable(product_info::environment_prefix + std::string(key));
}

std::wstring value(std::wstring_view key)
{
    return read_variable(product_info::environment_prefix_wide + std::wstring(key));
}
} // namespace solace::environment
