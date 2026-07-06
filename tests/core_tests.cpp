#include "lib/bj_core.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
}

void assertRgb(bj::RGB color, uint8_t r, uint8_t g, uint8_t b, uint8_t tolerance = 1) {
    if (std::abs(int(color.r) - int(r)) > tolerance
        || std::abs(int(color.g) - int(g)) > tolerance
        || std::abs(int(color.b) - int(b)) > tolerance
        || color.a != 255) {
        std::cerr << "RGB assertion failed: got "
                  << int(color.r) << ',' << int(color.g) << ',' << int(color.b) << ',' << int(color.a)
                  << " expected "
                  << int(r) << ',' << int(g) << ',' << int(b) << ",255 tolerance "
                  << int(tolerance) << '\n';
        std::abort();
    }
}

std::vector<bj::RGB> solid(size_t count, bj::RGB color) {
    return std::vector<bj::RGB>(count, color);
}

void testRgbStorage() {
    static_assert(sizeof(bj::RGB) == 4);
    static_assert(alignof(bj::RGB) == alignof(uint32_t));
    bj::RGB color(1, 2, 3, 4);
    assert(color.r == 1);
    assert(color.g == 2);
    assert(color.b == 3);
    assert(color.a == 4);
    assert(bj::RGB().a == 255);
}

void testHsvConversion() {
    bj::HSV black = bj::rgbToHsv(bj::RGB(0, 0, 0));
    assert(near(black.h, 0));
    assert(near(black.s, 0));
    assert(near(black.v, 0));

    bj::HSV gray = bj::rgbToHsv(bj::RGB(80, 80, 80));
    assert(near(gray.h, 0));
    assert(near(gray.s, 0));
    assert(near(gray.v, 80.0f / 255.0f));

    bj::HSV red = bj::rgbToHsv(bj::RGB(255, 0, 0));
    bj::HSV green = bj::rgbToHsv(bj::RGB(0, 255, 0));
    bj::HSV blue = bj::rgbToHsv(bj::RGB(0, 0, 255));
    bj::HSV magenta = bj::rgbToHsv(bj::RGB(255, 0, 128));
    assert(near(red.h, 0));
    assert(near(green.h, 120));
    assert(near(blue.h, 240));
    assert(magenta.h > 300);

    assertRgb(bj::hsvToRgb({30, 1, 1}), 255, 127, 0, 2);
    assertRgb(bj::hsvToRgb({90, 1, 1}), 127, 255, 0, 2);
    assertRgb(bj::hsvToRgb({150, 1, 1}), 0, 255, 127, 2);
    assertRgb(bj::hsvToRgb({210, 1, 1}), 0, 127, 255, 2);
    assertRgb(bj::hsvToRgb({270, 1, 1}), 127, 0, 255, 2);
    assertRgb(bj::hsvToRgb({330, 1, 1}), 255, 0, 127, 2);
    assertRgb(bj::hsvToRgb({60, 3, 2}), 255, 255, 0);
    assertRgb(bj::hsvToRgb({0, -1, -1}), 0, 0, 0);
}

void testMathHelpers() {
    assert(near(bj::distance(bj::RGB(0, 0, 0), bj::RGB(3, 4, 12)), 13));
    assertRgb(bj::smooth(bj::RGB(0, 100, 200), bj::RGB(100, 0, 0), 0.5f), 50, 50, 100);
    assertRgb(bj::smooth(bj::RGB(10, 20, 30), bj::RGB(200, 210, 220), -1.0f), 10, 20, 30);
    assertRgb(bj::smooth(bj::RGB(10, 20, 30), bj::RGB(200, 210, 220), 2.0f), 200, 210, 220);

    bj::Settings bright;
    bright.brightness = 0.5f;
    bright.saturation = 1.0f;
    bj::RGB dimmed = bj::enhance(bj::RGB(100, 50, 50), bright);
    assert(dimmed.r < 100);

    bj::Settings saturated;
    saturated.brightness = 1.0f;
    saturated.saturation = 2.5f;
    bj::RGB vivid = bj::enhance(bj::RGB(120, 90, 90), saturated);
    assert(vivid.r >= 120);
}

void testPackets() {
    auto full = bj::colorPacket(bj::RGB(255, 128, 0), 255);
    assert((full == std::array<uint8_t, 8>{0x69, 0x96, 0x05, 0x02, 255, 128, 0, 255}));

    auto half = bj::colorPacket(bj::RGB(255, 128, 10), 127);
    assert(half[4] == 127);
    assert(half[5] == 64);
    assert(half[6] == 5);
    assert(half[7] == 127);

    auto lowClamp = bj::colorPacket(bj::RGB(255, 255, 255), -10);
    auto highClamp = bj::colorPacket(bj::RGB(255, 255, 255), 999);
    assert(lowClamp[4] == 1 && lowClamp[7] == 1);
    assert(highClamp[4] == 255 && highClamp[7] == 255);
}

void testAnalyzerModes() {
    bj::ColorAnalyzer analyzer;
    bj::Settings settings;

    assertRgb(analyzer.analyze({}, settings), 0, 0, 0);

    std::vector<bj::RGB> mixed {
        bj::RGB(250, 0, 0), bj::RGB(250, 0, 0), bj::RGB(4, 4, 4), bj::RGB(0, 40, 210),
        bj::RGB(0, 40, 210), bj::RGB(120, 120, 120), bj::RGB(0, 0, 0), bj::RGB(10, 200, 10),
    };

    settings.mode = bj::SampleMode::Average;
    bj::RGB avg = analyzer.analyze(mixed, settings);
    assert(avg.r > 0 || avg.g > 0 || avg.b > 0);

    settings.mode = bj::SampleMode::Vibrant;
    bj::RGB vibrant = analyzer.analyze(mixed, settings);
    assert(std::max({vibrant.r, vibrant.g, vibrant.b}) > 120);

    settings.mode = bj::SampleMode::Balanced;
    bj::RGB balanced = analyzer.analyze(mixed, settings);
    assert(std::max({balanced.r, balanced.g, balanced.b}) > 90);

    std::vector<bj::RGB> dark = solid(16, bj::RGB(2, 2, 2));
    settings.mode = bj::SampleMode::Vibrant;
    assertRgb(analyzer.analyze(dark, settings), 5, 5, 5, 1);
}

void testFrameAnalysisValidation() {
    bj::ColorAnalyzer analyzer;
    bj::Settings settings;
    std::vector<bj::RGB> tiny = solid(4, bj::RGB(255, 0, 0));

    bj::FrameAnalysis empty = analyzer.analyzeFrame({}, 0, 0, settings);
    assertRgb(empty.output, 0, 0, 0);
    assert(empty.dominantZone == 0);
    for (bj::RGB corner : empty.corners) assertRgb(corner, 0, 0, 0);
    for (bj::RGB edge : empty.edges) assertRgb(edge, 0, 0, 0);

    bj::FrameAnalysis invalid = analyzer.analyzeFrame(tiny, 20, 20, settings);
    assertRgb(invalid.output, 0, 0, 0);
}

void testFrameAnalysisZones() {
    constexpr int width = 12;
    constexpr int height = 12;
    bj::Settings settings;
    settings.mode = bj::SampleMode::Balanced;
    settings.brightness = 1.0f;
    settings.saturation = 1.0f;
    bj::ColorAnalyzer analyzer;

    std::vector<bj::RGB> pixels = solid(width * height, bj::RGB(10, 10, 10));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) pixels[y * width + x] = bj::RGB(240, 20, 10);
    }
    bj::FrameAnalysis redCorner = analyzer.analyzeFrame(pixels, width, height, settings);
    assert(redCorner.output.r > redCorner.output.g);
    assert(redCorner.output.r > redCorner.output.b);
    assert(redCorner.dominantZone == 0 || redCorner.dominantZone == 7);

    pixels = solid(width * height, bj::RGB(80, 80, 80));
    for (int y = 3; y < 9; ++y) {
        for (int x = 3; x < 9; ++x) pixels[y * width + x] = bj::RGB(0, 220, 40);
    }
    bj::FrameAnalysis mutedEdges = analyzer.analyzeFrame(pixels, width, height, settings);
    assert(mutedEdges.output.g >= mutedEdges.output.r);

    pixels = solid(width * height, bj::RGB(1, 1, 1));
    for (int y = 4; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) pixels[y * width + x] = bj::RGB(20, 20, 200);
    }
    bj::FrameAnalysis darkZones = analyzer.analyzeFrame(pixels, width, height, settings);
    assert(darkZones.output.b >= darkZones.output.r);
}
}  // namespace

int main() {
    testRgbStorage();
    testHsvConversion();
    testMathHelpers();
    testPackets();
    testAnalyzerModes();
    testFrameAnalysisValidation();
    testFrameAnalysisZones();
    std::cout << "core tests ok\n";
    return 0;
}
