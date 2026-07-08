#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif

#include "lib/bj_core.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct WinBleDeviceInfo {
    uint64_t address = 0;
    std::wstring name;
    int rssi = -127;
};

class WinBleLed {
public:
    ~WinBleLed();
    bool connect(const wchar_t* address);
    bool connect(uint64_t address);
    bool isReady() const;
    bool write(bj::RGB color, int maxChannel);
    static std::vector<WinBleDeviceInfo> scan(int timeoutMs = 3000, int limit = 12);
    static std::vector<WinBleDeviceInfo> scanInProcess(int timeoutMs = 2500, int limit = 12);

private:
    void close();
    void closeUnlocked();
    bool connectKnownGattDevice();
    bool connectKnownGattDeviceUnlocked();

    mutable std::mutex mutex_;
    bool ready_ = false;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    struct CharacteristicStorage;
    CharacteristicStorage* characteristic_ = nullptr;
};
