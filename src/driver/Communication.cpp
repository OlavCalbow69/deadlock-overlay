#include "Communication.h"

communication::communication() {
    driver_handle = CreateFileA(
        "\\\\.\\RickOwens00",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
}

communication::~communication() {
    if (driver_handle != INVALID_HANDLE_VALUE)
        CloseHandle(driver_handle);
}

bool communication::is_connected() {
    return (driver_handle != INVALID_HANDLE_VALUE);
}

bool communication::v_attach(i32 pid) {
    _VRW args = {};
    args.security_code   = SECURITY_CODE;
    args.process_handle  = reinterpret_cast<HANDLE>(static_cast<intptr_t>(pid));
    return DeviceIoControl(
        driver_handle, VRW_ATTACH_CODE,
        &args, sizeof(args),
        &args, sizeof(args),
        nullptr, nullptr) != FALSE;
}

std::string communication::readstr(u64 address) {
    i32 len = read<i32>(address + 0x18);
    if (len >= 16)
        address = read<u64>(address);

    std::vector<i8> buf(256);
    _PRW args = {};
    args.security_code = SECURITY_CODE;
    args.address       = reinterpret_cast<void*>(address);
    args.buffer        = buf.data();
    args.size          = static_cast<u64>(buf.size());
    args.process_id    = process_id;
    args.Type          = false;
    DeviceIoControl(driver_handle, PRW_CODE, &args, sizeof(args), nullptr, 0, nullptr, nullptr);
    return std::string(buf.data());
}

i32 communication::find_process(const i8* process_name) {
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, process_name) == 0) {
                process_id = static_cast<i32>(pe.th32ProcessID);
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return process_id;
}

u64 communication::find_image() {
    u64 addr = 0;
    _BA args = {};
    args.security_code = SECURITY_CODE;
    args.process_id    = process_id;
    args.address       = &addr;
    DeviceIoControl(driver_handle, BA_CODE, &args, sizeof(args), nullptr, 0, nullptr, nullptr);
    image_address = addr;
    return addr;
}
