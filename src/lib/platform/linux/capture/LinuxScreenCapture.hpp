#pragma once

#include <span>
#include <vector>

#include "lib/bj_core.hpp"

class LinuxScreenCapture {
public:
    explicit LinuxScreenCapture(bj::Settings settings);
    ~LinuxScreenCapture();
    void updateSettings(bj::Settings settings);
    std::span<const bj::RGB> capture();
    bool isAvailable() const;

private:
    void openDisplay();

    bj::Settings settings_;
    std::vector<bj::RGB> pixels_;
    bool available_ = false;
    [[maybe_unused]] void* display_ = nullptr;
    [[maybe_unused]] unsigned long root_ = 0;
    [[maybe_unused]] int sourceX_ = 0;
    [[maybe_unused]] int sourceY_ = 0;
    [[maybe_unused]] int sourceWidth_ = 0;
    [[maybe_unused]] int sourceHeight_ = 0;
};
