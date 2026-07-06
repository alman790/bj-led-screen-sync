#include "lib/platform/linux/app/LinuxApp.hpp"

#include "lib/bj_core.hpp"
#include "lib/platform/linux/capture/LinuxScreenCapture.hpp"
#include "lib/platform/linux/led/BlueZLed.hpp"

#include <chrono>
#include <iostream>
#include <thread>

static bj::RGB selectedOutputColor(const std::string& mode, bj::RGB autoColor) {
    if (mode == "red") return bj::RGB(255, 0, 0);
    if (mode == "green") return bj::RGB(0, 255, 0);
    if (mode == "blue") return bj::RGB(0, 0, 255);
    if (mode == "white") return bj::RGB(255, 255, 255);
    return autoColor;
}

int LinuxApp::run(const std::string& address, const std::string& outputMode) {
    bj::Settings settings;
    LinuxScreenCapture capture(settings);
    BlueZLed led;
    bj::ColorAnalyzer analyzer;
    bj::RGB smoothed;
    bool hasSmoothed = false;
    bj::RGB lastSent;
    auto lastWrite = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    if (!capture.isAvailable()) {
        std::cerr << "Screen capture backend is unavailable. On Linux install X11 headers/libs or run under XWayland.\n";
    }

    if (!address.empty() && !led.connect(address)) {
        std::cerr << "Could not connect to " << address << '\n';
        return 1;
    }

    std::cout << "BJ LED Ambilight Linux\n";
    std::cout << "Qt is not used. Shared RGB union size: " << sizeof(bj::RGB) << " bytes\n";
    std::cout << "Output mode: " << outputMode << '\n';

    for (;;) {
        auto pixels = capture.capture();
        bj::FrameAnalysis frame = analyzer.analyzeFrame(pixels, settings.sampleWidth, settings.sampleHeight, settings);
        bj::RGB color = selectedOutputColor(outputMode, frame.output);
        smoothed = outputMode == "auto" && hasSmoothed ? bj::smooth(smoothed, color, settings.smoothing) : color;
        hasSmoothed = true;
        std::cout << "RGB " << int(smoothed.r) << ' ' << int(smoothed.g) << ' ' << int(smoothed.b) << '\n';
        const auto now = std::chrono::steady_clock::now();
        if (led.isReady()
            && (bj::distance(lastSent, smoothed) >= settings.threshold || now - lastWrite >= std::chrono::milliseconds(250))) {
            led.write(smoothed, settings.maxChannel);
            lastSent = smoothed;
            lastWrite = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / settings.fps));
    }
}
