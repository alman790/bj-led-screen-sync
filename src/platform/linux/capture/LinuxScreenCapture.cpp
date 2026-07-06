#include "lib/platform/linux/capture/LinuxScreenCapture.hpp"

#if defined(__linux__) && __has_include(<X11/Xlib.h>) && __has_include(<X11/Xutil.h>)
#define BJ_LED_HAS_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#else
#define BJ_LED_HAS_X11 0
#endif

#include <algorithm>

#if BJ_LED_HAS_X11
static uint8_t extractChannel(unsigned long pixel, unsigned long mask) {
    if (!mask) return 0;
    int shift = 0;
    while (((mask >> shift) & 1ul) == 0) ++shift;
    unsigned long value = (pixel & mask) >> shift;
    unsigned long maxValue = mask >> shift;
    return uint8_t((value * 255ul) / std::max(1ul, maxValue));
}
#endif

LinuxScreenCapture::LinuxScreenCapture(bj::Settings settings) : settings_(settings) {
    pixels_.resize(size_t(settings_.sampleWidth * settings_.sampleHeight));
    openDisplay();
}

LinuxScreenCapture::~LinuxScreenCapture() {
#if BJ_LED_HAS_X11
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
#endif
}

void LinuxScreenCapture::updateSettings(bj::Settings settings) {
    settings_ = settings;
    pixels_.resize(size_t(settings_.sampleWidth * settings_.sampleHeight));
}

bool LinuxScreenCapture::isAvailable() const {
    return available_;
}

void LinuxScreenCapture::openDisplay() {
#if BJ_LED_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    display_ = display;
    const int screen = DefaultScreen(display);
    root_ = RootWindow(display, screen);
    sourceWidth_ = DisplayWidth(display, screen);
    sourceHeight_ = DisplayHeight(display, screen);
    available_ = sourceWidth_ > 0 && sourceHeight_ > 0;
#endif
}

std::span<const bj::RGB> LinuxScreenCapture::capture() {
    if (!available_) {
        std::fill(pixels_.begin(), pixels_.end(), bj::RGB(0, 0, 0));
        return pixels_;
    }

#if BJ_LED_HAS_X11
    Display* display = static_cast<Display*>(display_);
    XImage* image = XGetImage(display, root_, sourceX_, sourceY_, sourceWidth_, sourceHeight_, AllPlanes, ZPixmap);
    if (!image) {
        std::fill(pixels_.begin(), pixels_.end(), bj::RGB(0, 0, 0));
        return pixels_;
    }

    for (int y = 0; y < settings_.sampleHeight; ++y) {
        const int srcY = std::clamp((y * sourceHeight_) / settings_.sampleHeight, 0, sourceHeight_ - 1);
        for (int x = 0; x < settings_.sampleWidth; ++x) {
            const int srcX = std::clamp((x * sourceWidth_) / settings_.sampleWidth, 0, sourceWidth_ - 1);
            const unsigned long pixel = XGetPixel(image, srcX, srcY);
            pixels_[size_t(y * settings_.sampleWidth + x)] = bj::RGB(
                extractChannel(pixel, image->red_mask),
                extractChannel(pixel, image->green_mask),
                extractChannel(pixel, image->blue_mask));
        }
    }
    XDestroyImage(image);
#endif

    return pixels_;
}
