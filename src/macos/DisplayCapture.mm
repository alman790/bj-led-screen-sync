#import "lib/macos/DisplayCapture.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <mutex>

@implementation DisplayInfo
+ (instancetype)displayWithID:(CGDirectDisplayID)displayID bounds:(CGRect)bounds title:(NSString*)title {
    DisplayInfo* info = [[DisplayInfo alloc] init];
    info.displayID = displayID;
    info.bounds = bounds;
    info.title = title;
    return info;
}
@end

@implementation DisplayCapture {
    __weak id<DisplayCaptureDelegate> delegate_;
    DisplayInfo* displayInfo_;
    bj::Settings settings_;
    std::vector<bj::RGB> latestPixels_;
    std::mutex mutex_;
    NSTimeInterval lastCaptureTime_;
    BOOL hasFrame_;
    BOOL captureErrorLogged_;
}

- (instancetype)initWithDelegate:(id<DisplayCaptureDelegate>)delegate {
    self = [super init];
    if (!self) return nil;
    delegate_ = delegate;
    lastCaptureTime_ = 0;
    hasFrame_ = NO;
    captureErrorLogged_ = NO;
    return self;
}

+ (NSArray<DisplayInfo*>*)activeDisplays {
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    if (count == 0) return @[];

    std::vector<CGDirectDisplayID> ids(count);
    CGGetActiveDisplayList(count, ids.data(), &count);

    NSMutableArray<DisplayInfo*>* displays = [NSMutableArray arrayWithCapacity:count];
    for (uint32_t index = 0; index < count; ++index) {
        CGRect bounds = CGDisplayBounds(ids[index]);
        NSString* title = [NSString stringWithFormat:@"Display %u (%.0fx%.0f @ %.0f,%.0f)",
            index + 1,
            bounds.size.width,
            bounds.size.height,
            bounds.origin.x,
            bounds.origin.y];
        [displays addObject:[DisplayInfo displayWithID:ids[index] bounds:bounds title:title]];
    }
    return displays;
}

- (void)startWithDisplay:(DisplayInfo*)display settings:(const bj::Settings&)settings {
    if (!display) return;
    if (displayInfo_ && displayInfo_.displayID == display.displayID) {
        [self updateSettings:settings];
        return;
    }

    [self stop];
    displayInfo_ = display;
    settings_ = settings;
    hasFrame_ = NO;
    [delegate_ displayCaptureLog:[NSString stringWithFormat:@"Composite display capture started: %@", display.title]];
}

- (void)updateSettings:(const bj::Settings&)settings {
    settings_ = settings;
}

- (std::vector<bj::RGB>)latestPixels {
    const NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    const NSTimeInterval minInterval = 1.0 / std::max(1, settings_.fps);
    if (now - lastCaptureTime_ >= minInterval || !hasFrame_) {
        [self captureFrame];
        lastCaptureTime_ = now;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return latestPixels_;
}

- (BOOL)hasFrame {
    return hasFrame_;
}

- (void)stop {
    displayInfo_ = nil;
    hasFrame_ = NO;
    captureErrorLogged_ = NO;
    lastCaptureTime_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    latestPixels_.clear();
}

- (void)captureFrame {
    if (!displayInfo_) return;

    using CreateImageFn = CGImageRef (*)(CGRect, uint32_t, uint32_t, uint32_t);
    static CreateImageFn createImage = []() -> CreateImageFn {
        void* handle = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_LAZY);
        if (!handle) return nullptr;
        return reinterpret_cast<CreateImageFn>(dlsym(handle, "CGWindowListCreateImage"));
    }();

    if (!createImage) {
        if (!captureErrorLogged_) {
            captureErrorLogged_ = YES;
            [delegate_ displayCaptureLog:@"CoreGraphics composite capture API is unavailable"];
        }
        return;
    }

    CGImageRef image = createImage(
        displayInfo_.bounds,
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        kCGWindowImageDefault);
    if (!image) {
        if (!captureErrorLogged_) {
            captureErrorLogged_ = YES;
            [delegate_ displayCaptureLog:@"CoreGraphics capture returned no image"];
        }
        return;
    }

    const size_t width = std::max(1, settings_.sampleWidth);
    const size_t height = std::max(1, settings_.sampleHeight);
    std::vector<bj::RGB> pixels(width * height);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        pixels.data(),
        width,
        height,
        8,
        width * sizeof(bj::RGB),
        colorSpace,
        CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);

    if (context) {
        CGContextSetInterpolationQuality(context, kCGInterpolationLow);
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
        CGContextRelease(context);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            latestPixels_.swap(pixels);
            hasFrame_ = YES;
        }
        captureErrorLogged_ = NO;
    } else if (!captureErrorLogged_) {
        captureErrorLogged_ = YES;
        [delegate_ displayCaptureLog:@"CoreGraphics bitmap context creation failed"];
    }

    CGColorSpaceRelease(colorSpace);
    CGImageRelease(image);
}

@end
