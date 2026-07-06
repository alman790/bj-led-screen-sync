#pragma once

#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "lib/bj_core.hpp"

@protocol LedBluetoothDelegate <NSObject>
- (void)bluetoothLog:(NSString*)line;
- (void)bluetoothDeviceFound:(CBPeripheral*)peripheral name:(NSString*)name rssi:(NSNumber*)rssi;
- (void)bluetoothStatusChanged:(NSString*)status;
- (void)bluetoothReadyChanged:(BOOL)ready;
@end

@interface LedBluetooth : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype)initWithDelegate:(id<LedBluetoothDelegate>)delegate;
- (void)scan;
- (void)connectIndex:(NSInteger)index;
- (void)writeColor:(bj::RGB)color maxChannel:(int)maxChannel;
- (BOOL)isReady;
@end
