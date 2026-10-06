/*
 * driver_loader.cpp
 *
 * Maps the embedded RickOwens00 kernel driver via kdmapper if it is not
 * already running, logging every step to the console.
 */

#include "driver_loader.h"
#include "console_log.h"

#include "kdmapper/include/kdmapper.hpp"
#include "kdmapper/include/intel_driver.hpp"
#include "kdmapper/include/utils.hpp"

#include "raw_driver.h"   // rawData[]

#include <Windows.h>
#include <vector>
#include <string>

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────
namespace
{
    constexpr const char* k_device_path = "\\\\.\\RickOwens00";

    // Open and immediately close the RickOwens00 device symlink.
    bool probe_device()
    {
        HANDLE h = ::CreateFileA(
            k_device_path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        ::CloseHandle(h);
        return true;
    }

    // SEH filter: ensure the Intel vulnerable driver is unloaded on crash.
    LONG WINAPI loader_seh_filter(EXCEPTION_POINTERS* /*ep*/)
    {
        if (intel_driver::hDevice)
            intel_driver::Unload();
        return EXCEPTION_EXECUTE_HANDLER;
    }

    bool map_embedded_driver()
    {
        clog::info("Loading Intel vulnerable driver (kdmapper)...");

        NTSTATUS status = intel_driver::Load();
        if (!NT_SUCCESS(status))
        {
            clog::fail("intel_driver::Load() failed  (NTSTATUS 0x"
                + [status]{ char buf[12]; sprintf_s(buf, "%08X", (ULONG)status); return std::string(buf); }()
                + ")  — are you running as Administrator?");
            return false;
        }
        clog::ok("Intel vulnerable driver loaded");

        const std::vector<uint8_t> image(rawData, rawData + sizeof(rawData));
        clog::info("Mapping RickOwens00.sys from embedded blob ("
                   + std::to_string(image.size()) + " bytes)...");

        NTSTATUS exit_code   = 0;
        bool     map_success = false;

        __try
        {
            map_success = kdmapper::MapDriver(
                const_cast<BYTE*>(image.data()),
                0, 0,
                /*free=*/          false,
                /*destroyHeader=*/ true,
                kdmapper::AllocationMode::AllocatePool,
                /*passAllocPtr=*/  false,
                /*callback=*/      nullptr,
                &exit_code) != 0;
        }
        __except (loader_seh_filter(GetExceptionInformation()))
        {
            map_success = false;
        }

        intel_driver::Unload();
        clog::info("Intel vulnerable driver unloaded");

        if (!map_success)
        {
            clog::fail("kdmapper::MapDriver() failed  (driver exit code 0x"
                + [exit_code]{ char buf[12]; sprintf_s(buf, "%08X", (ULONG)exit_code); return std::string(buf); }()
                + ")");
            return false;
        }

        clog::ok("RickOwens00.sys mapped into kernel");
        return true;
    }
} // anonymous namespace

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────
namespace driver_loader
{
    bool ensure_loaded()
    {
        clog::section("Kernel Driver");

        // ── 1. Admin check ────────────────────────────────────────────────────
        if (clog::is_elevated())
            clog::ok("Running as Administrator");
        else
        {
            clog::warn("NOT running as Administrator — kdmapper requires elevation");
            clog::warn("Driver mapping will likely fail; restart with 'Run as administrator'");
        }

        // ── 2. Fast path: driver already accessible ───────────────────────────
        if (probe_device())
        {
            clog::ok("RickOwens00 device already accessible — skipping map");
            return true;
        }
        clog::info("RickOwens00 device not found, attempting to map...");

        // ── 3. Map the embedded driver ────────────────────────────────────────
        if (!map_embedded_driver())
        {
            clog::fail("Driver load FAILED — reads will fall back to ReadProcessMemory");
            return false;
        }

        // ── 4. Wait for the device symlink to appear ──────────────────────────
        for (int i = 0; i < 20; ++i)
        {
            if (probe_device())
            {
                clog::ok("RickOwens00 device is accessible");
                return true;
            }
            ::Sleep(5);
        }

        clog::fail("Driver mapped but device symlink never appeared");
        return false;
    }

    bool is_driver_accessible()
    {
        return probe_device();
    }
} // namespace driver_loader
