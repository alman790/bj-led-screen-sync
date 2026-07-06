#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace bj {

union RGB {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#endif
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    uint32_t rgba;

    constexpr RGB() : rgba(0xff000000u) {}
    constexpr RGB(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}
};

static_assert(sizeof(RGB) == 4, "RGB must stay exactly one packed 32-bit pixel");
static_assert(alignof(RGB) == alignof(uint32_t), "RGB must stay uint32_t-aligned");

struct HSV {
    float h = 0;
    float s = 0;
    float v = 0;
};

enum class SampleMode {
    Average,
    Vibrant,
    Balanced,
};

struct Settings {
    int fps = 10;
    int sampleWidth = 160;
    int sampleHeight = 90;
    int cropPercent = 8;
    int maxChannel = 255;
    float brightness = 1.0f;
    float saturation = 1.35f;
    float smoothing = 0.55f;
    float threshold = 2.0f;
    SampleMode mode = SampleMode::Balanced;
};

struct FrameAnalysis {
    RGB output;
    std::array<RGB, 4> corners;
    std::array<RGB, 4> edges;
    int dominantZone = 0;
};

inline HSV rgbToHsv(RGB c) {
    const float r = c.r / 255.0f;
    const float g = c.g / 255.0f;
    const float b = c.b / 255.0f;
    const float maxv = std::max({r, g, b});
    const float minv = std::min({r, g, b});
    const float delta = maxv - minv;

    HSV out;
    out.v = maxv;
    out.s = maxv <= 0.0001f ? 0.0f : delta / maxv;
    if (delta <= 0.0001f) {
        out.h = 0;
    } else if (maxv == r) {
        out.h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
    } else if (maxv == g) {
        out.h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        out.h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (out.h < 0) out.h += 360.0f;
    return out;
}

inline RGB hsvToRgb(HSV hsv) {
    hsv.s = std::clamp(hsv.s, 0.0f, 1.0f);
    hsv.v = std::clamp(hsv.v, 0.0f, 1.0f);
    const float c = hsv.v * hsv.s;
    const float x = c * (1.0f - std::fabs(std::fmod(hsv.h / 60.0f, 2.0f) - 1.0f));
    const float m = hsv.v - c;
    float r = 0, g = 0, b = 0;

    if (hsv.h < 60) {
        r = c; g = x;
    } else if (hsv.h < 120) {
        r = x; g = c;
    } else if (hsv.h < 180) {
        g = c; b = x;
    } else if (hsv.h < 240) {
        g = x; b = c;
    } else if (hsv.h < 300) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }

    return RGB(
        static_cast<uint8_t>(std::clamp((r + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((g + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((b + m) * 255.0f, 0.0f, 255.0f)));
}

inline float distance(RGB a, RGB b) {
    const int dr = int(a.r) - int(b.r);
    const int dg = int(a.g) - int(b.g);
    const int db = int(a.b) - int(b.b);
    return std::sqrt(float(dr * dr + dg * dg + db * db));
}

inline RGB smooth(RGB previous, RGB current, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return RGB(
        uint8_t(previous.r * (1.0f - alpha) + current.r * alpha),
        uint8_t(previous.g * (1.0f - alpha) + current.g * alpha),
        uint8_t(previous.b * (1.0f - alpha) + current.b * alpha));
}

inline RGB enhance(RGB color, const Settings& settings) {
    HSV hsv = rgbToHsv(color);
    hsv.s = std::clamp(hsv.s * settings.saturation, 0.0f, 1.0f);
    hsv.v = std::clamp(hsv.v * settings.brightness, 0.02f, 1.0f);
    return hsvToRgb(hsv);
}

class ColorAnalyzer {
public:
    RGB analyze(std::span<const RGB> pixels, const Settings& settings) const {
        if (pixels.empty()) return RGB(0, 0, 0);
        RGB color;
        switch (settings.mode) {
            case SampleMode::Average:
                color = average(pixels);
                break;
            case SampleMode::Vibrant:
                color = vibrant(pixels);
                break;
            case SampleMode::Balanced:
                color = balanced(pixels);
                break;
        }
        return enhance(color, settings);
    }

    FrameAnalysis analyzeFrame(std::span<const RGB> pixels, int width, int height, const Settings& settings) const {
        FrameAnalysis frame;
        if (pixels.empty() || width <= 0 || height <= 0 || pixels.size() < size_t(width * height)) {
            frame.output = RGB(0, 0, 0);
            frame.corners.fill(frame.output);
            frame.edges.fill(frame.output);
            return frame;
        }

        const int bandX = std::max(4, width / 5);
        const int bandY = std::max(4, height / 5);
        const int cornerW = std::max(6, width / 3);
        const int cornerH = std::max(6, height / 3);

        frame.corners = {
            analyzeRect(pixels, width, height, 0, 0, cornerW, cornerH, settings),
            analyzeRect(pixels, width, height, width - cornerW, 0, cornerW, cornerH, settings),
            analyzeRect(pixels, width, height, 0, height - cornerH, cornerW, cornerH, settings),
            analyzeRect(pixels, width, height, width - cornerW, height - cornerH, cornerW, cornerH, settings),
        };

        frame.edges = {
            analyzeRect(pixels, width, height, 0, 0, width, bandY, settings),
            analyzeRect(pixels, width, height, width - bandX, 0, bandX, height, settings),
            analyzeRect(pixels, width, height, 0, height - bandY, width, bandY, settings),
            analyzeRect(pixels, width, height, 0, 0, bandX, height, settings),
        };

        std::array<RGB, 8> zones {
            frame.corners[0], frame.corners[1], frame.corners[2], frame.corners[3],
            frame.edges[0], frame.edges[1], frame.edges[2], frame.edges[3],
        };

        float bestScore = -1.0f;
        RGB best = zones[0];
        for (size_t index = 0; index < zones.size(); ++index) {
            const float score = zoneScore(zones[index]);
            if (score > bestScore) {
                bestScore = score;
                best = zones[index];
                frame.dominantZone = int(index);
            }
        }

        RGB center = analyzeRect(
            pixels,
            width,
            height,
            width / 4,
            height / 4,
            std::max(1, width / 2),
            std::max(1, height / 2),
            settings);

        HSV bestHsv = rgbToHsv(best);
        if (bestHsv.v < 0.04f || bestHsv.s < 0.05f) {
            frame.output = center;
        } else {
            frame.output = blendRgb(best, center, 0.18f);
        }
        return frame;
    }

private:
    struct Bucket {
        uint32_t count = 0;
        uint32_t r = 0;
        uint32_t g = 0;
        uint32_t b = 0;
    };

    static RGB blendRgb(RGB a, RGB b, float bWeight) {
        bWeight = std::clamp(bWeight, 0.0f, 1.0f);
        const float aWeight = 1.0f - bWeight;
        return RGB(
            uint8_t(std::clamp(a.r * aWeight + b.r * bWeight, 0.0f, 255.0f)),
            uint8_t(std::clamp(a.g * aWeight + b.g * bWeight, 0.0f, 255.0f)),
            uint8_t(std::clamp(a.b * aWeight + b.b * bWeight, 0.0f, 255.0f)));
    }

    static float zoneScore(RGB color) {
        HSV hsv = rgbToHsv(color);
        const uint8_t maxv = std::max({color.r, color.g, color.b});
        const uint8_t minv = std::min({color.r, color.g, color.b});
        const float chroma = float(maxv - minv) / 255.0f;
        return hsv.v * (0.35f + hsv.s * 1.25f + chroma * 1.4f);
    }

    static RGB analyzeRect(std::span<const RGB> pixels, int width, int height, int x, int y, int rectW, int rectH, const Settings& settings) {
        x = std::clamp(x, 0, std::max(0, width - 1));
        y = std::clamp(y, 0, std::max(0, height - 1));
        rectW = std::clamp(rectW, 1, width - x);
        rectH = std::clamp(rectH, 1, height - y);

        uint64_t r = 0, g = 0, b = 0, weightSum = 0;
        for (int yy = y; yy < y + rectH; ++yy) {
            const int row = yy * width;
            for (int xx = x; xx < x + rectW; ++xx) {
                RGB p = pixels[row + xx];
                const uint8_t maxv = std::max({p.r, p.g, p.b});
                const uint8_t minv = std::min({p.r, p.g, p.b});
                const uint32_t chroma = maxv - minv;
                if (maxv < 8) continue;
                const uint32_t weight = 1 + chroma / 10 + maxv / 42;
                r += p.r * weight;
                g += p.g * weight;
                b += p.b * weight;
                weightSum += weight;
            }
        }

        if (weightSum == 0) return RGB(0, 0, 0);
        return enhance(RGB(r / weightSum, g / weightSum, b / weightSum), settings);
    }

    static RGB average(std::span<const RGB> pixels) {
        uint64_t r = 0, g = 0, b = 0, weightSum = 0;
        for (RGB p : pixels) {
            const uint8_t maxv = std::max({p.r, p.g, p.b});
            const uint8_t minv = std::min({p.r, p.g, p.b});
            const uint32_t chroma = maxv - minv;
            const uint32_t weight = 1 + chroma / 18;
            r += p.r * weight;
            g += p.g * weight;
            b += p.b * weight;
            weightSum += weight;
        }
        return RGB(r / weightSum, g / weightSum, b / weightSum);
    }

    static RGB vibrant(std::span<const RGB> pixels) {
        std::array<Bucket, 8 * 8 * 8> buckets {};

        for (RGB p : pixels) {
            const uint8_t maxv = std::max({p.r, p.g, p.b});
            const uint8_t minv = std::min({p.r, p.g, p.b});
            const uint8_t chroma = maxv - minv;
            if (maxv < 28 || chroma < 20) continue;

            const int ri = p.r >> 5;
            const int gi = p.g >> 5;
            const int bi = p.b >> 5;
            Bucket& bucket = buckets[(ri * 8 + gi) * 8 + bi];
            bucket.count++;
            bucket.r += p.r;
            bucket.g += p.g;
            bucket.b += p.b;
        }

        float bestScore = -1.0f;
        RGB best = average(pixels);
        for (const Bucket& bucket : buckets) {
            if (!bucket.count) continue;
            RGB avg(bucket.r / bucket.count, bucket.g / bucket.count, bucket.b / bucket.count);
            const uint8_t maxv = std::max({avg.r, avg.g, avg.b});
            const uint8_t minv = std::min({avg.r, avg.g, avg.b});
            const float chroma = float(maxv - minv) / 255.0f;
            const float brightness = float(maxv) / 255.0f;
            const float score = std::log1p(float(bucket.count)) * (1.0f + chroma * 3.2f) * (0.6f + brightness);
            if (score > bestScore) {
                bestScore = score;
                best = avg;
            }
        }
        return best;
    }

    static RGB balanced(std::span<const RGB> pixels) {
        RGB avg = average(pixels);
        RGB vib = vibrant(pixels);
        HSV avgh = rgbToHsv(avg);
        HSV vibh = rgbToHsv(vib);
        const float pi = 3.14159265358979323846f;
        const float mix = std::clamp(0.35f + vibh.s * 0.45f, 0.35f, 0.8f);
        const float x1 = std::cos(avgh.h * pi / 180.0f) * avgh.s * (1.0f - mix)
            + std::cos(vibh.h * pi / 180.0f) * vibh.s * mix;
        const float y1 = std::sin(avgh.h * pi / 180.0f) * avgh.s * (1.0f - mix)
            + std::sin(vibh.h * pi / 180.0f) * vibh.s * mix;
        HSV out;
        out.h = std::atan2(y1, x1) * 180.0f / pi;
        if (out.h < 0) out.h += 360.0f;
        out.s = std::clamp(avgh.s * 0.4f + vibh.s * 0.6f, 0.0f, 1.0f);
        out.v = std::clamp(avgh.v * 0.55f + vibh.v * 0.45f, 0.0f, 1.0f);
        return hsvToRgb(out);
    }
};

inline std::array<uint8_t, 8> colorPacket(RGB color, int maxChannel) {
    maxChannel = std::clamp(maxChannel, 1, 255);
    const auto scale = [maxChannel](uint8_t value) -> uint8_t {
        return uint8_t(std::round(value * (maxChannel / 255.0f)));
    };
    return {0x69, 0x96, 0x05, 0x02, scale(color.r), scale(color.g), scale(color.b), uint8_t(maxChannel)};
}

}  // namespace bj
