#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#ifdef RGB
#undef RGB
#endif

#include "lib/bj_core.hpp"
#include "lib/platform/windows/capture/GdiScreenCapture.hpp"
#include "lib/platform/windows/led/WinBleLed.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

class WinApp {
public:
    struct Rect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct Slider {
        Rect rect;
        const wchar_t* label = L"";
        float min = 0;
        float max = 1;
        float value = 0;
    };

    int run(HINSTANCE instance);

private:
    static LRESULT CALLBACK windowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void stop();
    void joinWorkers();
    void tick();
    void paint(HDC dc);
    void resize(int width, int height);
    void applyWindowEffects();
    void recreateBackbuffer(HDC dc);
    void destroyBackbuffer();
    void rebuildLayout();
    void appendLog(const wchar_t* line);
    void handlePointer(int x, int y, bool pressed);
    void releasePointer();
    void updateSlider(int x);
    void cycleMode();
    bj::RGB selectedOutputColor(bj::RGB autoColor) const;
    void setOutputMode(int mode);
    const wchar_t* modeTitle() const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT labelFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT monoFont_ = nullptr;
    HDC backbufferDc_ = nullptr;
    HBITMAP backbufferBitmap_ = nullptr;
    HGDIOBJ oldBackbufferBitmap_ = nullptr;
    int backbufferWidth_ = 0;
    int backbufferHeight_ = 0;
    int width_ = 1200;
    int height_ = 820;
    Rect deviceRect_;
    Rect displayRect_;
    Rect modeRect_;
    Rect scanRect_;
    Rect connectRect_;
    Rect max127Rect_;
    Rect max255Rect_;
    Rect previewRect_;
    Rect logRect_;
    std::array<Rect, 5> outputRects_ {};
    std::array<Slider, 5> sliders_ {};
    int activeSlider_ = -1;
    int modeIndex_ = 0;
    bj::Settings settings_;
    bj::ColorAnalyzer analyzer_;
    bj::RGB smoothed_;
    bj::RGB lastSent_;
    bj::FrameAnalysis frameAnalysis_;
    std::chrono::steady_clock::time_point lastWriteTime_ {};
    bool hasSmoothed_ = false;
    int outputMode_ = 0;
    bool connected_ = false;
    bool deviceFound_ = false;
    bool movingWindow_ = false;
    uint64_t selectedAddress_ = 0;
    std::atomic_bool writeInFlight_ {false};
    std::atomic_bool connectInFlight_ {false};
    std::atomic_bool scanInFlight_ {false};
    std::atomic_bool stopping_ {false};
    std::thread connectThread_;
    std::thread writeThread_;
    GdiScreenCapture* capture_ = nullptr;
    WinBleLed led_;
    std::wstring deviceLabel_ = L"No device yet";
    std::wstring displayLabel_ = L"Virtual desktop";
    std::vector<std::wstring> logs_;
};
