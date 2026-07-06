#include "lib/platform/windows/led/WinBleLed.hpp"

#include <initguid.h>
#include <bthledef.h>
#include <bluetoothleapis.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <vector>

struct WinBleLed::CharacteristicStorage {
    BTH_LE_GATT_CHARACTERISTIC value {};
};

static bool isBjLedCharacteristic(const BTH_LE_UUID& uuid) {
    if (uuid.IsShortUuid) return uuid.Value.ShortUuid == 0xEE01;
    static const GUID bjLedUuid {0x0000ee01, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
    return IsEqualGUID(uuid.Value.LongUuid, bjLedUuid);
}

WinBleLed::~WinBleLed() {
    close();
}

void WinBleLed::close() {
    ready_ = false;
    delete characteristic_;
    characteristic_ = nullptr;
    if (device_ != INVALID_HANDLE_VALUE) {
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
}

bool WinBleLed::connect(const wchar_t*) {
    close();

    HDEVINFO deviceInfo = SetupDiGetClassDevsW(
        &GUID_BLUETOOTHLE_DEVICE_INTERFACE,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) return false;

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &GUID_BLUETOOTHLE_DEVICE_INTERFACE, index, &interfaceData)) break;

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        if (!requiredSize) continue;

        std::vector<unsigned char> detailBuffer(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail, requiredSize, nullptr, nullptr)) continue;

        HANDLE device = CreateFileW(
            detail->DevicePath,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (device == INVALID_HANDLE_VALUE) continue;

        USHORT serviceCount = 0;
        HRESULT hr = BluetoothGATTGetServices(device, 0, nullptr, &serviceCount, BLUETOOTH_GATT_FLAG_NONE);
        if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || serviceCount == 0) {
            CloseHandle(device);
            continue;
        }

        std::vector<BTH_LE_GATT_SERVICE> services(serviceCount);
        hr = BluetoothGATTGetServices(device, serviceCount, services.data(), &serviceCount, BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr)) {
            CloseHandle(device);
            continue;
        }

        for (const BTH_LE_GATT_SERVICE& service : services) {
            USHORT characteristicCount = 0;
            hr = BluetoothGATTGetCharacteristics(device, const_cast<BTH_LE_GATT_SERVICE*>(&service), 0, nullptr, &characteristicCount, BLUETOOTH_GATT_FLAG_NONE);
            if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || characteristicCount == 0) continue;

            std::vector<BTH_LE_GATT_CHARACTERISTIC> characteristics(characteristicCount);
            hr = BluetoothGATTGetCharacteristics(device, const_cast<BTH_LE_GATT_SERVICE*>(&service), characteristicCount, characteristics.data(), &characteristicCount, BLUETOOTH_GATT_FLAG_NONE);
            if (FAILED(hr)) continue;

            for (const BTH_LE_GATT_CHARACTERISTIC& characteristic : characteristics) {
                if (!isBjLedCharacteristic(characteristic.CharacteristicUuid)) continue;
                device_ = device;
                characteristic_ = new CharacteristicStorage();
                characteristic_->value = characteristic;
                ready_ = true;
                SetupDiDestroyDeviceInfoList(deviceInfo);
                return true;
            }
        }

        CloseHandle(device);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return false;
}

bool WinBleLed::isReady() const {
    return ready_;
}

void WinBleLed::write(bj::RGB color, int maxChannel) {
    if (!ready_ || device_ == INVALID_HANDLE_VALUE || !characteristic_) return;
    std::array<uint8_t, 8> packet = bj::colorPacket(color, maxChannel);
    std::vector<unsigned char> buffer(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + packet.size() - 1);
    auto* value = reinterpret_cast<BTH_LE_GATT_CHARACTERISTIC_VALUE*>(buffer.data());
    value->DataSize = ULONG(packet.size());
    std::copy(packet.begin(), packet.end(), value->Data);
    BluetoothGATTSetCharacteristicValue(device_, &characteristic_->value, value, 0, BLUETOOTH_GATT_FLAG_NONE);
}
