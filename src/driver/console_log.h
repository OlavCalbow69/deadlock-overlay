#pragma once
/*
 * console_log.h
 *
 * Lightweight coloured console logging used by the driver loader and game reader.
 * All helpers write to stdout and are safe to call from any thread (each call
 * flushes atomically through a single std::cout write).
 *
 * Colour codes only take effect on Windows 10+ consoles (VT enabled at startup).
 */

#include <Windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>

namespace clog
{
    // Call once before any log output to enable VT escape sequences.
    inline void enable_vt()
    {
        HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (::GetConsoleMode(h, &mode))
            ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // ── Colour constants (ANSI SGR) ───────────────────────────────────────────
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* DIM     = "\033[2m";
    constexpr const char* GREEN   = "\033[92m";
    constexpr const char* YELLOW  = "\033[93m";
    constexpr const char* RED     = "\033[91m";
    constexpr const char* CYAN    = "\033[96m";
    constexpr const char* MAGENTA = "\033[95m";
    constexpr const char* WHITE   = "\033[97m";
    constexpr const char* GRAY    = "\033[90m";

    // ── Prefix symbols ────────────────────────────────────────────────────────
    //  [+] OK / success
    //  [~] Info / neutral
    //  [!] Warning
    //  [x] Error / failure

    inline void ok(const std::string& msg)
    {
        std::cout << GREEN << "[+] " << RESET << msg << "\n";
    }
    inline void info(const std::string& msg)
    {
        std::cout << CYAN << "[~] " << RESET << msg << "\n";
    }
    inline void warn(const std::string& msg)
    {
        std::cout << YELLOW << "[!] " << RESET << msg << "\n";
    }
    inline void fail(const std::string& msg)
    {
        std::cout << RED << "[x] " << RESET << msg << "\n";
    }
    inline void section(const std::string& title)
    {
        std::cout << "\n" << BOLD << MAGENTA << "=== " << title << " ===" << RESET << "\n";
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    inline bool is_elevated()
    {
        BOOL elevated = FALSE;
        HANDLE token  = nullptr;
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            TOKEN_ELEVATION te{};
            DWORD           size{};
            if (::GetTokenInformation(token, TokenElevation, &te, sizeof(te), &size))
                elevated = te.TokenIsElevated;
            ::CloseHandle(token);
        }
        return elevated != FALSE;
    }
} // namespace clog
