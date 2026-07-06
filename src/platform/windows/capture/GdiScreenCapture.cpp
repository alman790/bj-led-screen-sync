#include "lib/platform/windows/capture/GdiScreenCapture.hpp"

#include <algorithm>
#include <cstring>

GdiScreenCapture::GdiScreenCapture(bj::Settings settings) : settings_(settings) {
    recreateBuffer();
}

GdiScreenCapture::~GdiScreenCapture() {
    if (memory_ && oldBitmap_) SelectObject(memory_, oldBitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (memory_) DeleteDC(memory_);
    if (screen_) ReleaseDC(nullptr, screen_);
}

void GdiScreenCapture::updateSettings(bj::Settings settings) {
    const bool sizeChanged = settings_.sampleWidth != settings.sampleWidth
        || settings_.sampleHeight != settings.sampleHeight;
    settings_ = settings;
    if (sizeChanged) recreateBuffer();
}

void GdiScreenCapture::recreateBuffer() {
    if (memory_ && oldBitmap_) SelectObject(memory_, oldBitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (!screen_) screen_ = GetDC(nullptr);
    if (!memory_) memory_ = CreateCompatibleDC(screen_);

    pixels_.resize(size_t(settings_.sampleWidth * settings_.sampleHeight));
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = settings_.sampleWidth;
    info.bmiHeader.biHeight = -settings_.sampleHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    dibPixels_ = nullptr;
    bitmap_ = CreateDIBSection(screen_, &info, DIB_RGB_COLORS, &dibPixels_, nullptr, 0);
    oldBitmap_ = SelectObject(memory_, bitmap_);
    SetStretchBltMode(memory_, HALFTONE);
}

std::span<const bj::RGB> GdiScreenCapture::capture() {
    if (!screen_ || !memory_ || !dibPixels_) return pixels_;

    const int srcX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int srcY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int srcW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int srcH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    StretchBlt(
        memory_,
        0,
        0,
        settings_.sampleWidth,
        settings_.sampleHeight,
        screen_,
        srcX,
        srcY,
        srcW,
        srcH,
        SRCCOPY);

    std::memcpy(pixels_.data(), dibPixels_, pixels_.size() * sizeof(bj::RGB));

    for (bj::RGB& pixel : pixels_) {
        std::swap(pixel.r, pixel.b);
        pixel.a = 255;
    }
    return pixels_;
}
