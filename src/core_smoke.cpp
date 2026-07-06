#include "lib/bj_core.hpp"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    static_assert(sizeof(bj::RGB) == 4);
    static_assert(alignof(bj::RGB) == alignof(uint32_t));

    bj::RGB red(255, 0, 0);
    auto packet = bj::colorPacket(red, 127);
    assert(packet[0] == 0x69);
    assert(packet[1] == 0x96);
    assert(packet[2] == 0x05);
    assert(packet[3] == 0x02);
    assert(packet[4] == 0x7f);
    assert(packet[5] == 0x00);
    assert(packet[6] == 0x00);
    assert(packet[7] == 0x7f);

    std::vector<bj::RGB> pixels;
    pixels.reserve(160 * 90);
    for (int i = 0; i < 160 * 90; ++i) {
        pixels.emplace_back(uint8_t(i % 255), uint8_t((i * 3) % 255), uint8_t((i * 7) % 255));
    }

    bj::Settings settings;
    bj::ColorAnalyzer analyzer;
    bj::RGB color = analyzer.analyze(pixels, settings);
    assert(color.a == 255);

    std::vector<bj::RGB> zoned(160 * 90, bj::RGB(4, 4, 4));
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 50; ++x) {
            zoned[y * 160 + x] = bj::RGB(240, 20, 10);
        }
    }
    bj::FrameAnalysis frame = analyzer.analyzeFrame(zoned, 160, 90, settings);
    assert(frame.output.r > frame.output.g);
    assert(frame.output.r > frame.output.b);
    assert(frame.dominantZone == 0 || frame.dominantZone == 7);

    std::cout << "core ok: sizeof(RGB)=" << sizeof(bj::RGB)
              << " packet=";
    for (uint8_t byte : packet) {
        std::cout << std::hex << int(byte) << ' ';
    }
    std::cout << std::dec << "color="
              << int(color.r) << ','
              << int(color.g) << ','
              << int(color.b) << '\n';
    return 0;
}
