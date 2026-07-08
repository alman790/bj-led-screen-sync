#include "lib/platform/linux/app/LinuxApp.hpp"

#include "lib/bj_core.hpp"
#include "lib/platform/linux/capture/LinuxScreenCapture.hpp"
#include "lib/platform/linux/led/BlueZLed.hpp"

#if defined(__linux__) && __has_include(<X11/Xlib.h>)
#define BJ_LED_HAS_X11_UI 1
#include <X11/Xlib.h>
#else
#define BJ_LED_HAS_X11_UI 0
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct Slider {
    Rect rect;
    const char* label = "";
    float min = 0;
    float max = 1;
    float value = 0;
};

[[maybe_unused]] bool contains(const Rect& rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

[[maybe_unused]] bj::RGB selectedOutputColor(int mode, bj::RGB autoColor) {
    switch (mode) {
        case 1: return bj::RGB(255, 0, 0);
        case 2: return bj::RGB(0, 255, 0);
        case 3: return bj::RGB(0, 0, 255);
        case 4: return bj::RGB(255, 255, 255);
        default: return autoColor;
    }
}

#if BJ_LED_HAS_X11_UI
unsigned long rgb(Display* display, bj::RGB color) {
    (void)display;
    return (static_cast<unsigned long>(color.r) << 16U)
        | (static_cast<unsigned long>(color.g) << 8U)
        | static_cast<unsigned long>(color.b);
}

unsigned long rgb(Display* display, unsigned char r, unsigned char g, unsigned char b) {
    return rgb(display, bj::RGB(r, g, b));
}

void fill(Display* display, Drawable target, GC gc, const Rect& rect, unsigned long color) {
    XSetForeground(display, gc, color);
    XFillRectangle(display, target, gc, rect.x, rect.y, unsigned(rect.w), unsigned(rect.h));
}

void border(Display* display, Drawable target, GC gc, const Rect& rect, unsigned long color) {
    XSetForeground(display, gc, color);
    XDrawRectangle(display, target, gc, rect.x, rect.y, unsigned(rect.w), unsigned(rect.h));
}

void text(Display* display, Drawable target, GC gc, XFontStruct* font, int x, int y, const char* value, unsigned long color) {
    if (font) XSetFont(display, gc, font->fid);
    XSetForeground(display, gc, color);
    XDrawString(display, target, gc, x, y, value, int(std::char_traits<char>::length(value)));
}

void setClip(Display* display, GC gc, const Rect& rect) {
    XRectangle clip {
        static_cast<short>(rect.x),
        static_cast<short>(rect.y),
        static_cast<unsigned short>(std::max(0, rect.w)),
        static_cast<unsigned short>(std::max(0, rect.h)),
    };
    XSetClipRectangles(display, gc, 0, 0, &clip, 1, Unsorted);
}

void clearClip(Display* display, GC gc) {
    XSetClipMask(display, gc, None);
}

int sliderX(const Slider& slider) {
    const float t = (slider.value - slider.min) / std::max(0.001f, slider.max - slider.min);
    return slider.rect.x + int(std::clamp(t, 0.0f, 1.0f) * float(slider.rect.w));
}

class X11AmbilightWindow {
public:
    X11AmbilightWindow(const std::string& address, const std::string& initialMode)
        : address_(address), capture_(settings_) {
        if (initialMode == "red") outputMode_ = 1;
        if (initialMode == "green") outputMode_ = 2;
        if (initialMode == "blue") outputMode_ = 3;
        if (initialMode == "white") outputMode_ = 4;
    }

    int run() {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            std::cerr << "X11 display is unavailable. Run under X11 or XWayland.\n";
            return 1;
        }

        const int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(
            display_,
            RootWindow(display_, screen),
            60,
            60,
            unsigned(width_),
            unsigned(height_),
            1,
            rgb(display_, 100, 108, 112),
            rgb(display_, 37, 40, 41));
        XStoreName(display_, window_, "BJ LED Ambilight");
        wmDelete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display_, window_, &wmDelete_, 1);
        XSelectInput(display_, window_, ExposureMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask | StructureNotifyMask);
        XMapWindow(display_, window_);
        gc_ = XCreateGC(display_, window_, 0, nullptr);
        backbuffer_ = XCreatePixmap(display_, window_, unsigned(width_), unsigned(height_), unsigned(DefaultDepth(display_, screen)));
        titleFont_ = XLoadQueryFont(display_, "-*-helvetica-bold-r-normal-*-34-*-*-*-*-*-*-*");
        labelFont_ = XLoadQueryFont(display_, "-*-helvetica-bold-r-normal-*-18-*-*-*-*-*-*-*");
        textFont_ = XLoadQueryFont(display_, "-*-helvetica-medium-r-normal-*-18-*-*-*-*-*-*-*");
        monoFont_ = XLoadQueryFont(display_, "fixed");

        appendLog("Connecting to BJ_LED...");
        connected_ = !address_.empty() && led_.connect(address_);
        appendLog(connected_ ? "Strip ready, live writing enabled" : "Click Scan or pass a Bluetooth address");
        rebuildLayout();

        auto nextFrame = std::chrono::steady_clock::now();
        bool running = true;
        while (running) {
            while (XPending(display_)) {
                XEvent event;
                XNextEvent(display_, &event);
                if (event.type == Expose) draw();
                if (event.type == ConfigureNotify) {
                    width_ = std::max(940, event.xconfigure.width);
                    height_ = std::max(650, event.xconfigure.height);
                    recreateBackbuffer();
                    rebuildLayout();
                    draw();
                }
                if (event.type == ButtonPress) pointer(event.xbutton.x, event.xbutton.y, true);
                if (event.type == MotionNotify) pointer(event.xmotion.x, event.xmotion.y, false);
                if (event.type == ButtonRelease) activeSlider_ = -1;
                if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == wmDelete_) running = false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextFrame) {
                tick();
                draw();
                nextFrame = now + std::chrono::milliseconds(std::max(8, 1000 / settings_.fps));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        stopping_ = true;
        joinWrite();
        if (backbuffer_) XFreePixmap(display_, backbuffer_);
        if (gc_) XFreeGC(display_, gc_);
        XCloseDisplay(display_);
        return 0;
    }

private:
    void rebuildLayout() {
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

        const char* labels[5] {"FPS", "Brightness", "Saturation", "Smoothing", "Threshold"};
        const float mins[5] {1.0f, 0.15f, 0.50f, 0.01f, 0.0f};
        const float maxs[5] {30.0f, 1.0f, 2.5f, 1.0f, 35.0f};
        const float values[5] {float(settings_.fps), settings_.brightness, settings_.saturation, settings_.smoothing, settings_.threshold};
        const int sliderStartY = 224;
        const int sliderGap = 50;
        for (int i = 0; i < 5; ++i) {
            sliders_[i] = {{fieldX, sliderStartY + i * sliderGap, 480, 26}, labels[i], mins[i], maxs[i], values[i]};
        }

        const int segY = 482;
        int segX = 28;
        const int widths[5] {78, 82, 92, 82, 92};
        for (int i = 0; i < 5; ++i) {
            outputRects_[i] = {segX, segY, widths[i], 32};
            segX += widths[i] + 4;
        }
    }

    void appendLog(const std::string& line) {
        logs_.push_back(line);
        if (logs_.size() > 4) logs_.erase(logs_.begin());
    }

    const char* modeTitle() const {
        switch (settings_.mode) {
            case bj::SampleMode::Average: return "Average";
            case bj::SampleMode::Vibrant: return "Vibrant";
            case bj::SampleMode::Balanced:
            default: return "Balanced";
        }
    }

    void recreateBackbuffer() {
        if (backbuffer_) XFreePixmap(display_, backbuffer_);
        backbuffer_ = XCreatePixmap(display_, window_, unsigned(width_), unsigned(height_), unsigned(DefaultDepth(display_, DefaultScreen(display_))));
    }

    void drawButton(Drawable target, const Rect& rect, const char* label, bool active = false) {
        fill(display_, target, gc_, rect, active ? rgb(display_, 30, 132, 240) : rgb(display_, 72, 75, 77));
        text(display_, target, gc_, textFont_, rect.x + 16, rect.y + 22, label, rgb(display_, 245, 245, 245));
    }

    void draw() {
        if (!backbuffer_) recreateBackbuffer();
        Drawable target = backbuffer_;
        fill(display_, target, gc_, {0, 0, width_, height_}, rgb(display_, 37, 40, 41));
        fill(display_, target, gc_, {30, 28, 38, 38}, rgb(display_, 4, 15, 38));
        text(display_, target, gc_, textFont_, 36, 55, "BJ", rgb(display_, 255, 255, 255));
        text(display_, target, gc_, titleFont_, 80, 60, "BJ LED Ambilight", rgb(display_, 245, 245, 247));

        text(display_, target, gc_, labelFont_, 28, 116, "Device", rgb(display_, 245, 245, 245));
        text(display_, target, gc_, labelFont_, 28, 168, "Display", rgb(display_, 245, 245, 245));
        text(display_, target, gc_, labelFont_, 428, 168, "Mode", rgb(display_, 245, 245, 245));

        drawButton(target, deviceRect_, deviceLabel_.c_str());
        drawButton(target, scanRect_, "Scan");
        drawButton(target, connectRect_, "Connect");
        drawButton(target, displayRect_, "Virtual desktop");
        drawButton(target, modeRect_, modeTitle());
        drawButton(target, max127Rect_, "127", settings_.maxChannel == 127);
        drawButton(target, max255Rect_, "255", settings_.maxChannel == 255);

        for (const Slider& slider : sliders_) {
            text(display_, target, gc_, labelFont_, 28, slider.rect.y + 20, slider.label, rgb(display_, 245, 245, 245));
            fill(display_, target, gc_, {slider.rect.x, slider.rect.y + 10, slider.rect.w, 6}, rgb(display_, 82, 86, 88));
            const int knob = sliderX(slider);
            fill(display_, target, gc_, {slider.rect.x, slider.rect.y + 10, std::max(1, knob - slider.rect.x), 6}, rgb(display_, 30, 132, 240));
            fill(display_, target, gc_, {knob - 11, slider.rect.y + 2, 22, 22}, rgb(display_, 232, 234, 236));
            char value[32];
            std::snprintf(value, sizeof(value), "%.2f", slider.value);
            text(display_, target, gc_, textFont_, 650, slider.rect.y + 20, value, rgb(display_, 245, 245, 245));
        }

        const char* modes[5] {"Auto", "Red", "Green", "Blue", "White"};
        for (int i = 0; i < 5; ++i) drawButton(target, outputRects_[i], modes[i], outputMode_ == i);

        fill(display_, target, gc_, previewRect_, rgb(display_, smoothed_));
        border(display_, target, gc_, previewRect_, rgb(display_, 34, 87, 120));
        const int sw = 28;
        const int gap = 8;
        const std::array<Rect, 4> swatches {{
            {previewRect_.x - sw - gap, previewRect_.y + previewRect_.h - sw, sw, sw},
            {previewRect_.x + previewRect_.w + gap, previewRect_.y + previewRect_.h - sw, sw, sw},
            {previewRect_.x - sw - gap, previewRect_.y, sw, sw},
            {previewRect_.x + previewRect_.w + gap, previewRect_.y, sw, sw},
        }};
        for (int i = 0; i < 4; ++i) {
            fill(display_, target, gc_, swatches[i], rgb(display_, frame_.corners[i]));
            border(display_, target, gc_, swatches[i], rgb(display_, 58, 72, 92));
        }

        char rgbText[80];
        std::snprintf(rgbText, sizeof(rgbText), "RGB %u %u %u", smoothed_.r, smoothed_.g, smoothed_.b);
        text(display_, target, gc_, labelFont_, 726, 402, rgbText, rgb(display_, 245, 245, 245));

        fill(display_, target, gc_, logRect_, rgb(display_, 22, 27, 29));
        setClip(display_, gc_, {logRect_.x + 8, logRect_.y + 8, logRect_.w - 16, logRect_.h - 16});
        int y = logRect_.y + 24;
        for (const std::string& line : logs_) {
            text(display_, target, gc_, monoFont_, logRect_.x + 16, y, line.c_str(), rgb(display_, 222, 226, 229));
            y += 18;
        }
        clearClip(display_, gc_);
        XCopyArea(display_, backbuffer_, window_, gc_, 0, 0, unsigned(width_), unsigned(height_), 0, 0);
        XFlush(display_);
    }

    void pointer(int x, int y, bool pressed) {
        if (pressed) {
            for (int i = 0; i < int(sliders_.size()); ++i) {
                if (x >= sliders_[i].rect.x && x <= sliders_[i].rect.x + sliders_[i].rect.w && y >= sliders_[i].rect.y - 12 && y <= sliders_[i].rect.y + 36) {
                    activeSlider_ = i;
                    updateSlider(x);
                    return;
                }
            }
            for (int i = 0; i < int(outputRects_.size()); ++i) {
                if (contains(outputRects_[i], x, y)) {
                    outputMode_ = i;
                    hasSmoothed_ = false;
                    draw();
                    return;
                }
            }
            if (contains(max127Rect_, x, y)) settings_.maxChannel = 127;
            if (contains(max255Rect_, x, y)) settings_.maxChannel = 255;
            if (contains(deviceRect_, x, y)) appendLog(address_.empty() ? "No scanned device yet" : "BJ_LED selected");
            if (contains(displayRect_, x, y)) appendLog("Capturing the X11 virtual desktop");
            if (contains(modeRect_, x, y)) cycleMode();
            if (contains(scanRect_, x, y)) scanDevices();
            if (contains(connectRect_, x, y)) {
                appendLog("Connecting to BJ_LED...");
                connectDevice();
            }
        } else if (activeSlider_ >= 0) {
            updateSlider(x);
        }
        draw();
    }

    void updateSlider(int x) {
        Slider& slider = sliders_[activeSlider_];
        const float t = std::clamp(float(x - slider.rect.x) / std::max(1, slider.rect.w), 0.0f, 1.0f);
        slider.value = slider.min + (slider.max - slider.min) * t;
        if (activeSlider_ == 0) {
            settings_.fps = std::clamp(int(slider.value + 0.5f), 1, 30);
            slider.value = float(settings_.fps);
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
    }

    void cycleMode() {
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

    void tick() {
        if (stopping_) return;
        auto pixels = capture_.capture();
        frame_ = analyzer_.analyzeFrame(pixels, settings_.sampleWidth, settings_.sampleHeight, settings_);
        const bj::RGB target = selectedOutputColor(outputMode_, frame_.output);
        smoothed_ = outputMode_ == 0 && hasSmoothed_ ? bj::smooth(smoothed_, target, settings_.smoothing) : target;
        hasSmoothed_ = true;
        const auto now = std::chrono::steady_clock::now();
        const bool forceRefresh = now - lastWriteTime_ >= std::chrono::milliseconds(750);
        const bool colorChanged = bj::distance(lastSent_, smoothed_) >= settings_.threshold;
        const bool writeWindowOpen = now - lastWriteTime_ >= std::chrono::milliseconds(180);
        if (led_.isReady() && writeWindowOpen && (colorChanged || forceRefresh) && !writeInFlight_.exchange(true)) {
            if (writeThread_.joinable()) writeThread_.join();
            const bj::RGB color = smoothed_;
            const int maxChannel = settings_.maxChannel;
            lastSent_ = color;
            lastWriteTime_ = now;
            writeThread_ = std::thread([this, color, maxChannel] {
                try {
                    if (!stopping_) led_.write(color, maxChannel);
                } catch (...) {
                }
                writeInFlight_ = false;
            });
        }
    }

    void joinWrite() {
        if (writeThread_.joinable()) writeThread_.join();
    }

    void scanDevices() {
        appendLog("BLE scan started");
        auto devices = BlueZLed::scan();
        if (devices.empty()) {
            appendLog("BJ_LED not found over BlueZ LE scan");
            return;
        }
        selectedDevice_ = devices.front();
        address_ = selectedDevice_.address;
        deviceLabel_ = selectedDevice_.name + "  RSSI " + std::to_string(selectedDevice_.rssi);
        appendLog("Found " + selectedDevice_.name + " " + selectedDevice_.address);
    }

    void connectDevice() {
        if (address_.empty()) {
            appendLog("No device address. Click Scan first.");
            return;
        }
        appendLog("Connecting by BlueZ device path...");
        connected_ = selectedDevice_.objectPath.empty() ? led_.connect(address_) : led_.connect(selectedDevice_);
        appendLog(connected_ ? "Strip ready, live writing enabled" : "Could not connect to BJ_LED");
    }

    std::string address_;
    BlueZDeviceInfo selectedDevice_;
    bj::Settings settings_;
    LinuxScreenCapture capture_;
    BlueZLed led_;
    bj::ColorAnalyzer analyzer_;
    bj::FrameAnalysis frame_;
    bj::RGB smoothed_;
    bj::RGB lastSent_;
    std::chrono::steady_clock::time_point lastWriteTime_ {};
    bool hasSmoothed_ = false;
    bool connected_ = false;
    std::atomic_bool writeInFlight_ {false};
    std::atomic_bool stopping_ {false};
    std::thread writeThread_;
    int outputMode_ = 0;
    int modeIndex_ = 0;
    int activeSlider_ = -1;
    int width_ = 940;
    int height_ = 650;
    Rect deviceRect_;
    Rect scanRect_;
    Rect connectRect_;
    Rect displayRect_;
    Rect modeRect_;
    Rect max127Rect_;
    Rect max255Rect_;
    Rect previewRect_;
    Rect logRect_;
    std::array<Rect, 5> outputRects_ {};
    std::array<Slider, 5> sliders_ {};
    std::vector<std::string> logs_;
    std::string deviceLabel_ = "No device yet";
    Display* display_ = nullptr;
    Window window_ = 0;
    Pixmap backbuffer_ = 0;
    Atom wmDelete_ = 0;
    GC gc_ {};
    XFontStruct* titleFont_ = nullptr;
    XFontStruct* labelFont_ = nullptr;
    XFontStruct* textFont_ = nullptr;
    XFontStruct* monoFont_ = nullptr;
};
#endif
}

int LinuxApp::run(const std::string& address, const std::string& outputMode) {
#if BJ_LED_HAS_X11_UI
    X11AmbilightWindow window(address, outputMode);
    return window.run();
#else
    (void)address;
    (void)outputMode;
    std::cerr << "BJ LED Ambilight Linux UI requires X11/XWayland headers and runtime.\n";
    return 1;
#endif
}
