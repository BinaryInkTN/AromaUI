# Permissions API – `aroma_android.h`

The Permissions API enables runtime permission checks and requests in compliance with modern Android security requirements.

---

## `bool aroma_android_check_permission(const char* permission_name);`

Checks whether a permission has already been granted.

### Parameters

* `permission_name` – e.g. "android.permission.CAMERA"

### Returns

* `true` if granted
* `false` otherwise

### Example

```c
if (!aroma_android_check_permission("android.permission.CAMERA")) {
    aroma_android_request_permission("android.permission.CAMERA");
}
```

### Notes

* Always check before performing restricted operations.
* Required for camera, Bluetooth, storage, and location.

---

## `void aroma_android_request_permission(const char* permission_name);`

Requests a permission from the user.

### Example

```c
aroma_android_request_permission("android.permission.ACCESS_FINE_LOCATION");
```

### Notes

* Asynchronous operation.
* Result must be handled on the Java side.
