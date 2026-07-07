#include "lib/platform/windows/app/WinApp.hpp"
#include "lib/platform/windows/led/WinBleLed.hpp"

#include <cwchar>
#include <cstdio>
#include <string>

namespace {
void writeStdout(const std::string& text) {
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), DWORD(text.size()), &written, nullptr);
}

std::string narrow(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (wchar_t c : value) out.push_back(c <= 0x7f ? char(c) : '?');
    return out;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (std::wcsstr(GetCommandLineW(), L"--ble-scan-child")) {
        for (const WinBleDeviceInfo& device : WinBleLed::scanInProcess()) {
            char line[256];
            std::snprintf(
                line,
                sizeof(line),
                "%012llX\t%d\t%s\n",
                static_cast<unsigned long long>(device.address),
                device.rssi,
                narrow(device.name).c_str());
            writeStdout(line);
        }
        return 0;
    }
    WinApp app;
    return app.run(instance);
}
