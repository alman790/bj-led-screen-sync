#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace bj::ble {

inline std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return value;
}

inline bool isBjLedName(const std::string& name) {
    return name == "BJ_LED" || name == "BJ_LED_M" || name.find("BJ_LED") != std::string::npos;
}

inline bool isEe01Uuid(std::string uuid) {
    uuid = lowerAscii(uuid);
    return uuid == "ee01"
        || uuid == "0xee01"
        || uuid == "0000ee01"
        || uuid == "0000ee01-0000-1000-8000-00805f9b34fb";
}

inline std::string formatBluetoothAddress(uint64_t address) {
    char text[18];
    std::snprintf(
        text,
        sizeof(text),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        unsigned((address >> 40) & 0xff),
        unsigned((address >> 32) & 0xff),
        unsigned((address >> 24) & 0xff),
        unsigned((address >> 16) & 0xff),
        unsigned((address >> 8) & 0xff),
        unsigned(address & 0xff));
    return text;
}

inline std::optional<uint64_t> parseBluetoothAddress(const std::string& text) {
    std::string hex;
    hex.reserve(12);
    for (unsigned char c : text) {
        if (std::isxdigit(c)) hex.push_back(char(c));
    }
    if (hex.size() != 12) return std::nullopt;
    uint64_t value = 0;
    for (char c : hex) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= uint64_t(c - '0');
        else if (c >= 'a' && c <= 'f') value |= uint64_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= uint64_t(c - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

}  // namespace bj::ble
