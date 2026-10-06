#pragma once
#include <Windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <string>
#include <vector>

// ── Type aliases (from RickOwens00 Definitions.h) ────────────────────────────
typedef char           i8;
typedef short          i16;
typedef int            i32;
typedef long long      i64;
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef float  f32;
typedef double f64;

// ── IOCTL codes (must match driver/project/source/definitions.h) ─────────────
#define PRW_CODE        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x2ec33, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VRW_ATTACH_CODE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x2ec34, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define VRW_CODE        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x2ec35, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define BA_CODE         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x2ec36, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SECURITY_CODE   0x94c9e4bc3ULL

// ── Request structures ────────────────────────────────────────────────────────
struct _PRW {
    u64   security_code;
    i32   process_id;
    void* address;
    void* buffer;
    u64   size;
    u64   return_size;
    bool  Type;
};

struct _VRW {
    u64    security_code;
    HANDLE process_handle;
    void*  address;
    void*  buffer;
    u64    size;
    u64    return_size;
    bool   Type;
};

struct _BA {
    u64  security_code;
    i32  process_id;
    u64* address;
};

// ── Communication class ───────────────────────────────────────────────────────
class communication {
public:
    communication();
    ~communication();

    bool is_connected();
    bool v_attach(i32 process_id);

    i32 find_process(const i8* ProcessName);
    u64 find_image();

    template <typename T> T    v_read(u64 address);
    template <typename T> void v_write(u64 address, T& Value);
    template <typename T> T    read(u64 address);
    template <typename T> void write(u64 address, T& Value);

    std::string readstr(u64 address);

    u64 image_address = 0;
    i32 process_id    = 0;

private:
    HANDLE driver_handle = INVALID_HANDLE_VALUE;
};

// ── Template implementations ──────────────────────────────────────────────────
template <typename T>
T communication::v_read(u64 address) {
    T temp = {};
    _VRW args;
    args.security_code  = SECURITY_CODE;
    args.address        = reinterpret_cast<void*>(address);
    args.buffer         = &temp;
    args.size           = sizeof(T);
    args.Type           = false;
    DeviceIoControl(driver_handle, VRW_CODE, &args, sizeof(args), &args, sizeof(args), nullptr, nullptr);
    return temp;
}

template <typename T>
void communication::v_write(u64 address, T& value) {
    _VRW args;
    args.security_code  = SECURITY_CODE;
    args.address        = reinterpret_cast<void*>(address);
    args.buffer         = (void*)&value;
    args.size           = sizeof(T);
    args.Type           = true;
    DeviceIoControl(driver_handle, VRW_CODE, &args, sizeof(args), &args, sizeof(args), nullptr, nullptr);
}

template <typename T>
T communication::read(u64 address) {
    T temp = {};
    _PRW args = {};
    args.security_code  = SECURITY_CODE;
    args.address        = reinterpret_cast<void*>(address);
    args.buffer         = &temp;
    args.size           = sizeof(T);
    args.process_id     = process_id;
    args.Type           = false;
    DeviceIoControl(driver_handle, PRW_CODE, &args, sizeof(args), nullptr, 0, nullptr, nullptr);
    return temp;
}

template <typename T>
void communication::write(u64 address, T& value) {
    _PRW args = {};
    args.security_code  = SECURITY_CODE;
    args.address        = reinterpret_cast<void*>(address);
    args.buffer         = (void*)&value;
    args.size           = sizeof(T);
    args.process_id     = process_id;
    args.Type           = true;
    DeviceIoControl(driver_handle, PRW_CODE, &args, sizeof(args), nullptr, 0, nullptr, nullptr);
}
