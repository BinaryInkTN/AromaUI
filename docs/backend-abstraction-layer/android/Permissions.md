> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

## Setup first

Declare each permission in `AndroidManifest.xml` before you check it. Without the manifest entry, checks always fail. Results for requests arrive async, so check again after the user responds.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_check_permission` | `bool aroma_android_check_permission(const char* permission_name)` | Returns true if the permission is granted, false if not. |
| `aroma_android_request_permission` | `void aroma_android_request_permission(const char** permissions, int permCount)` | Requests runtime permissions from the user. Pass an array of strings plus its length. No return value, results arrive async. |

## Which string to use

| Feature | Manifest string |
|---|---|
| Bluetooth scan | `android.permission.BLUETOOTH_SCAN` |
| Bluetooth connect | `android.permission.BLUETOOTH_CONNECT` |
| Camera | `android.permission.CAMERA` |
| Location (Bluetooth scan on older API levels) | `android.permission.ACCESS_FINE_LOCATION` |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

if (!aroma_android_check_permission("android.permission.BLUETOOTH_CONNECT")) {
    const char* perms[] = { "android.permission.BLUETOOTH_CONNECT" };
    aroma_android_request_permission(perms, 1);
    return; // Check again after the user responds.
}
// Permission granted. Start Bluetooth work.
#endif
```

**Sources:** [include/aroma_android.h84-108](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L84-L108)

## Next

* Read [System UI](System-UI.md) for toast and vibration.
* Read [App Setup](App-Setup.md) for manifest placement.
