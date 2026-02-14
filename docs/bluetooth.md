# Bluetooth API – `aroma_android.h`

Provides Bluetooth device enumeration, connection management, and data transmission.

---

## `int aroma_android_bt_get_paired(char out_addrs[][18], char out_names[][248], int max_devices);`

Retrieves paired Bluetooth devices.

### Example

```c
char addrs[8][18];
char names[8][248];
int count = aroma_android_bt_get_paired(addrs, names, 8);
```

### Notes

* Buffers must be preallocated.
* Address format: "XX:XX:XX:XX:XX:XX".

---

## `bool aroma_android_bt_connect(const char* addr);`

Connects to a Bluetooth device.

### Example

```c
if (aroma_android_bt_connect("00:11:22:33:44:55")) {
    aroma_android_toast("Connected", false);
}
```

---

## `void aroma_android_bt_disconnect(void);`

Disconnects current connection.

---

## `int aroma_android_bt_send(const char* data, int len);`

Sends raw data.

### Example

```c
const char* msg = "HELLO";
aroma_android_bt_send(msg, 5);
```

---

## `bool aroma_android_bt_is_connected(void);`

Returns connection status.

### Notes

* Ensure Bluetooth permissions are granted.
* Connection flow may be asynchronous internally.
