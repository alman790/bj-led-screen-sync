#pragma once

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <vector>

#include "lib/bj_core.hpp"

@interface DisplayInfo : NSObject
@property(nonatomic, assign) CGRect bounds;
@property(nonatomic, assign) CGDirectDisplayID displayID;
@property(nonatomic, copy) NSString* title;
+ (instancetype)displayWithID:(CGDirectDisplayID)displayID bounds:(CGRect)bounds title:(NSString*)title;
@end

@protocol DisplayCaptureDelegate <NSObject>
- (void)displayCaptureLog:(NSString*)line;
@end

@interface DisplayCapture : NSObject
- (instancetype)initWithDelegate:(id<DisplayCaptureDelegate>)delegate;
+ (NSArray<DisplayInfo*>*)activeDisplays;
- (void)startWithDisplay:(DisplayInfo*)display settings:(const bj::Settings&)settings;
- (void)updateSettings:(const bj::Settings&)settings;
- (std::vector<bj::RGB>)latestPixels;
- (BOOL)hasFrame;
- (void)stop;
@end
