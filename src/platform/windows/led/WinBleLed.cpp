#include "lib/platform/windows/led/WinBleLed.hpp"

#include "lib/bj_ble.hpp"

#include <initguid.h>
#include <bthledef.h>
#include <bluetoothleapis.h>
#include <setupapi.h>

#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <memory>
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

static std::wstring readDeviceProperty(HDEVINFO deviceInfo, SP_DEVINFO_DATA& deviceData, DWORD property) {
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(deviceInfo, &deviceData, property, &type, nullptr, 0, &required);
    if (!required || type != REG_SZ) return {};

    std::vector<wchar_t> buffer((required / sizeof(wchar_t)) + 1);
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfo,
            &deviceData,
            property,
            &type,
            reinterpret_cast<PBYTE>(buffer.data()),
            required,
            nullptr)) {
        return {};
    }
    buffer.back() = L'\0';
    return std::wstring(buffer.data());
}

static bool isHex(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

static int hexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return int(c - L'0');
    if (c >= L'a' && c <= L'f') return 10 + int(c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + int(c - L'A');
    return 0;
}

static uint64_t parseAddressFromWideText(const std::wstring& text) {
    for (size_t i = 0; i + 12 <= text.size(); ++i) {
        bool ok = true;
        uint64_t value = 0;
        for (size_t n = 0; n < 12; ++n) {
            const wchar_t c = text[i + n];
            if (!isHex(c)) {
                ok = false;
                break;
            }
            value = (value << 4U) | uint64_t(hexValue(c));
        }
        if (!ok) continue;
        const bool leftOk = i == 0 || !isHex(text[i - 1]);
        const bool rightOk = i + 12 == text.size() || !isHex(text[i + 12]);
        if (leftOk && rightOk && value != 0) return value;
    }
    return 0;
}

static std::wstring propertyString(const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, winrt::Windows::Foundation::IInspectable>& properties, const wchar_t* key) {
    try {
        const auto value = properties.Lookup(key);
        if (!value) return {};
        return std::wstring(winrt::unbox_value_or<winrt::hstring>(value, L""));
    } catch (...) {
        return {};
    }
}

static int propertySignalStrength(const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, winrt::Windows::Foundation::IInspectable>& properties) {
    try {
        const auto value = properties.Lookup(L"System.Devices.Aep.SignalStrength");
        if (!value) return -127;
        return winrt::unbox_value_or<int32_t>(value, -127);
    } catch (...) {
        return -127;
    }
}

static uint64_t parseDeviceAddress(const winrt::hstring& id, const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, winrt::Windows::Foundation::IInspectable>& properties) {
    const std::wstring address = propertyString(properties, L"System.Devices.Aep.DeviceAddress");
    if (auto parsed = bj::ble::parseBluetoothAddress(narrowAscii(address))) return *parsed;
    return parseAddressFromWideText(std::wstring(id.c_str(), id.size()));
}

static bool hasBjLedCharacteristic(HANDLE device) {
    USHORT serviceCount = 0;
    HRESULT hr = BluetoothGATTGetServices(device, 0, nullptr, &serviceCount, BLUETOOTH_GATT_FLAG_NONE);
    if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || serviceCount == 0) return false;

    std::vector<BTH_LE_GATT_SERVICE> services(serviceCount);
    hr = BluetoothGATTGetServices(device, serviceCount, services.data(), &serviceCount, BLUETOOTH_GATT_FLAG_NONE);
    if (FAILED(hr)) return false;

    for (const BTH_LE_GATT_SERVICE& service : services) {
        USHORT characteristicCount = 0;
        hr = BluetoothGATTGetCharacteristics(device, const_cast<BTH_LE_GATT_SERVICE*>(&service), 0, nullptr, &characteristicCount, BLUETOOTH_GATT_FLAG_NONE);
        if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || characteristicCount == 0) continue;

        std::vector<BTH_LE_GATT_CHARACTERISTIC> characteristics(characteristicCount);
        hr = BluetoothGATTGetCharacteristics(
            device,
            const_cast<BTH_LE_GATT_SERVICE*>(&service),
            characteristicCount,
            characteristics.data(),
            &characteristicCount,
            BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr)) continue;

        for (const BTH_LE_GATT_CHARACTERISTIC& characteristic : characteristics) {
            if (isBjLedCharacteristic(characteristic.CharacteristicUuid)) return true;
        }
    }
    return false;
}

static std::vector<WinBleDeviceInfo> scanKnownGattDevices(int limit) {
    std::vector<WinBleDeviceInfo> devices;
    HDEVINFO deviceInfo = SetupDiGetClassDevsW(
        &GUID_BLUETOOTHLE_DEVICE_INTERFACE,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) return devices;

    for (DWORD index = 0; int(devices.size()) < limit; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &GUID_BLUETOOTHLE_DEVICE_INTERFACE, index, &interfaceData)) break;

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        if (!requiredSize) continue;

        std::vector<unsigned char> detailBuffer(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA deviceData {};
        deviceData.cbSize = sizeof(deviceData);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail, requiredSize, nullptr, &deviceData)) continue;

        HANDLE device = CreateFileW(
            detail->DevicePath,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (device == INVALID_HANDLE_VALUE) continue;

        const bool compatible = hasBjLedCharacteristic(device);
        CloseHandle(device);
        if (!compatible) continue;

        std::wstring name = readDeviceProperty(deviceInfo, deviceData, SPDRP_FRIENDLYNAME);
        if (name.empty()) name = readDeviceProperty(deviceInfo, deviceData, SPDRP_DEVICEDESC);
        if (name.empty()) name = L"BJ_LED";

        const uint64_t address = parseAddressFromWideText(detail->DevicePath);
        const auto existing = std::find_if(devices.begin(), devices.end(), [address](const WinBleDeviceInfo& item) {
            return address != 0 && item.address == address;
        });
        if (existing != devices.end()) continue;

        devices.push_back({address, name, -127});
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    return devices;
}

static std::vector<WinBleDeviceInfo> scanWinrtDevices(int timeoutMs, int limit) {
    std::vector<WinBleDeviceInfo> devices;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        namespace bt = winrt::Windows::Devices::Bluetooth;
        namespace enumeration = winrt::Windows::Devices::Enumeration;

        auto requestedProperties = winrt::single_threaded_vector<winrt::hstring>();
        requestedProperties.Append(L"System.Devices.Aep.DeviceAddress");
        requestedProperties.Append(L"System.Devices.Aep.SignalStrength");

        auto watcher = enumeration::DeviceInformation::CreateWatcher(bt::BluetoothLEDevice::GetDeviceSelector(), requestedProperties);
        struct ScanState {
            std::mutex mutex;
            std::vector<WinBleDeviceInfo> devices;
            std::atomic_bool limitReached {false};
        };
        auto state = std::make_shared<ScanState>();
        const int candidateLimit = std::max(1, limit);

        auto addOrUpdate = [state, candidateLimit](const winrt::hstring& id, const winrt::hstring& name, const auto& properties) noexcept {
            try {
                const std::string asciiName = narrowAscii(name);
                if (!bj::ble::isBjLedName(asciiName)) return;

                WinBleDeviceInfo candidate;
                candidate.address = parseDeviceAddress(id, properties);
                candidate.name = wideString(name);
                candidate.rssi = propertySignalStrength(properties);

                std::lock_guard<std::mutex> lock(state->mutex);
                auto existing = std::find_if(state->devices.begin(), state->devices.end(), [&candidate](const WinBleDeviceInfo& device) {
                    return (candidate.address != 0 && device.address == candidate.address) || device.name == candidate.name;
                });
                if (existing == state->devices.end()) {
                    state->devices.push_back(candidate);
                } else {
                    if (candidate.address != 0) existing->address = candidate.address;
                    if (!candidate.name.empty()) existing->name = candidate.name;
                    if (candidate.rssi > existing->rssi) existing->rssi = candidate.rssi;
                }
                if (int(state->devices.size()) >= candidateLimit) state->limitReached = true;
            } catch (...) {
            }
        };

        auto addedToken = watcher.Added([addOrUpdate](const enumeration::DeviceWatcher&, const enumeration::DeviceInformation& info) noexcept {
            addOrUpdate(info.Id(), info.Name(), info.Properties());
        });
        auto updatedToken = watcher.Updated([state](const enumeration::DeviceWatcher&, const enumeration::DeviceInformationUpdate& update) noexcept {
            try {
                const uint64_t address = parseDeviceAddress(update.Id(), update.Properties());
                const int rssi = propertySignalStrength(update.Properties());
                if (address == 0 && rssi <= -127) return;

                std::lock_guard<std::mutex> lock(state->mutex);
                for (WinBleDeviceInfo& device : state->devices) {
                    if (address != 0 && device.address == address) {
                        if (rssi > device.rssi) device.rssi = rssi;
                        return;
                    }
                }
            } catch (...) {
            }
        });

        watcher.Start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(800, timeoutMs));
        while (std::chrono::steady_clock::now() < deadline && !state->limitReached.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        try {
            watcher.Stop();
        } catch (...) {
        }
        watcher.Added(addedToken);
        watcher.Updated(updatedToken);

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            devices = state->devices;
        }
    } catch (...) {
        return {};
    }

    std::sort(devices.begin(), devices.end(), [](const WinBleDeviceInfo& a, const WinBleDeviceInfo& b) {
        const bool exactA = a.name == L"BJ_LED" || a.name == L"BJ_LED_M";
        const bool exactB = b.name == L"BJ_LED" || b.name == L"BJ_LED_M";
        if (exactA != exactB) return exactA;
        return a.rssi > b.rssi;
    });
    return devices;
}

WinBleLed::~WinBleLed() {
    close();
}

void WinBleLed::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeUnlocked();
}

void WinBleLed::closeUnlocked() {
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
    std::lock_guard<std::mutex> lock(mutex_);
    closeUnlocked();
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
        closeUnlocked();
        return false;
    } catch (...) {
        closeUnlocked();
        return false;
    }
    closeUnlocked();
    return false;
}

bool WinBleLed::connectKnownGattDevice() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connectKnownGattDeviceUnlocked();
}

bool WinBleLed::connectKnownGattDeviceUnlocked() {
    closeUnlocked();
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
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

void WinBleLed::write(bj::RGB color, int maxChannel) {
    std::lock_guard<std::mutex> lock(mutex_);
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
        } catch (...) {
            ready_ = false;
        }
        return;
    }

    if (device_ == INVALID_HANDLE_VALUE) return;
    std::vector<unsigned char> buffer(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + packet.size() - 1);
    auto* value = reinterpret_cast<BTH_LE_GATT_CHARACTERISTIC_VALUE*>(buffer.data());
    value->DataSize = ULONG(packet.size());
    std::copy(packet.begin(), packet.end(), value->Data);
    const HRESULT hr = BluetoothGATTSetCharacteristicValue(device_, &characteristic_->value, value, 0, BLUETOOTH_GATT_FLAG_NONE);
    if (FAILED(hr)) ready_ = false;
}

std::vector<WinBleDeviceInfo> WinBleLed::scan(int timeoutMs, int limit) {
    auto devices = scanWinrtDevices(timeoutMs, limit);
    if (!devices.empty()) return devices;
    return scanKnownGattDevices(std::max(1, limit));
}

std::vector<WinBleDeviceInfo> WinBleLed::scanInProcess(int timeoutMs, int limit) {
    return scan(timeoutMs, limit);
}
