#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lib/bj_core.hpp"
#include "lib/platform/windows/capture/GdiScreenCapture.hpp"
#include "lib/platform/windows/led/WinBleLed.hpp"

class WinApp {
public:
    int run(HINSTANCE instance);

private:
    static LRESULT CALLBACK windowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void stop();
    void tick();
    bj::RGB selectedOutputColor(bj::RGB autoColor) const;
    void setOutputMode(int mode);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
    HWND swatch_ = nullptr;
    HWND outputButtons_[5] {};
    bj::Settings settings_;
    bj::ColorAnalyzer analyzer_;
    bj::RGB smoothed_;
    bj::RGB lastSent_;
    bj::FrameAnalysis frameAnalysis_;
    bool hasSmoothed_ = false;
    int outputMode_ = 0;
    GdiScreenCapture* capture_ = nullptr;
    WinBleLed led_;
};
