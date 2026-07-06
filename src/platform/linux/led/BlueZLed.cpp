#include "lib/platform/linux/led/BlueZLed.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

bool BlueZLed::connect(const std::string& address) {
    address_ = address;
    return !address_.empty();
}

bool BlueZLed::isReady() const {
    return !address_.empty();
}

void BlueZLed::write(bj::RGB color, int maxChannel) {
    auto packet = bj::colorPacket(color, maxChannel);
    std::ostringstream hex;
    for (uint8_t byte : packet) {
        char part[3];
        std::snprintf(part, sizeof(part), "%02x", byte);
        hex << part;
    }

    const char* uuid = std::getenv("BJ_LED_CHAR_UUID");
    if (!uuid) uuid = "0000ee01-0000-1000-8000-00805f9b34fb";

    std::ostringstream command;
    command << "gatttool -b '" << address_ << "' --char-write-req --uuid='" << uuid << "' --value='" << hex.str() << "' >/dev/null 2>&1";
    const int rc = std::system(command.str().c_str());
    if (rc != 0) {
        std::cerr << "BlueZ write failed. Install bluez gatttool or set BJ_LED_CHAR_UUID. Packet=" << hex.str() << '\n';
    }
}
