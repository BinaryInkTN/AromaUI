
# Bluetooth API (Classic Only)

Supports device scanning, pairing, RFCOMM data communication, and connection state management.

---

## Bluetooth Scan Modes

| Constant                    | Description                    |
| --------------------------- | ------------------------------ |
| `AROMA_BT_SCAN_MODE_PAIRED` | Scan paired devices only       |
| `AROMA_BT_SCAN_MODE_NEW`    | Scan new unpaired devices only |
| `AROMA_BT_SCAN_MODE_ALL`    | Scan all devices               |

---

## Bluetooth Device Types

| Constant                  | Description         |
| ------------------------- | ------------------- |
| `AROMA_BT_TYPE_UNKNOWN`   | Unknown device type |
| `AROMA_BT_TYPE_HEADSET`   | Headset device      |
| `AROMA_BT_TYPE_PHONE`     | Phone device        |
| `AROMA_BT_TYPE_SPEAKER`   | Speaker device      |
| `AROMA_BT_TYPE_WEARABLE`  | Wearable device     |
| `AROMA_BT_TYPE_KEYBOARD`  | Keyboard device     |
| `AROMA_BT_TYPE_MOUSE`     | Mouse device        |
| `AROMA_BT_TYPE_PRINTER`   | Printer device      |
| `AROMA_BT_TYPE_CAR`       | Car device          |
| `AROMA_BT_TYPE_MEDICAL`   | Medical device      |
| `AROMA_BT_TYPE_ARDUINO`   | Arduino device      |
| `AROMA_BT_TYPE_RASPBERRY` | Raspberry Pi device |

---

## Bluetooth Bond States

| Constant                | Description         |
| ----------------------- | ------------------- |
| `AROMA_BT_BOND_NONE`    | Not bonded          |
| `AROMA_BT_BOND_BONDING` | Bonding in progress |
| `AROMA_BT_BOND_BONDED`  | Bonded              |

---

## Bluetooth Connection Modes

| Constant              | Description                 |
| --------------------- | --------------------------- |
| `AROMA_BT_MODE_DATA`  | RFCOMM data mode            |
| `AROMA_BT_MODE_AUDIO` | Audio mode                  |
| `AROMA_BT_MODE_HID`   | HID mode                    |
| `AROMA_BT_MODE_AUTO`  | Automatically selected mode |

---

# Device Discovery

## `int aroma_android_bt_scan(int scan_mode, void (*callback)(const char* addr, const char* name, int type, int rssi));`

Starts Bluetooth Classic device scanning.

### Parameters

* `scan_mode` One of the AROMA_BT_SCAN_MODE constants
* `callback` Invoked for each discovered device

### Callback Signature

```
void callback(const char* addr, const char* name, int type, int rssi);
```

### Notes

* Scanning may be asynchronous internally
* Requires Bluetooth and location permissions on modern Android versions

---

## `void aroma_android_bt_stop_scan(void);`

Stops an ongoing scan.

---

# Paired Devices

## `int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices);`

Retrieves paired Bluetooth devices.

### Example

```
char addrs[8][18];
char names[8][248];
int count = aroma_android_bt_get_paired(addrs, names, 8);
```

### Notes

* Buffers must be preallocated
* Address format is "XX:XX:XX:XX:XX:XX"

---

# Pairing Management

## `bool aroma_android_bt_pair(const char* addr);`

Initiates pairing with a device.

## `bool aroma_android_bt_unpair(const char* addr);`

Removes pairing with a device.

## `int aroma_android_bt_get_pair_state(const char* addr);`

Returns one of the AROMA_BT_BOND constants.

---

# Connection Management

## `bool aroma_android_bt_connect(const char* addr);`

Connects using default mode.

## `bool aroma_android_bt_connect_with_mode(const char* addr, int mode);`

Connects using a specific mode.

## `void aroma_android_bt_disconnect(void);`

Disconnects the active connection.

## `bool aroma_android_bt_is_connected(void);`

Returns connection status.

---

# Data Transmission

## `int aroma_android_bt_send(const char* data, int len);`

Sends raw bytes over RFCOMM.

### Example

```
const char* msg = "HELLO";
aroma_android_bt_send(msg, 5);
```

### Notes

* Returns number of bytes sent
* Returns -1 on error

---

# Connected Device Information

## `int aroma_android_bt_get_device_type(void);`

Returns device type constant.

## `const char* aroma_android_bt_get_device_name(void);`

Returns connected device name or NULL.

## `int aroma_android_bt_get_current_mode(void);`

Returns current connection mode.

## `const char* aroma_android_bt_get_mode_name(void);`

Returns human readable mode name.

---

# Important Limitations

* Bluetooth Classic only
* No BLE support yet
* No GATT support
* Connection flow may be asynchronous internally
* Proper Android runtime permissions are required
