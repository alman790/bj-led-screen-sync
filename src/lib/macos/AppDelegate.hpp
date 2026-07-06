#pragma once

#import <AppKit/AppKit.h>

#import "lib/macos/DisplayCapture.hpp"
#import "lib/macos/LedBluetooth.hpp"

@interface AppDelegate : NSObject <NSApplicationDelegate, LedBluetoothDelegate, DisplayCaptureDelegate>
@end
