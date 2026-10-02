> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Scan first. See [Bluetooth Scan](Bluetooth-Scan.md). Then use the functions below to pair, connect, and send data.

Pair, connect, and send are async. A true return means the request started. Completion arrives through the callbacks registered with `aroma_android_bt_register_callbacks`.

## Bluetooth state

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_is_bluetooth_enabled` | `bool aroma_android_is_bluetooth_enabled()` | Returns true when Bluetooth is on, false if not. |

## Pairing

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_bt_pair` | `bool aroma_android_bt_pair(const char* addr)` | Starts bonding with the address. Backed by `AromaHelper.btPair`. Returns true if bonding started. Result arrives through `pairing_cb`. |
| `aroma_android_bt_unpair` | `bool aroma_android_bt_unpair(const char* addr)` | Removes bonding. Backed by `AromaHelper.btUnpair`. Returns true if the request started. |
| `aroma_android_bt_get_pair_state` | `int aroma_android_bt_get_pair_state(const char* addr)` | Returns one `AROMA_BT_BOND_*` value (10 none, 11 bonding, 12 bonded). Backed by `AromaHelper.btGetPairState`. |

## Connection

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_bt_connect` | `bool aroma_android_bt_connect(const char* addr)` | Connects with the default mode. Backed by `AromaHelper.btConnect`. Returns true if the attempt started. Result arrives through `connection_cb`. |
| `aroma_android_bt_connect_with_mode` | `bool aroma_android_bt_connect_with_mode(const char* addr, int mode)` | Connects with one `AROMA_BT_MODE_*` value (0 data, 1 audio, 2 HID, 3 auto). Backed by `AromaHelper.btConnectWithMode`. |
| `aroma_android_bt_disconnect` | `void aroma_android_bt_disconnect(void)` | Drops the current connection. Backed by `AromaHelper.btDisconnect`. |
| `aroma_android_bt_send` | `int aroma_android_bt_send(const char* data, int len)` | Sends bytes. Returns count sent or -1 on error. Backed by `AromaHelper.btSend`. |

## Status

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_bt_is_connected` | `bool aroma_android_bt_is_connected(void)` | Returns true when connected. Backed by `AromaHelper.btIsConnected`. |
| `aroma_android_bt_get_device_type` | `int aroma_android_bt_get_device_type(void)` | Returns one `AROMA_BT_TYPE_*` value (0 to 11). Returns 0 when unknown. Backed by `AromaHelper.btGetDeviceType`. |
| `aroma_android_bt_get_device_name` | `const char* aroma_android_bt_get_device_name(void)` | Returns the peer name or NULL when missing. Backed by `AromaHelper.btGetDeviceName`. |
| `aroma_android_bt_get_current_mode` | `int aroma_android_bt_get_current_mode(void)` | Returns one `AROMA_BT_MODE_*` value. Returns 3 (auto) when unknown. Backed by `AromaHelper.btGetCurrentMode`. |
| `aroma_android_bt_get_mode_name` | `const char* aroma_android_bt_get_mode_name(void)` | Returns the mode name or NULL when missing. Backed by `AromaHelper.btGetModeName`. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

static void on_pair(bool ok, const char* addr, const char* msg) {
    (void)msg;
    if (ok) {
        aroma_android_bt_connect_with_mode(addr, AROMA_BT_MODE_DATA);
    }
}

static void on_conn(bool ok, const char* addr, int mode, int type) {
    (void)addr; (void)mode; (void)type;
    if (ok) {
        const char hello[] = "hello";
        aroma_android_bt_send(hello, sizeof(hello) - 1);
    }
}

if (!aroma_android_is_bluetooth_enabled()) {
    aroma_android_toast("Turn on Bluetooth first", true);
} else if (aroma_android_bt_get_pair_state("00:11:22:33:44:55") != AROMA_BT_BOND_BONDED) {
    aroma_android_bt_register_callbacks(NULL, NULL, on_pair, on_conn, NULL);
    aroma_android_bt_pair("00:11:22:33:44:55");
} else {
    aroma_android_bt_connect_with_mode("00:11:22:33:44:55", AROMA_BT_MODE_DATA);
}
#endif
```

**Sources:** [include/aroma_android.h189-195](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L189-L195) [include/aroma_android.h260-419](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L260-L419) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl598-1120](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L598-L1120)

## Next

* Read [Bluetooth Scan](Bluetooth-Scan.md) for discovery.
* Read [Internals](Internals.md) for the JNI callback path.
