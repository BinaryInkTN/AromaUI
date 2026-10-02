> Android specific. This page only applies to Android builds. All constants and functions here need `__ANDROID__`.

The C API forwards to `AromaHelper` over JNI. Results come back through `native_on_*` callbacks in `aroma_platform_android.c`.

## Constants

| Group | Values |
|---|---|
| Scan mode `AROMA_BT_SCAN_MODE_PAIRED`, `_NEW`, `_ALL` | 0, 1, 2. Paired only, new only, or all devices. Matches `AromaHelper.SCAN_MODE_*`. |
| Device type `AROMA_BT_TYPE_UNKNOWN`, `_HEADSET`, `_PHONE`, `_SPEAKER`, `_WEARABLE`, `_KEYBOARD`, `_MOUSE`, `_PRINTER`, `_CAR`, `_MEDICAL`, `_ARDUINO`, `_RASPBERRY` | 0 to 11. Matches `AromaHelper.TYPE_*`. |
| Mode `AROMA_BT_MODE_DATA`, `_AUDIO`, `_HID`, `_AUTO` | 0 to 3. Matches `AromaHelper.MODE_*`. |
| Bond state `AROMA_BT_BOND_NONE`, `_BONDING`, `_BONDED` | 10, 11, 12. |

## Callbacks

Register these before you scan. Each one maps to a Java event. Pass NULL for events you do not need.

| Callback | Signature | Fires when |
|---|---|---|
| `device_cb` | `void (*)(const char* addr, const char* name, int type, int rssi)` | Each device is found. `type` is one `AROMA_BT_TYPE_*` value. |
| `scan_finished_cb` | `void (*)(void)` | Discovery stops. |
| `pairing_cb` | `void (*)(bool ok, const char* addr, const char* msg)` | A pairing attempt completes. |
| `connection_cb` | `void (*)(bool ok, const char* addr, int mode, int type)` | A connection attempt completes. |
| `data_cb` | `void (*)(const char* data, int len)` | Bytes arrive from the peer. |

## Scan functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_bt_register_callbacks` | `void aroma_android_bt_register_callbacks(device_cb, scan_finished_cb, pairing_cb, connection_cb, data_cb)` | Registers the five C callbacks. Fed by `AromaHelper.NativeCallback` through `native_on_device_discovered`, `native_on_scan_finished`, `native_on_pairing_result`, `native_on_connection_result`, `native_on_data_received`. |
| `aroma_android_bt_scan` | `int aroma_android_bt_scan(int scan_mode, void (*callback)(addr, name, type, rssi))` | Starts discovery with one `AROMA_BT_SCAN_MODE_*` value. Backed by `AromaHelper.startScan`. Returns device count when known, else 0. Discovery itself is async. |
| `aroma_android_bt_stop_scan` | `void aroma_android_bt_stop_scan(void)` | Stops discovery. Backed by `AromaHelper.stopScan`. |
| `aroma_android_bt_get_paired` | `int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices)` | Fills your buffers with bonded devices. Each address needs 18 chars, each name up to 248. Returns the count found. Backed by `AromaHelper.btGetPairedDevices`. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

static void on_device(const char* addr, const char* name, int type, int rssi) {
    (void)type; (void)rssi;
    aroma_android_toast(name, false);
}

static void on_done(void) {
    aroma_android_toast("Scan finished", false);
}

static void on_data(const char* data, int len) {
    (void)data; (void)len;
    // Handle incoming bytes.
}

char addrs[16][18];
char names[16][248];
int paired = aroma_android_bt_get_paired(addrs, names, 16);

aroma_android_bt_register_callbacks(on_device, on_done, NULL, NULL, on_data);
aroma_android_bt_scan(AROMA_BT_SCAN_MODE_ALL, on_device);
#endif
```

**Sources:** [include/aroma_android.h26-62](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L26-L62) [include/aroma_android.h197-257](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L197-L257) [src/backends/platforms/aroma_platform_android.c269-350](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L269-L350) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl434-568](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L434-L568)

## Next

* Read [Bluetooth Connect](Bluetooth-Connect.md) to pair and send data.
* Read [Permissions](Permissions.md) to request Bluetooth permissions first.
