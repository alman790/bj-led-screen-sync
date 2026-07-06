#import "lib/macos/LedBluetooth.hpp"

#include <array>

@implementation LedBluetooth {
    __weak id<LedBluetoothDelegate> delegate_;
    CBCentralManager* central_;
    NSMutableArray<CBPeripheral*>* peripherals_;
    CBPeripheral* connected_;
    CBCharacteristic* writeCharacteristic_;
}

- (instancetype)initWithDelegate:(id<LedBluetoothDelegate>)delegate {
    self = [super init];
    if (!self) return nil;
    delegate_ = delegate;
    peripherals_ = [NSMutableArray array];
    central_ = [[CBCentralManager alloc] initWithDelegate:self queue:dispatch_get_main_queue()];
    return self;
}

- (BOOL)isReady {
    return connected_ && writeCharacteristic_;
}

- (void)scan {
    [peripherals_ removeAllObjects];
    writeCharacteristic_ = nil;
    connected_ = nil;
    if (central_.state != CBManagerStatePoweredOn) {
        [delegate_ bluetoothLog:@"Bluetooth is not ready yet"];
        return;
    }
    [delegate_ bluetoothLog:@"Scanning for BJ_LED..."];
    [central_ scanForPeripheralsWithServices:nil options:@{ CBCentralManagerScanOptionAllowDuplicatesKey: @NO }];
}

- (void)connectIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)peripherals_.count) return;
    [central_ stopScan];
    connected_ = peripherals_[index];
    connected_.delegate = self;
    NSString* name = connected_.name ? connected_.name : connected_.identifier.UUIDString;
    [delegate_ bluetoothLog:[NSString stringWithFormat:@"Connecting to %@...", name]];
    [central_ connectPeripheral:connected_ options:nil];
}

- (void)writeColor:(bj::RGB)color maxChannel:(int)maxChannel {
    if (![self isReady]) return;
    std::array<uint8_t, 8> packet = bj::colorPacket(color, maxChannel);
    NSData* data = [NSData dataWithBytes:packet.data() length:packet.size()];
    const BOOL supportsWriteWithoutResponse = writeCharacteristic_.properties & CBCharacteristicPropertyWriteWithoutResponse;
    if (supportsWriteWithoutResponse && !connected_.canSendWriteWithoutResponse) return;
    CBCharacteristicWriteType type = supportsWriteWithoutResponse
        ? CBCharacteristicWriteWithoutResponse
        : CBCharacteristicWriteWithResponse;
    [connected_ writeValue:data forCharacteristic:writeCharacteristic_ type:type];
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)central {
    switch (central.state) {
        case CBManagerStatePoweredOn:
            [delegate_ bluetoothStatusChanged:@"Bluetooth ready"];
            break;
        case CBManagerStateUnauthorized:
            [delegate_ bluetoothStatusChanged:@"Bluetooth permission denied"];
            break;
        case CBManagerStatePoweredOff:
            [delegate_ bluetoothStatusChanged:@"Bluetooth off"];
            break;
        default:
            [delegate_ bluetoothStatusChanged:@"Bluetooth starting..."];
            break;
    }
}

- (void)centralManager:(CBCentralManager*)central
 didDiscoverPeripheral:(CBPeripheral*)peripheral
     advertisementData:(NSDictionary<NSString*, id>*)advertisementData
                  RSSI:(NSNumber*)RSSI {
    NSString* name = peripheral.name ? peripheral.name : advertisementData[CBAdvertisementDataLocalNameKey];
    if (!name || ![name.uppercaseString hasPrefix:@"BJ_LED"]) return;
    for (CBPeripheral* existing in peripherals_) {
        if ([existing.identifier isEqual:peripheral.identifier]) return;
    }
    [peripherals_ addObject:peripheral];
    [delegate_ bluetoothDeviceFound:peripheral name:name rssi:RSSI];
}

- (void)centralManager:(CBCentralManager*)central didConnectPeripheral:(CBPeripheral*)peripheral {
    [delegate_ bluetoothLog:@"Connected, discovering services..."];
    [peripheral discoverServices:nil];
}

- (void)centralManager:(CBCentralManager*)central didFailToConnectPeripheral:(CBPeripheral*)peripheral error:(NSError*)error {
    [delegate_ bluetoothLog:[NSString stringWithFormat:@"Connect failed: %@", error.localizedDescription]];
    [delegate_ bluetoothReadyChanged:NO];
}

- (void)centralManager:(CBCentralManager*)central didDisconnectPeripheral:(CBPeripheral*)peripheral error:(NSError*)error {
    [delegate_ bluetoothLog:@"Disconnected"];
    writeCharacteristic_ = nil;
    connected_ = nil;
    [delegate_ bluetoothReadyChanged:NO];
}

- (void)peripheral:(CBPeripheral*)peripheral didDiscoverServices:(NSError*)error {
    if (error) {
        [delegate_ bluetoothLog:error.localizedDescription];
        return;
    }
    for (CBService* service in peripheral.services) {
        [peripheral discoverCharacteristics:nil forService:service];
    }
}

- (void)peripheral:(CBPeripheral*)peripheral didDiscoverCharacteristicsForService:(CBService*)service error:(NSError*)error {
    if (error) {
        [delegate_ bluetoothLog:error.localizedDescription];
        return;
    }
    for (CBCharacteristic* characteristic in service.characteristics) {
        NSString* uuid = characteristic.UUID.UUIDString.uppercaseString;
        if ([uuid isEqualToString:@"0000EE01-0000-1000-8000-00805F9B34FB"] || [uuid isEqualToString:@"EE01"]) {
            writeCharacteristic_ = characteristic;
            [delegate_ bluetoothLog:@"BJ_LED write characteristic ready"];
            [delegate_ bluetoothReadyChanged:YES];
            return;
        }
    }
}

@end
