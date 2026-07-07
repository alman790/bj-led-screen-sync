#include "lib/platform/windows/led/WinBleLed.hpp"

#include "lib/bj_ble.hpp"

#include <initguid.h>
#include <bthledef.h>
#include <bluetoothleapis.h>
#include <setupapi.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct WinBleLed::CharacteristicStorage {
    BTH_LE_GATT_CHARACTERISTIC value {};
    winrt::Windows::Devices::Bluetooth::BluetoothLEDevice winrtDevice {nullptr};
    winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic winrtCharacteristic {nullptr};
    bool usesWinrt = false;
};

static bool isBjLedCharacteristic(const BTH_LE_UUID& uuid) {
    if (uuid.IsShortUuid) return uuid.Value.ShortUuid == 0xEE01;
    static const GUID bjLedUuid {0x0000ee01, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
    return IsEqualGUID(uuid.Value.LongUuid, bjLedUuid);
}

static bool isBjLedCharacteristic(winrt::guid uuid) {
    return uuid.Data1 == 0x0000ee01
        && uuid.Data2 == 0x0000
        && uuid.Data3 == 0x1000
        && uuid.Data4[0] == 0x80
        && uuid.Data4[1] == 0x00
        && uuid.Data4[2] == 0x00
        && uuid.Data4[3] == 0x80
        && uuid.Data4[4] == 0x5f
        && uuid.Data4[5] == 0x9b
        && uuid.Data4[6] == 0x34
        && uuid.Data4[7] == 0xfb;
}

static std::string narrowAscii(const winrt::hstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        out.push_back(c <= 0x7f ? char(c) : '?');
    }
    return out;
}

static std::wstring wideString(const winrt::hstring& text) {
    return std::wstring(text.c_str(), text.size());
}

static std::string narrowAscii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text) out.push_back(c <= 0x7f ? char(c) : '?');
    return out;
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

bool WinBleLed::connect(const wchar_t* address) {
    if (address && *address) {
        std::wstring wide(address);
        std::string text = narrowAscii(wide);
        if (auto parsed = bj::ble::parseBluetoothAddress(text)) {
            return connect(*parsed);
        }
        wchar_t* end = nullptr;
        const uint64_t value = std::wcstoull(address, &end, 0);
        if (end && *end == L'\0' && value != 0) return connect(value);
    }
    return connectKnownGattDevice();
}

bool WinBleLed::connect(uint64_t address) {
    close();
    if (!address) return false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        namespace bt = winrt::Windows::Devices::Bluetooth;
        namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

        auto device = bt::BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
        if (!device) return false;

        auto services = device.GetGattServicesAsync(bt::BluetoothCacheMode::Uncached).get();
        if (services.Status() != gatt::GattCommunicationStatus::Success) return false;

        for (const auto& service : services.Services()) {
            auto characteristics = service.GetCharacteristicsAsync(bt::BluetoothCacheMode::Uncached).get();
            if (characteristics.Status() != gatt::GattCommunicationStatus::Success) continue;
            for (const auto& characteristic : characteristics.Characteristics()) {
                if (!isBjLedCharacteristic(characteristic.Uuid())) continue;
                characteristic_ = new CharacteristicStorage();
                characteristic_->winrtDevice = device;
                characteristic_->winrtCharacteristic = characteristic;
                characteristic_->usesWinrt = true;
                ready_ = true;
                return true;
            }
        }
    } catch (const winrt::hresult_error&) {
        close();
        return false;
    }
    close();
    return false;
}

bool WinBleLed::connectKnownGattDevice() {
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
    if (!ready_ || !characteristic_) return;
    std::array<uint8_t, 8> packet = bj::colorPacket(color, maxChannel);
    if (characteristic_->usesWinrt) {
        try {
            namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
            winrt::Windows::Storage::Streams::DataWriter writer;
            writer.WriteBytes(winrt::array_view<const uint8_t>(packet.data(), packet.data() + packet.size()));
            auto buffer = writer.DetachBuffer();
            auto result = characteristic_->winrtCharacteristic.WriteValueWithResultAsync(buffer, gatt::GattWriteOption::WriteWithResponse).get();
            if (result.Status() != gatt::GattCommunicationStatus::Success) {
                characteristic_->winrtCharacteristic.WriteValueWithResultAsync(buffer, gatt::GattWriteOption::WriteWithoutResponse).get();
            }
        } catch (const winrt::hresult_error&) {
            ready_ = false;
        }
        return;
    }

    if (device_ == INVALID_HANDLE_VALUE) return;
    std::vector<unsigned char> buffer(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + packet.size() - 1);
    auto* value = reinterpret_cast<BTH_LE_GATT_CHARACTERISTIC_VALUE*>(buffer.data());
    value->DataSize = ULONG(packet.size());
    std::copy(packet.begin(), packet.end(), value->Data);
    BluetoothGATTSetCharacteristicValue(device_, &characteristic_->value, value, 0, BLUETOOTH_GATT_FLAG_NONE);
}

std::vector<WinBleDeviceInfo> WinBleLed::scan(int timeoutMs, int limit) {
    std::vector<WinBleDeviceInfo> devices;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        namespace adv = winrt::Windows::Devices::Bluetooth::Advertisement;
        adv::BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(adv::BluetoothLEScanningMode::Active);

        std::mutex mutex;
        auto token = watcher.Received([&](const adv::BluetoothLEAdvertisementWatcher&, const adv::BluetoothLEAdvertisementReceivedEventArgs& args) {
            const winrt::hstring localName = args.Advertisement().LocalName();
            const std::string name = narrowAscii(localName);
            if (!bj::ble::isBjLedName(name)) return;

            std::lock_guard<std::mutex> lock(mutex);
            const uint64_t address = args.BluetoothAddress();
            auto existing = std::find_if(devices.begin(), devices.end(), [address](const WinBleDeviceInfo& device) {
                return device.address == address;
            });
            if (existing == devices.end()) {
                devices.push_back({address, wideString(localName), args.RawSignalStrengthInDBm()});
            } else if (args.RawSignalStrengthInDBm() > existing->rssi) {
                existing->rssi = args.RawSignalStrengthInDBm();
                existing->name = wideString(localName);
            }
            if (int(devices.size()) >= limit) watcher.Stop();
        });

        watcher.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(500, timeoutMs)));
        watcher.Stop();
        watcher.Received(token);
    } catch (const winrt::hresult_error&) {
        return devices;
    }

    std::sort(devices.begin(), devices.end(), [](const WinBleDeviceInfo& a, const WinBleDeviceInfo& b) {
        const bool exactA = a.name == L"BJ_LED" || a.name == L"BJ_LED_M";
        const bool exactB = b.name == L"BJ_LED" || b.name == L"BJ_LED_M";
        if (exactA != exactB) return exactA;
        return a.rssi > b.rssi;
    });
    return devices;
}
