#pragma once
#include <Windows.h>
#include <cstdint>

namespace driver_loader
{
    // Attempts to map the embedded RickOwens00 driver using kdmapper.
    // Returns true if the driver was already running or was mapped successfully.
    // Should be called once before any communication::is_connected() check.
    bool ensure_loaded();

    // Returns true if the RickOwens00 device is currently accessible.
    bool is_driver_accessible();
}
