#include "lib/platform/linux/led/BlueZLed.hpp"

#include "lib/bj_ble.hpp"

#include <dbus/dbus.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
constexpr const char* bluezService = "org.bluez";
constexpr const char* objectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr const char* adapterPath = "/org/bluez/hci0";

struct DbusError {
    DBusError value {};
    DbusError() { dbus_error_init(&value); }
    ~DbusError() {
        if (dbus_error_is_set(&value)) dbus_error_free(&value);
    }
    bool isSet() const { return dbus_error_is_set(&value); }
    const char* message() const { return value.message ? value.message : "unknown D-Bus error"; }
};

DBusConnection* systemBus() {
    DbusError error;
    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error.value);
    if (!connection) std::cerr << "BlueZ D-Bus unavailable: " << error.message() << '\n';
    return connection;
}

DBusMessage* call(DBusConnection* connection, const char* path, const char* interface, const char* method, int timeoutMs = 8000) {
    DBusMessage* message = dbus_message_new_method_call(bluezService, path, interface, method);
    if (!message) return nullptr;
    DbusError error;
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, timeoutMs, &error.value);
    dbus_message_unref(message);
    if (!reply && error.isSet()) std::cerr << "BlueZ call failed " << interface << "." << method << ": " << error.message() << '\n';
    return reply;
}

bool callNoReply(DBusConnection* connection, const char* path, const char* interface, const char* method, int timeoutMs = 8000) {
    DBusMessage* reply = call(connection, path, interface, method, timeoutMs);
    if (!reply) return false;
    dbus_message_unref(reply);
    return true;
}

bool setLeDiscoveryFilter(DBusConnection* connection) {
    DBusMessage* message = dbus_message_new_method_call(bluezService, adapterPath, "org.bluez.Adapter1", "SetDiscoveryFilter");
    if (!message) return false;
    DBusMessageIter root;
    dbus_message_iter_init_append(message, &root);
    DBusMessageIter dict;
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &dict);
    DBusMessageIter entry;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    const char* key = "Transport";
    const char* value = "le";
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    DBusMessageIter variant;
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&root, &dict);

    DbusError error;
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, 3000, &error.value);
    dbus_message_unref(message);
    if (reply) {
        dbus_message_unref(reply);
        return true;
    }
    return false;
}

std::string stringFromVariant(DBusMessageIter* variant) {
    DBusMessageIter child;
    dbus_message_iter_recurse(variant, &child);
    if (dbus_message_iter_get_arg_type(&child) != DBUS_TYPE_STRING) return {};
    const char* value = nullptr;
    dbus_message_iter_get_basic(&child, &value);
    return value ? value : "";
}

int intFromVariant(DBusMessageIter* variant, int fallback = -127) {
    DBusMessageIter child;
    dbus_message_iter_recurse(variant, &child);
    const int type = dbus_message_iter_get_arg_type(&child);
    if (type == DBUS_TYPE_INT16) {
        dbus_int16_t value = 0;
        dbus_message_iter_get_basic(&child, &value);
        return int(value);
    }
    if (type == DBUS_TYPE_INT32) {
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&child, &value);
        return int(value);
    }
    return fallback;
}

bool boolFromVariant(DBusMessageIter* variant, bool fallback = false) {
    DBusMessageIter child;
    dbus_message_iter_recurse(variant, &child);
    if (dbus_message_iter_get_arg_type(&child) != DBUS_TYPE_BOOLEAN) return fallback;
    dbus_bool_t value = false;
    dbus_message_iter_get_basic(&child, &value);
    return value != 0;
}

void parseDeviceProperties(DBusMessageIter* properties, BlueZDeviceInfo& device, bool* servicesResolved = nullptr) {
    while (dbus_message_iter_get_arg_type(properties) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(properties, &entry);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        if (key && std::strcmp(key, "Address") == 0) device.address = stringFromVariant(&entry);
        if (key && std::strcmp(key, "Name") == 0) device.name = stringFromVariant(&entry);
        if (key && std::strcmp(key, "Alias") == 0 && device.name.empty()) device.name = stringFromVariant(&entry);
        if (key && std::strcmp(key, "RSSI") == 0) device.rssi = intFromVariant(&entry);
        if (key && std::strcmp(key, "ServicesResolved") == 0 && servicesResolved) *servicesResolved = boolFromVariant(&entry);
        dbus_message_iter_next(properties);
    }
}

std::vector<BlueZDeviceInfo> managedDevices(DBusConnection* connection, bool onlyBjLed) {
    std::vector<BlueZDeviceInfo> devices;
    DBusMessage* reply = call(connection, "/", objectManagerInterface, "GetManagedObjects", 8000);
    if (!reply) return devices;

    DBusMessageIter root;
    if (!dbus_message_iter_init(reply, &root) || dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return devices;
    }

    DBusMessageIter objects;
    dbus_message_iter_recurse(&root, &objects);
    while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter objectEntry;
        dbus_message_iter_recurse(&objects, &objectEntry);
        const char* objectPath = nullptr;
        dbus_message_iter_get_basic(&objectEntry, &objectPath);
        dbus_message_iter_next(&objectEntry);

        DBusMessageIter interfaces;
        dbus_message_iter_recurse(&objectEntry, &interfaces);
        while (dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter interfaceEntry;
            dbus_message_iter_recurse(&interfaces, &interfaceEntry);
            const char* interfaceName = nullptr;
            dbus_message_iter_get_basic(&interfaceEntry, &interfaceName);
            dbus_message_iter_next(&interfaceEntry);
            if (interfaceName && std::strcmp(interfaceName, "org.bluez.Device1") == 0) {
                BlueZDeviceInfo device;
                device.objectPath = objectPath ? objectPath : "";
                DBusMessageIter properties;
                dbus_message_iter_recurse(&interfaceEntry, &properties);
                parseDeviceProperties(&properties, device);
                if (!device.address.empty() && (!onlyBjLed || bj::ble::isBjLedName(device.name))) devices.push_back(device);
            }
            dbus_message_iter_next(&interfaces);
        }
        dbus_message_iter_next(&objects);
    }
    dbus_message_unref(reply);
    return devices;
}

bool servicesResolved(DBusConnection* connection, const std::string& devicePath) {
    DBusMessage* reply = call(connection, "/", objectManagerInterface, "GetManagedObjects", 8000);
    if (!reply) return false;
    bool resolved = false;
    DBusMessageIter root;
    if (dbus_message_iter_init(reply, &root)) {
        DBusMessageIter objects;
        dbus_message_iter_recurse(&root, &objects);
        while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter objectEntry;
            dbus_message_iter_recurse(&objects, &objectEntry);
            const char* objectPath = nullptr;
            dbus_message_iter_get_basic(&objectEntry, &objectPath);
            dbus_message_iter_next(&objectEntry);
            if (!objectPath || devicePath != objectPath) {
                dbus_message_iter_next(&objects);
                continue;
            }
            DBusMessageIter interfaces;
            dbus_message_iter_recurse(&objectEntry, &interfaces);
            while (dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter interfaceEntry;
                dbus_message_iter_recurse(&interfaces, &interfaceEntry);
                const char* interfaceName = nullptr;
                dbus_message_iter_get_basic(&interfaceEntry, &interfaceName);
                dbus_message_iter_next(&interfaceEntry);
                if (interfaceName && std::strcmp(interfaceName, "org.bluez.Device1") == 0) {
                    DBusMessageIter properties;
                    dbus_message_iter_recurse(&interfaceEntry, &properties);
                    BlueZDeviceInfo unused;
                    parseDeviceProperties(&properties, unused, &resolved);
                }
                dbus_message_iter_next(&interfaces);
            }
            break;
        }
    }
    dbus_message_unref(reply);
    return resolved;
}

std::string findEe01Characteristic(DBusConnection* connection, const std::string& devicePath) {
    DBusMessage* reply = call(connection, "/", objectManagerInterface, "GetManagedObjects", 8000);
    if (!reply) return {};
    std::string found;
    DBusMessageIter root;
    if (dbus_message_iter_init(reply, &root)) {
        DBusMessageIter objects;
        dbus_message_iter_recurse(&root, &objects);
        while (found.empty() && dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter objectEntry;
            dbus_message_iter_recurse(&objects, &objectEntry);
            const char* objectPath = nullptr;
            dbus_message_iter_get_basic(&objectEntry, &objectPath);
            dbus_message_iter_next(&objectEntry);
            const std::string path = objectPath ? objectPath : "";
            if (path.rfind(devicePath, 0) != 0) {
                dbus_message_iter_next(&objects);
                continue;
            }

            DBusMessageIter interfaces;
            dbus_message_iter_recurse(&objectEntry, &interfaces);
            while (found.empty() && dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter interfaceEntry;
                dbus_message_iter_recurse(&interfaces, &interfaceEntry);
                const char* interfaceName = nullptr;
                dbus_message_iter_get_basic(&interfaceEntry, &interfaceName);
                dbus_message_iter_next(&interfaceEntry);
                if (interfaceName && std::strcmp(interfaceName, "org.bluez.GattCharacteristic1") == 0) {
                    DBusMessageIter properties;
                    dbus_message_iter_recurse(&interfaceEntry, &properties);
                    while (dbus_message_iter_get_arg_type(&properties) == DBUS_TYPE_DICT_ENTRY) {
                        DBusMessageIter propertyEntry;
                        dbus_message_iter_recurse(&properties, &propertyEntry);
                        const char* key = nullptr;
                        dbus_message_iter_get_basic(&propertyEntry, &key);
                        dbus_message_iter_next(&propertyEntry);
                        if (key && std::strcmp(key, "UUID") == 0 && bj::ble::isEe01Uuid(stringFromVariant(&propertyEntry))) found = path;
                        dbus_message_iter_next(&properties);
                    }
                }
                dbus_message_iter_next(&interfaces);
            }
            dbus_message_iter_next(&objects);
        }
    }
    dbus_message_unref(reply);
    return found;
}

bool writeGattValue(DBusConnection* connection, const std::string& characteristicPath, const std::array<uint8_t, 8>& packet, const char* type) {
    DBusMessage* message = dbus_message_new_method_call(bluezService, characteristicPath.c_str(), "org.bluez.GattCharacteristic1", "WriteValue");
    if (!message) return false;
    DBusMessageIter root;
    dbus_message_iter_init_append(message, &root);
    DBusMessageIter bytes;
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &bytes);
    for (uint8_t byte : packet) dbus_message_iter_append_basic(&bytes, DBUS_TYPE_BYTE, &byte);
    dbus_message_iter_close_container(&root, &bytes);

    DBusMessageIter dict;
    dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &dict);
    DBusMessageIter entry;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    const char* key = "type";
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    DBusMessageIter variant;
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &type);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&root, &dict);

    DbusError error;
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, 4000, &error.value);
    dbus_message_unref(message);
    if (reply) {
        dbus_message_unref(reply);
        return true;
    }
    return false;
}
}  // namespace

std::vector<BlueZDeviceInfo> BlueZLed::scan(int timeoutMs, int limit) {
    DBusConnection* connection = systemBus();
    if (!connection) return {};
    setLeDiscoveryFilter(connection);
    callNoReply(connection, adapterPath, "org.bluez.Adapter1", "StartDiscovery", 4000);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(500, timeoutMs)));
    callNoReply(connection, adapterPath, "org.bluez.Adapter1", "StopDiscovery", 4000);
    auto devices = managedDevices(connection, true);
    std::sort(devices.begin(), devices.end(), [](const BlueZDeviceInfo& a, const BlueZDeviceInfo& b) {
        const bool exactA = a.name == "BJ_LED" || a.name == "BJ_LED_M";
        const bool exactB = b.name == "BJ_LED" || b.name == "BJ_LED_M";
        if (exactA != exactB) return exactA;
        return a.rssi > b.rssi;
    });
    if (int(devices.size()) > limit) devices.resize(size_t(limit));
    return devices;
}

bool BlueZLed::connect(const BlueZDeviceInfo& device) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectUnlocked();
    address_ = device.address;
    objectPath_ = device.objectPath;
    if (objectPath_.empty()) {
        DBusConnection* connection = systemBus();
        if (!connection) return false;
        auto devices = managedDevices(connection, false);
        auto match = std::find_if(devices.begin(), devices.end(), [&](const BlueZDeviceInfo& candidate) {
            return candidate.address == address_;
        });
        if (match == devices.end()) return false;
        objectPath_ = match->objectPath;
    }
    return resolveCharacteristicUnlocked();
}

bool BlueZLed::connect(const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectUnlocked();
    address_ = address;
    DBusConnection* connection = systemBus();
    if (!connection) return false;
    auto devices = managedDevices(connection, false);
    auto match = std::find_if(devices.begin(), devices.end(), [&](const BlueZDeviceInfo& device) {
        return device.address == address;
    });
    if (match == devices.end()) return false;
    objectPath_ = match->objectPath;
    return resolveCharacteristicUnlocked();
}

bool BlueZLed::resolveCharacteristic() {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolveCharacteristicUnlocked();
}

bool BlueZLed::resolveCharacteristicUnlocked() {
    ready_ = false;
    DBusConnection* connection = systemBus();
    if (!connection || objectPath_.empty()) return false;

    callNoReply(connection, objectPath_.c_str(), "org.bluez.Device1", "Connect", 10000);
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (servicesResolved(connection, objectPath_)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    characteristicPath_ = findEe01Characteristic(connection, objectPath_);
    ready_ = !characteristicPath_.empty();
    if (!ready_) std::cerr << "BlueZ EE01 characteristic not found for " << address_ << '\n';
    return ready_;
}

void BlueZLed::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectUnlocked();
}

void BlueZLed::disconnectUnlocked() {
    ready_ = false;
    characteristicPath_.clear();
}

bool BlueZLed::isReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && !characteristicPath_.empty();
}

void BlueZLed::write(bj::RGB color, int maxChannel) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || characteristicPath_.empty()) return;
    DBusConnection* connection = systemBus();
    if (!connection) return;
    const auto packet = bj::colorPacket(color, maxChannel);
    if (!writeGattValue(connection, characteristicPath_, packet, "request")) {
        if (!writeGattValue(connection, characteristicPath_, packet, "command")) {
            ready_ = false;
            std::cerr << "BlueZ write failed for " << address_ << '\n';
        }
    }
}
