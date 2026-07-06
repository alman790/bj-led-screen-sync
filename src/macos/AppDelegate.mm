#import "lib/macos/AppDelegate.hpp"

#include <array>
#include <vector>

@implementation AppDelegate {
    NSWindow* window_;
    NSPopUpButton* devicePopup_;
    NSPopUpButton* displayPopup_;
    NSPopUpButton* modePopup_;
    NSSlider* fpsSlider_;
    NSSlider* brightnessSlider_;
    NSSlider* saturationSlider_;
    NSSlider* smoothingSlider_;
    NSSlider* thresholdSlider_;
    NSTextField* fpsValueLabel_;
    NSTextField* brightnessValueLabel_;
    NSTextField* saturationValueLabel_;
    NSTextField* smoothingValueLabel_;
    NSTextField* thresholdValueLabel_;
    NSSegmentedControl* maxSegment_;
    NSSegmentedControl* outputSegment_;
    NSView* swatch_;
    std::array<NSView*, 4> cornerSwatches_;
    NSTextField* statusLabel_;
    NSTextField* colorLabel_;
    NSTextField* liveLabel_;
    NSTextView* logView_;
    LedBluetooth* bluetooth_;
    DisplayCapture* displayCapture_;
    NSArray<DisplayInfo*>* displays_;
    NSTimer* timer_;
    bj::ColorAnalyzer analyzer_;
    bj::RGB lastSent_;
    bj::RGB smoothed_;
    bj::FrameAnalysis frameAnalysis_;
    NSTimeInterval lastWriteTime_;
    bool hasSmoothed_;
    bool captureFailureLogged_;
    bool stripReady_;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    bluetooth_ = [[LedBluetooth alloc] initWithDelegate:self];
    displayCapture_ = [[DisplayCapture alloc] initWithDelegate:self];
    lastWriteTime_ = 0;
    hasSmoothed_ = false;
    captureFailureLogged_ = false;
    stripReady_ = false;
    [self buildWindow];
    [self refreshDisplays];
    [self restartDisplayStream];
    [self startLiveLoop];
    [self log:@"Live sampler started"];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

- (void)buildWindow {
    window_ = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 940, 650)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window_.title = @"BJ LED Ambilight";
    window_.titlebarAppearsTransparent = YES;
    window_.movableByWindowBackground = YES;
    window_.backgroundColor = NSColor.clearColor;
    NSVisualEffectView* glassRoot = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, 940, 650)];
    glassRoot.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    glassRoot.material = NSVisualEffectMaterialHUDWindow;
    glassRoot.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    glassRoot.state = NSVisualEffectStateActive;
    glassRoot.wantsLayer = YES;
    glassRoot.layer.cornerRadius = 18;
    glassRoot.layer.borderWidth = 1;
    glassRoot.layer.borderColor = [NSColor colorWithCalibratedWhite:1 alpha:0.16].CGColor;
    window_.contentView = glassRoot;
    [window_ center];

    NSView* root = window_.contentView;
    CGFloat y = 592;
    NSImageView* logoView = [[NSImageView alloc] initWithFrame:NSMakeRect(30, y - 2, 38, 38)];
    logoView.image = [NSImage imageNamed:@"app-icon.png"];
    logoView.imageScaling = NSImageScaleProportionallyUpOrDown;
    logoView.wantsLayer = YES;
    logoView.layer.cornerRadius = 9;
    logoView.layer.masksToBounds = YES;
    [root addSubview:logoView];
    [root addSubview:[self label:@"BJ LED Ambilight" frame:NSMakeRect(80, y, 360, 34) size:28 bold:YES]];
    statusLabel_ = nil;

    y -= 66;
    [root addSubview:[self label:@"Device" frame:NSMakeRect(28, y + 5, 110, 24) size:15 bold:YES]];
    devicePopup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150, y, 350, 32) pullsDown:NO];
    [devicePopup_ addItemWithTitle:@"No device yet"];
    [root addSubview:devicePopup_];
    [root addSubview:[self button:@"Scan" frame:NSMakeRect(518, y, 110, 32) action:@selector(scan:)]];
    [root addSubview:[self button:@"Connect" frame:NSMakeRect(640, y, 122, 32) action:@selector(connect:)]];

    y -= 52;
    [root addSubview:[self label:@"Display" frame:NSMakeRect(28, y + 5, 110, 24) size:15 bold:YES]];
    displayPopup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150, y, 250, 32) pullsDown:NO];
    displayPopup_.target = self;
    displayPopup_.action = @selector(controlChanged:);
    [root addSubview:displayPopup_];
    [root addSubview:[self label:@"Mode" frame:NSMakeRect(428, y + 5, 70, 24) size:15 bold:YES]];
    modePopup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(498, y, 176, 32) pullsDown:NO];
    [modePopup_ addItemsWithTitles:@[@"Balanced", @"Average", @"Vibrant"]];
    modePopup_.target = self;
    modePopup_.action = @selector(controlChanged:);
    [root addSubview:modePopup_];
    maxSegment_ = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(704, y + 1, 128, 30)];
    [maxSegment_ setSegmentCount:2];
    [maxSegment_ setLabel:@"127" forSegment:0];
    [maxSegment_ setLabel:@"255" forSegment:1];
    [maxSegment_ setSelectedSegment:1];
    maxSegment_.target = self;
    maxSegment_.action = @selector(controlChanged:);
    [root addSubview:maxSegment_];

    y -= 72;
    fpsSlider_ = [self slider:@"FPS" y:y min:1 max:30 value:10 valueLabel:&fpsValueLabel_];
    y -= 54;
    brightnessSlider_ = [self slider:@"Brightness" y:y min:0.15 max:1.0 value:1.0 valueLabel:&brightnessValueLabel_];
    y -= 54;
    saturationSlider_ = [self slider:@"Saturation" y:y min:0.5 max:2.5 value:1.35 valueLabel:&saturationValueLabel_];
    y -= 54;
    smoothingSlider_ = [self slider:@"Smoothing" y:y min:0.01 max:1.0 value:0.55 valueLabel:&smoothingValueLabel_];
    y -= 54;
    thresholdSlider_ = [self slider:@"Threshold" y:y min:0.0 max:35.0 value:1.0 valueLabel:&thresholdValueLabel_];

    swatch_ = [[NSView alloc] initWithFrame:NSMakeRect(730, 274, 132, 132)];
    swatch_.wantsLayer = YES;
    swatch_.layer.cornerRadius = 18;
    swatch_.layer.borderWidth = 1;
    swatch_.layer.borderColor = [NSColor colorWithCalibratedWhite:1 alpha:0.20].CGColor;
    swatch_.layer.backgroundColor = NSColor.blackColor.CGColor;
    [root addSubview:swatch_];
    const CGFloat mini = 28;
    const CGFloat gap = 8;
    const CGFloat sx = 730;
    const CGFloat sy = 274;
    const std::array<NSRect, 4> miniFrames {
        NSMakeRect(sx - mini - gap, sy + 132 - mini, mini, mini),
        NSMakeRect(sx + 132 + gap, sy + 132 - mini, mini, mini),
        NSMakeRect(sx - mini - gap, sy, mini, mini),
        NSMakeRect(sx + 132 + gap, sy, mini, mini),
    };
    for (size_t index = 0; index < cornerSwatches_.size(); ++index) {
        cornerSwatches_[index] = [[NSView alloc] initWithFrame:miniFrames[index]];
        cornerSwatches_[index].wantsLayer = YES;
        cornerSwatches_[index].layer.cornerRadius = 7;
        cornerSwatches_[index].layer.borderWidth = 1;
        cornerSwatches_[index].layer.borderColor = [NSColor colorWithCalibratedWhite:1 alpha:0.18].CGColor;
        cornerSwatches_[index].layer.backgroundColor = NSColor.blackColor.CGColor;
        [root addSubview:cornerSwatches_[index]];
    }
    colorLabel_ = [self label:@"RGB 0 0 0" frame:NSMakeRect(694, 235, 210, 26) size:15 bold:YES];
    colorLabel_.alignment = NSTextAlignmentCenter;
    [root addSubview:colorLabel_];
    liveLabel_ = [self label:@"" frame:NSMakeRect(676, 206, 250, 24) size:13 bold:NO];
    liveLabel_.alignment = NSTextAlignmentCenter;
    [root addSubview:liveLabel_];

    outputSegment_ = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(28, 150, 430, 34)];
    [outputSegment_ setSegmentCount:5];
    [outputSegment_ setLabel:@"Auto" forSegment:0];
    [outputSegment_ setLabel:@"Red" forSegment:1];
    [outputSegment_ setLabel:@"Green" forSegment:2];
    [outputSegment_ setLabel:@"Blue" forSegment:3];
    [outputSegment_ setLabel:@"White" forSegment:4];
    [outputSegment_ setSelectedSegment:0];
    outputSegment_.target = self;
    outputSegment_.action = @selector(outputChanged:);
    [root addSubview:outputSegment_];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(28, 24, 884, 106)];
    scroll.hasVerticalScroller = YES;
    scroll.wantsLayer = YES;
    scroll.layer.cornerRadius = 8;
    scroll.layer.borderWidth = 1;
    scroll.layer.borderColor = [NSColor colorWithCalibratedWhite:1 alpha:0.10].CGColor;
    logView_ = [[NSTextView alloc] initWithFrame:scroll.bounds];
    logView_.editable = NO;
    logView_.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    logView_.textColor = [NSColor colorWithCalibratedWhite:0.90 alpha:0.88];
    logView_.backgroundColor = [NSColor colorWithCalibratedWhite:0 alpha:0.22];
    scroll.documentView = logView_;
    [root addSubview:scroll];

    [window_ makeKeyAndOrderFront:nil];
}

- (NSTextField*)label:(NSString*)text frame:(NSRect)frame size:(CGFloat)size bold:(BOOL)bold {
    NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
    label.stringValue = text;
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.editable = NO;
    label.selectable = NO;
    label.textColor = [NSColor colorWithCalibratedWhite:0.9 alpha:1];
    label.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    return label;
}

- (NSButton*)button:(NSString*)title frame:(NSRect)frame action:(SEL)action {
    NSButton* button = [[NSButton alloc] initWithFrame:frame];
    button.title = title;
    button.bezelStyle = NSBezelStyleRounded;
    button.target = self;
    button.action = action;
    return button;
}

- (NSSlider*)slider:(NSString*)name y:(CGFloat)y min:(double)min max:(double)max value:(double)value valueLabel:(NSTextField* __strong*)valueLabel {
    [window_.contentView addSubview:[self label:name frame:NSMakeRect(28, y + 2, 116, 24) size:15 bold:YES]];
    NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(150, y, 480, 26)];
    slider.minValue = min;
    slider.maxValue = max;
    slider.doubleValue = value;
    slider.continuous = YES;
    slider.target = self;
    slider.action = @selector(sliderChanged:);
    [window_.contentView addSubview:slider];
    *valueLabel = [self label:[NSString stringWithFormat:@"%.2f", value] frame:NSMakeRect(650, y + 2, 62, 24) size:14 bold:NO];
    (*valueLabel).alignment = NSTextAlignmentRight;
    [window_.contentView addSubview:*valueLabel];
    return slider;
}

- (void)sliderChanged:(id)sender {
    NSSlider* slider = (NSSlider*)sender;
    NSTextField* label = nil;
    if (sender == fpsSlider_) label = fpsValueLabel_;
    if (sender == brightnessSlider_) label = brightnessValueLabel_;
    if (sender == saturationSlider_) label = saturationValueLabel_;
    if (sender == smoothingSlider_) label = smoothingValueLabel_;
    if (sender == thresholdSlider_) label = thresholdValueLabel_;
    label.stringValue = [NSString stringWithFormat:@"%.2f", slider.doubleValue];
    if (sender == fpsSlider_) {
        [self startLiveLoop];
    }
    if (sender == smoothingSlider_) hasSmoothed_ = false;
}

- (void)controlChanged:(id)sender {
    hasSmoothed_ = false;
    [self restartDisplayStream];
}

- (void)refreshDisplays {
    [displayPopup_ removeAllItems];
    displays_ = [DisplayCapture activeDisplays];
    if (displays_.count == 0) {
        [displayPopup_ addItemWithTitle:@"No displays"];
        [self log:@"No active displays found"];
        return;
    }
    for (DisplayInfo* display in displays_) {
        [displayPopup_ addItemWithTitle:display.title];
    }
    liveLabel_.stringValue = @"";
    [self log:[NSString stringWithFormat:@"Displays ready: %lu", displays_.count]];
}

- (void)restartDisplayStream {
    if (displayPopup_.indexOfSelectedItem < 0 || displayPopup_.indexOfSelectedItem >= (NSInteger)displays_.count) return;
    [displayCapture_ startWithDisplay:displays_[displayPopup_.indexOfSelectedItem] settings:[self settings]];
    hasSmoothed_ = false;
}

- (bj::Settings)settings {
    bj::Settings settings;
    settings.fps = std::max(1, int(std::round(fpsSlider_.doubleValue)));
    settings.brightness = brightnessSlider_.floatValue;
    settings.saturation = saturationSlider_.floatValue;
    settings.smoothing = smoothingSlider_.floatValue;
    settings.threshold = thresholdSlider_.floatValue;
    settings.maxChannel = maxSegment_.selectedSegment == 0 ? 127 : 255;
    switch (modePopup_.indexOfSelectedItem) {
        case 1: settings.mode = bj::SampleMode::Average; break;
        case 2: settings.mode = bj::SampleMode::Vibrant; break;
        default: settings.mode = bj::SampleMode::Balanced; break;
    }
    return settings;
}

- (void)startLiveLoop {
    [timer_ invalidate];
    bj::Settings settings = [self settings];
    timer_ = [NSTimer scheduledTimerWithTimeInterval:1.0 / settings.fps target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}

- (void)tick:(NSTimer*)timer {
    bj::Settings settings = [self settings];
    [displayCapture_ updateSettings:settings];
    std::vector<bj::RGB> pixels = [displayCapture_ latestPixels];
    if (pixels.empty()) {
        liveLabel_.stringValue = @"";
        if (!captureFailureLogged_) {
            captureFailureLogged_ = true;
            [self log:@"Waiting for display stream frame. If this persists, re-grant Screen Recording to BJLEDAmbilight and reopen."];
        }
        return;
    }

    captureFailureLogged_ = false;
    liveLabel_.stringValue = @"";
    frameAnalysis_ = analyzer_.analyzeFrame(pixels, settings.sampleWidth, settings.sampleHeight, settings);
    bj::RGB color = [self selectedOutputColor:frameAnalysis_.output];
    bj::RGB output = [self isAutoOutput] && hasSmoothed_ ? bj::smooth(smoothed_, color, settings.smoothing) : color;
    hasSmoothed_ = true;
    smoothed_ = output;
    [self updateSwatch:output];
    const NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    const bool forceRefresh = now - lastWriteTime_ >= 0.25;
    const bool colorChanged = bj::distance(lastSent_, output) >= settings.threshold;
    if (stripReady_ && (colorChanged || forceRefresh)) {
        lastSent_ = output;
        lastWriteTime_ = now;
        [bluetooth_ writeColor:output maxChannel:settings.maxChannel];
    }
}

- (void)updateSwatch:(bj::RGB)color {
    swatch_.layer.backgroundColor = [NSColor colorWithCalibratedRed:color.r / 255.0
                                                              green:color.g / 255.0
                                                               blue:color.b / 255.0
                                                              alpha:1].CGColor;
    for (size_t index = 0; index < cornerSwatches_.size(); ++index) {
        bj::RGB corner = frameAnalysis_.corners[index];
        cornerSwatches_[index].layer.backgroundColor = [NSColor colorWithCalibratedRed:corner.r / 255.0
                                                                                 green:corner.g / 255.0
                                                                                  blue:corner.b / 255.0
                                                                                 alpha:1].CGColor;
    }
    colorLabel_.stringValue = [NSString stringWithFormat:@"RGB %u %u %u", color.r, color.g, color.b];
}

- (void)scan:(id)sender {
    [devicePopup_ removeAllItems];
    [devicePopup_ addItemWithTitle:@"Scanning..."];
    [bluetooth_ scan];
}

- (void)connect:(id)sender {
    [bluetooth_ connectIndex:devicePopup_.indexOfSelectedItem];
}

- (BOOL)isAutoOutput {
    return outputSegment_.selectedSegment <= 0;
}

- (bj::RGB)selectedOutputColor:(bj::RGB)autoColor {
    switch (outputSegment_.selectedSegment) {
        case 1: return bj::RGB(255, 0, 0);
        case 2: return bj::RGB(0, 255, 0);
        case 3: return bj::RGB(0, 0, 255);
        case 4: return bj::RGB(255, 255, 255);
        default: return autoColor;
    }
}

- (void)outputChanged:(id)sender {
    hasSmoothed_ = false;
    lastWriteTime_ = 0;
    bj::RGB color = [self selectedOutputColor:frameAnalysis_.output];
    [self updateSwatch:color];
    if (stripReady_) {
        lastSent_ = color;
        [bluetooth_ writeColor:color maxChannel:[self settings].maxChannel];
    }
}

- (void)log:(NSString*)line {
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self log:line];
        });
        return;
    }
    NSString* next = [logView_.string stringByAppendingFormat:@"%@\n", line];
    if (next.length > 12000) {
        next = [next substringFromIndex:next.length - 9000];
    }
    logView_.string = next;
    [logView_ scrollRangeToVisible:NSMakeRange(logView_.string.length, 0)];
}

- (void)bluetoothLog:(NSString*)line {
    [self log:line];
}

- (void)bluetoothDeviceFound:(CBPeripheral*)peripheral name:(NSString*)name rssi:(NSNumber*)rssi {
    if (devicePopup_.numberOfItems == 1 && [[devicePopup_ itemTitleAtIndex:0] isEqualToString:@"Scanning..."]) {
        [devicePopup_ removeAllItems];
    }
    [devicePopup_ addItemWithTitle:[NSString stringWithFormat:@"%@  RSSI %@", name, rssi]];
    [self log:[NSString stringWithFormat:@"Found %@ %@", name, peripheral.identifier.UUIDString]];
}

- (void)bluetoothStatusChanged:(NSString*)status {
    statusLabel_.stringValue = status;
    [self log:status];
}

- (void)bluetoothReadyChanged:(BOOL)ready {
    stripReady_ = ready;
    hasSmoothed_ = false;
    liveLabel_.stringValue = @"";
    [self log:ready ? @"Strip ready, live writing enabled" : @"Strip disconnected"];
}

- (void)displayCaptureLog:(NSString*)line {
    [self log:line];
}

@end
