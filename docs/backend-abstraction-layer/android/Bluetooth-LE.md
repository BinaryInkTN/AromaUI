> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Bluetooth Low Energy uses a different radio path than classic Bluetooth. Classic Bluetooth in [Bluetooth Scan](Bluetooth-Scan.md) and [Bluetooth Connect](Bluetooth-Connect.md) targets headsets, speakers, and serial devices. BLE targets sensors, beacons, wearables, and custom gadgets through GATT services and characteristics.

## Setup first

BLE needs `android.permission.BLUETOOTH_SCAN` and `android.permission.BLUETOOTH_CONNECT` on API 31 and newer, plus `android.permission.ACCESS_FINE_LOCATION` on older releases. Declare them in `AndroidManifest.xml` and request them with [Permissions](Permissions.md). The template manifest already declares them.

## Write types

| Constant | Value | Meaning |
|---|---|---|
| `AROMA_BTLE_WRITE_WITH_RESPONSE` | 2 | Peripheral acknowledges the write. |
| `AROMA_BTLE_WRITE_NO_RESPONSE` | 1 | Fire and forget, faster. |

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_btle_register_callbacks` | `void aroma_android_btle_register_callbacks(void (*device_cb)(const char*, const char*, int), void (*scan_finished_cb)(void), void (*connection_cb)(const char*, int, bool), void (*services_cb)(const char*, const char*), void (*data_cb)(const char*, const char*, const char*, int), void (*write_cb)(const char*, const char*, int))` | Registers all six BLE callbacks. Call once at startup. |
| `aroma_android_btle_scan` | `void aroma_android_btle_scan(int timeout_ms, const char* service_uuids)` | Scans for peripherals for `timeout_ms` milliseconds. Pass a comma separated service UUID list to filter, or `NULL` for everything. Results arrive in `device_cb` as `(address, name, rssi)`. |
| `aroma_android_btle_stop_scan` | `void aroma_android_btle_stop_scan(void)` | Stops an active scan. The finished callback still fires. |
| `aroma_android_btle_connect` | `void aroma_android_btle_connect(const char* addr)` | Connects with transport LE. The result arrives in `connection_cb` as `(address, status, connected)`. Status 0 means GATT success. |
| `aroma_android_btle_disconnect` | `void aroma_android_btle_disconnect(void)` | Disconnects and releases the GATT client. |
| `aroma_android_btle_is_connected` | `bool aroma_android_btle_is_connected(void)` | Returns true while a GATT link is open. |
| `aroma_android_btle_discover` | `bool aroma_android_btle_discover(void)` | Starts service discovery on the open link. Found services arrive in `services_cb` as one comma separated UUID string. |
| `aroma_android_btle_read` | `bool aroma_android_btle_read(const char* service_uuid, const char* char_uuid)` | Reads a characteristic. The value arrives in `data_cb` as `(address, char_uuid, data, len)`. |
| `aroma_android_btle_write` | `bool aroma_android_btle_write(const char* service_uuid, const char* char_uuid, const char* data, int len, int write_type)` | Writes bytes with `AROMA_BTLE_WRITE_WITH_RESPONSE` or `AROMA_BTLE_WRITE_NO_RESPONSE`. The outcome arrives in `write_cb` as `(address, char_uuid, status)`. |
| `aroma_android_btle_notify` | `bool aroma_android_btle_notify(const char* service_uuid, const char* char_uuid, bool enable)` | Subscribes or unsubscribes from notifications, including the CCCD descriptor write. Updates stream into `data_cb`. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>
#include <string.h>

static void on_ble_conn(const char* addr, int status, bool connected) {
    if (connected && status == 0) {
        aroma_android_btle_discover();
    }
}

static void on_ble_services(const char* addr, const char* csv) {
    (void)addr;
    (void)csv;
    aroma_android_btle_notify("0000180d-0000-1000-8000-00805f9b34fb",
        "00002a37-0000-1000-8000-00805f9b34fb", true);
}

void start_heart_rate(const char* addr) {
    aroma_android_btle_register_callbacks(NULL, NULL, on_ble_conn, on_ble_services, NULL, NULL);
    aroma_android_btle_connect(addr);
}
#endif
```

**Sources:** [include/aroma_android.h826-827](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L826-L827) [include/aroma_android.h1162-1240](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1162-L1240)

## Next

* Read [Bluetooth Scan](Bluetooth-Scan.md) for classic device discovery.
* Read [Permissions](Permissions.md) for the scan and connect permissions.
