#pragma once

#include <string>
#include <vector>
#include <mutex>

#include "lib/bj_core.hpp"

struct BlueZDeviceInfo {
    std::string address;
    std::string name;
    std::string objectPath;
    int rssi = -127;
};

class BlueZLed {
public:
    bool connect(const std::string& address);
    bool connect(const BlueZDeviceInfo& device);
    bool isReady() const;
    void write(bj::RGB color, int maxChannel);
    static std::vector<BlueZDeviceInfo> scan(int timeoutMs = 6000, int limit = 12);

private:
    bool resolveCharacteristic();
    bool resolveCharacteristicUnlocked();
    void disconnect();
    void disconnectUnlocked();

    mutable std::mutex mutex_;
    std::string address_;
    std::string objectPath_;
    std::string characteristicPath_;
    bool ready_ = false;
};
