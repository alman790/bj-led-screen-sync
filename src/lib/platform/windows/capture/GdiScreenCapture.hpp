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

#include <span>
#include <vector>

#include "lib/bj_core.hpp"

class GdiScreenCapture {
public:
    explicit GdiScreenCapture(bj::Settings settings);
    ~GdiScreenCapture();
    void updateSettings(bj::Settings settings);
    std::span<const bj::RGB> capture();

private:
    void recreateBuffer();

    bj::Settings settings_;
    std::vector<bj::RGB> pixels_;
    HDC screen_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* dibPixels_ = nullptr;
};
