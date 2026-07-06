#pragma once

#include <string>

#include "lib/bj_core.hpp"

class BlueZLed {
public:
    bool connect(const std::string& address);
    bool isReady() const;
    void write(bj::RGB color, int maxChannel);

private:
    std::string address_;
};
