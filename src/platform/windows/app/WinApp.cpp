#include "lib/platform/windows/app/WinApp.hpp"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <memory>
#include <thread>

#include "platform/windows/resource.h"

namespace {
COLORREF colorRef(unsigned char r, unsigned char g, unsigned char b) {
    return COLORREF(unsigned(r) | (unsigned(g) << 8U) | (unsigned(b) << 16U));
}

COLORREF colorRef(bj::RGB c) {
    return colorRef(c.r, c.g, c.b);
}

bool contains(const WinApp::Rect& rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

void fillRect(HDC dc, const WinApp::Rect& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    RECT native {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    FillRect(dc, &native, brush);
    DeleteObject(brush);
}

void frameRect(HDC dc, const WinApp::Rect& rect, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 18, 18);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void roundFill(HDC dc, const WinApp::Rect& rect, COLORREF color, int radius = 18) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void drawText(HDC dc, HFONT font, const wchar_t* text, const WinApp::Rect& rect, COLORREF color, UINT format) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT native {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    DrawTextW(dc, text, -1, &native, format);
    SelectObject(dc, oldFont);
}

int sliderX(const WinApp::Slider& slider) {
    const float t = (slider.value - slider.min) / std::max(0.001f, slider.max - slider.min);
    return slider.rect.x + int(std::clamp(t, 0.0f, 1.0f) * float(slider.rect.w));
}

std::wstring formatFloat(float value) {
    wchar_t text[32];
    std::swprintf(text, 32, L"%.2f", value);
    return text;
}

template <typename T>
void setDwmAttribute(HWND hwnd, DWORD attribute, const T& value) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto* fn = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (fn) fn(hwnd, attribute, &value, sizeof(T));
    FreeLibrary(dwm);
}
}

int WinApp::run(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSW wc {};
    wc.lpfnWndProc = &WinApp::windowProcThunk;
    wc.hInstance = instance_;
    wc.lpszClassName = L"BJLEDAmbilightWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_BJ_APP));
    wc.hbrBackground = CreateSolidBrush(colorRef(31, 34, 36));
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"BJ LED Ambilight",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        690,
        nullptr,
        nullptr,
        instance_,
        this);
    applyWindowEffects();
    ShowWindow(hwnd_, SW_SHOW);
    HICON icon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_BJ_APP));
    if (icon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK WinApp::windowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WinApp* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = reinterpret_cast<WinApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<WinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return app ? app->windowProc(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT WinApp::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            titleFont_ = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            labelFont_ = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            textFont_ = CreateFontW(21, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            monoFont_ = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
            RECT rect;
            GetClientRect(hwnd, &rect);
            resize(rect.right - rect.left, rect.bottom - rect.top);
            capture_ = new GdiScreenCapture(settings_);
            appendLog(L"Ready");
            SetTimer(hwnd, 1, std::max(8, 1000 / settings_.fps), nullptr);
            break;
        }
        case WM_ENTERSIZEMOVE:
            movingWindow_ = true;
            break;
        case WM_EXITSIZEMOVE:
            movingWindow_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_SIZE:
            resize(LOWORD(lparam), HIWORD(lparam));
            destroyBackbuffer();
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            handlePointer(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), true);
            break;
        case WM_MOUSEMOVE:
            if (wparam & MK_LBUTTON) handlePointer(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), false);
            break;
        case WM_LBUTTONUP:
            releasePointer();
            ReleaseCapture();
            break;
        case WM_TIMER:
            tick();
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_APP + 1:
            connected_ = wparam != 0;
            appendLog(wparam ? L"Strip ready, live writing enabled" : L"BJ_LED service not found");
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_APP + 2:
            deviceFound_ = wparam != 0;
            deviceLabel_ = deviceFound_ ? L"BJ_LED  ready" : L"No BJ_LED found";
            appendLog(deviceFound_ ? L"Found BJ_LED service" : L"BJ_LED not found. Pair it in Windows Bluetooth first.");
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            recreateBackbuffer(dc);
            paint(backbufferDc_);
            BitBlt(dc, 0, 0, width_, height_, backbufferDc_, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY:
            stop();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return 0;
}

void WinApp::resize(int width, int height) {
    width_ = std::max(width, 940);
    height_ = std::max(height, 650);
    rebuildLayout();
}

void WinApp::applyWindowEffects() {
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
    const BOOL dark = TRUE;
    setDwmAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, dark);
    const DWORD rounded = 2;
    setDwmAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, rounded);
    const DWORD mica = 2;
    setDwmAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, mica);
}

void WinApp::recreateBackbuffer(HDC dc) {
    if (backbufferDc_ && backbufferBitmap_ && backbufferWidth_ == width_ && backbufferHeight_ == height_) return;
    destroyBackbuffer();
    backbufferDc_ = CreateCompatibleDC(dc);
    backbufferBitmap_ = CreateCompatibleBitmap(dc, width_, height_);
    oldBackbufferBitmap_ = SelectObject(backbufferDc_, backbufferBitmap_);
    backbufferWidth_ = width_;
    backbufferHeight_ = height_;
}

void WinApp::destroyBackbuffer() {
    if (backbufferDc_ && oldBackbufferBitmap_) {
        SelectObject(backbufferDc_, oldBackbufferBitmap_);
        oldBackbufferBitmap_ = nullptr;
    }
    if (backbufferBitmap_) {
        DeleteObject(backbufferBitmap_);
        backbufferBitmap_ = nullptr;
    }
    if (backbufferDc_) {
        DeleteDC(backbufferDc_);
        backbufferDc_ = nullptr;
    }
    backbufferWidth_ = 0;
    backbufferHeight_ = 0;
}

void WinApp::rebuildLayout() {
    const int left = 36;
    const int fieldX = 150;
    deviceRect_ = {fieldX, 92, 350, 32};
    scanRect_ = {518, 92, 110, 32};
    connectRect_ = {640, 92, 122, 32};
    displayRect_ = {fieldX, 144, 250, 32};
    modeRect_ = {498, 144, 176, 32};
    max127Rect_ = {704, 145, 64, 30};
    max255Rect_ = {768, 145, 64, 30};
    previewRect_ = {730, 244, 132, 132};
    logRect_ = {28, height_ - 126, width_ - 56, 106};

    const wchar_t* labels[5] {L"FPS", L"Brightness", L"Saturation", L"Smoothing", L"Threshold"};
    const float mins[5] {1.0f, 0.15f, 0.50f, 0.01f, 0.0f};
    const float maxs[5] {30.0f, 1.0f, 2.5f, 1.0f, 35.0f};
    const float values[5] {float(settings_.fps), settings_.brightness, settings_.saturation, settings_.smoothing, settings_.threshold};
    for (int i = 0; i < 5; ++i) {
            sliders_[i] = {{fieldX, 248 + i * 54, 480, 26}, labels[i], mins[i], maxs[i], values[i]};
    }

    const int segY = 466;
    int segX = 28;
    const int widths[5] {78, 82, 92, 82, 92};
    for (int i = 0; i < 5; ++i) {
        outputRects_[i] = {segX, segY, widths[i], 32};
        segX += widths[i] + 4;
    }
}

void WinApp::appendLog(const wchar_t* line) {
    logs_.push_back(line);
    if (logs_.size() > 6) logs_.erase(logs_.begin());
}

void WinApp::paint(HDC dc) {
    RECT client {0, 0, width_, height_};
    HBRUSH bg = CreateSolidBrush(colorRef(37, 40, 41));
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    roundFill(dc, {30, 28, 38, 38}, colorRef(4, 15, 38), 14);
    drawText(dc, textFont_, L"BJ", {34, 35, 30, 26}, colorRef(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, titleFont_, L"BJ LED Ambilight", {80, 24, 430, 50}, colorRef(245, 245, 247), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    drawText(dc, labelFont_, L"Device", {28, 94, 110, 28}, colorRef(244, 244, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, labelFont_, L"Display", {28, 146, 110, 28}, colorRef(244, 244, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, labelFont_, L"Mode", {428, 146, 70, 28}, colorRef(244, 244, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    roundFill(dc, deviceRect_, colorRef(73, 76, 77));
    roundFill(dc, displayRect_, colorRef(73, 76, 77));
    roundFill(dc, modeRect_, colorRef(73, 76, 77));
    roundFill(dc, scanRect_, colorRef(72, 75, 77));
    roundFill(dc, connectRect_, colorRef(72, 75, 77));
    roundFill(dc, max127Rect_, settings_.maxChannel == 127 ? colorRef(30, 132, 240) : colorRef(72, 75, 77));
    roundFill(dc, max255Rect_, settings_.maxChannel == 255 ? colorRef(30, 132, 240) : colorRef(72, 75, 77));

    drawText(dc, textFont_, deviceLabel_.c_str(), {deviceRect_.x + 16, deviceRect_.y, deviceRect_.w - 32, deviceRect_.h}, colorRef(245, 245, 245), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, displayLabel_.c_str(), {displayRect_.x + 16, displayRect_.y, displayRect_.w - 32, displayRect_.h}, colorRef(245, 245, 245), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, modeTitle(), {modeRect_.x + 16, modeRect_.y, modeRect_.w - 32, modeRect_.h}, colorRef(245, 245, 245), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, L"Scan", scanRect_, colorRef(245, 245, 245), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, L"Connect", connectRect_, colorRef(245, 245, 245), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, L"127", max127Rect_, colorRef(245, 245, 245), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawText(dc, textFont_, L"255", max255Rect_, colorRef(245, 245, 245), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (const Slider& slider : sliders_) {
        const int rowY = slider.rect.y;
        drawText(dc, labelFont_, slider.label, {28, rowY + 2, 116, 24}, colorRef(245, 245, 245), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        roundFill(dc, {slider.rect.x, slider.rect.y + 10, slider.rect.w, 6}, colorRef(82, 86, 88), 7);
        const int knob = sliderX(slider);
        roundFill(dc, {slider.rect.x, slider.rect.y + 10, std::max(1, knob - slider.rect.x), 6}, colorRef(30, 132, 240), 7);
        roundFill(dc, {knob - 11, slider.rect.y + 2, 22, 22}, colorRef(232, 234, 236), 24);
        std::wstring value = formatFloat(slider.value);
        drawText(dc, textFont_, value.c_str(), {650, rowY + 2, 62, 24}, colorRef(245, 245, 245), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    const wchar_t* modes[5] {L"Auto", L"Red", L"Green", L"Blue", L"White"};
    for (int i = 0; i < 5; ++i) {
        roundFill(dc, outputRects_[i], outputMode_ == i ? colorRef(30, 132, 240) : colorRef(72, 75, 77), 12);
        drawText(dc, textFont_, modes[i], outputRects_[i], colorRef(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    roundFill(dc, previewRect_, colorRef(smoothed_), 18);
    frameRect(dc, previewRect_, colorRef(34, 87, 120));
    const int sw = 28;
    const int gap = 8;
    const std::array<bj::RGB, 4> corners = frameAnalysis_.corners;
    const std::array<Rect, 4> swatches {{
        {previewRect_.x - sw - gap, previewRect_.y + previewRect_.h - sw, sw, sw},
        {previewRect_.x + previewRect_.w + gap, previewRect_.y + previewRect_.h - sw, sw, sw},
        {previewRect_.x - sw - gap, previewRect_.y, sw, sw},
        {previewRect_.x + previewRect_.w + gap, previewRect_.y, sw, sw},
    }};
    for (int i = 0; i < 4; ++i) {
        roundFill(dc, swatches[i], colorRef(corners[i]), 10);
        frameRect(dc, swatches[i], colorRef(58, 72, 92));
    }

    wchar_t rgb[80];
    std::swprintf(rgb, 80, L"RGB %u %u %u", smoothed_.r, smoothed_.g, smoothed_.b);
    drawText(dc, labelFont_, rgb, {694, 385, 210, 26}, colorRef(245, 245, 245), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    roundFill(dc, logRect_, colorRef(22, 27, 29), 14);
    HRGN clip = CreateRectRgn(logRect_.x + 8, logRect_.y + 8, logRect_.x + logRect_.w - 8, logRect_.y + logRect_.h - 8);
    SelectClipRgn(dc, clip);
    int y = logRect_.y + 12;
    for (const std::wstring& line : logs_) {
        drawText(dc, monoFont_, line.c_str(), {logRect_.x + 16, y, logRect_.w - 32, 22}, colorRef(222, 226, 229), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += 22;
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void WinApp::handlePointer(int x, int y, bool pressed) {
    if (pressed) {
        for (int i = 0; i < int(sliders_.size()); ++i) {
            if (contains(sliders_[i].rect, x, y) || (x >= sliders_[i].rect.x && x <= sliders_[i].rect.x + sliders_[i].rect.w && y >= sliders_[i].rect.y - 12 && y <= sliders_[i].rect.y + 36)) {
                activeSlider_ = i;
                updateSlider(x);
                return;
            }
        }
        for (int i = 0; i < int(outputRects_.size()); ++i) {
            if (contains(outputRects_[i], x, y)) {
                setOutputMode(i);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        if (contains(max127Rect_, x, y)) settings_.maxChannel = 127;
        if (contains(max255Rect_, x, y)) settings_.maxChannel = 255;
            if (contains(deviceRect_, x, y)) {
                appendLog(deviceFound_ ? L"BJ_LED selected" : L"No scanned device yet");
            }
            if (contains(displayRect_, x, y)) {
                appendLog(L"Capturing the Windows virtual desktop");
            }
            if (contains(modeRect_, x, y)) {
                cycleMode();
            }
            if (contains(scanRect_, x, y) && !scanInFlight_.exchange(true)) {
                deviceLabel_ = L"Scanning...";
                appendLog(L"Scanning paired BLE GATT services...");
                appendLog(L"Windows requires BJ_LED to be paired in Bluetooth Settings first.");
                std::thread([this] {
                    WinBleLed probe;
                    const bool ok = probe.connect(nullptr);
                    scanInFlight_ = false;
                    PostMessageW(hwnd_, WM_APP + 2, ok ? 1 : 0, 0);
                }).detach();
            }
            if (contains(connectRect_, x, y) && !connectInFlight_.exchange(true)) {
                appendLog(L"Connecting to BJ_LED...");
                std::thread([this] {
                    const bool ok = led_.connect(nullptr);
                    connectInFlight_ = false;
                    PostMessageW(hwnd_, WM_APP + 1, ok ? 1 : 0, 0);
                }).detach();
            }
    } else if (activeSlider_ >= 0) {
        updateSlider(x);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WinApp::releasePointer() {
    activeSlider_ = -1;
}

void WinApp::updateSlider(int x) {
    if (activeSlider_ < 0) return;
    Slider& slider = sliders_[activeSlider_];
    const float t = std::clamp(float(x - slider.rect.x) / std::max(1, slider.rect.w), 0.0f, 1.0f);
    slider.value = slider.min + (slider.max - slider.min) * t;
    if (activeSlider_ == 0) {
        settings_.fps = std::clamp(int(slider.value + 0.5f), 1, 30);
        slider.value = float(settings_.fps);
        KillTimer(hwnd_, 1);
        SetTimer(hwnd_, 1, std::max(8, 1000 / settings_.fps), nullptr);
    } else if (activeSlider_ == 1) {
        settings_.brightness = slider.value;
    } else if (activeSlider_ == 2) {
        settings_.saturation = slider.value;
    } else if (activeSlider_ == 3) {
        settings_.smoothing = slider.value;
    } else if (activeSlider_ == 4) {
        settings_.threshold = slider.value;
    }
    hasSmoothed_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WinApp::cycleMode() {
    modeIndex_ = (modeIndex_ + 1) % 3;
    switch (modeIndex_) {
        case 1:
            settings_.mode = bj::SampleMode::Average;
            break;
        case 2:
            settings_.mode = bj::SampleMode::Vibrant;
            break;
        default:
            settings_.mode = bj::SampleMode::Balanced;
            break;
    }
    hasSmoothed_ = false;
}

void WinApp::stop() {
    KillTimer(hwnd_, 1);
    destroyBackbuffer();
    delete capture_;
    capture_ = nullptr;
    if (titleFont_) DeleteObject(titleFont_);
    if (labelFont_) DeleteObject(labelFont_);
    if (textFont_) DeleteObject(textFont_);
    if (monoFont_) DeleteObject(monoFont_);
}

void WinApp::tick() {
    if (!capture_ || movingWindow_) return;
    auto pixels = capture_->capture();
    frameAnalysis_ = analyzer_.analyzeFrame(pixels, settings_.sampleWidth, settings_.sampleHeight, settings_);
    bj::RGB color = selectedOutputColor(frameAnalysis_.output);
    const bj::RGB next = outputMode_ == 0 && hasSmoothed_ ? bj::smooth(smoothed_, color, settings_.smoothing) : color;
    const bool visualChanged = !hasSmoothed_ || bj::distance(smoothed_, next) >= 1.0f;
    smoothed_ = next;
    hasSmoothed_ = true;
    const auto now = std::chrono::steady_clock::now();
    const bool forceRefresh = now - lastWriteTime_ >= std::chrono::milliseconds(250);
    const bool colorChanged = bj::distance(lastSent_, smoothed_) >= settings_.threshold;
    const bool writeWindowOpen = now - lastWriteTime_ >= std::chrono::milliseconds(90);
    if (led_.isReady() && !connectInFlight_ && writeWindowOpen && (colorChanged || forceRefresh) && !writeInFlight_.exchange(true)) {
        const bj::RGB color = smoothed_;
        const int maxChannel = settings_.maxChannel;
        lastSent_ = color;
        lastWriteTime_ = now;
        std::thread([this, color, maxChannel] {
            led_.write(color, maxChannel);
            writeInFlight_ = false;
        }).detach();
    }
    if (visualChanged) InvalidateRect(hwnd_, nullptr, FALSE);
}

bj::RGB WinApp::selectedOutputColor(bj::RGB autoColor) const {
    switch (outputMode_) {
        case 1: return bj::RGB(255, 0, 0);
        case 2: return bj::RGB(0, 255, 0);
        case 3: return bj::RGB(0, 0, 255);
        case 4: return bj::RGB(255, 255, 255);
        default: return autoColor;
    }
}

void WinApp::setOutputMode(int mode) {
    outputMode_ = mode;
    hasSmoothed_ = false;
}

const wchar_t* WinApp::modeTitle() const {
    switch (settings_.mode) {
        case bj::SampleMode::Average: return L"Average";
        case bj::SampleMode::Vibrant: return L"Vibrant";
        case bj::SampleMode::Balanced:
        default: return L"Balanced";
    }
}
