> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_toast` | `void aroma_android_toast(const char* msg, bool long_duration)` | Shows an Android toast on the UI thread through `AromaHelper.showToast`. Pass true for long duration, false for short. No return value. |
| `aroma_android_open_settings` | `void aroma_android_open_settings()` | Opens the Android system settings screen. No return value. |
| `aroma_android_vibrate` | `void aroma_android_vibrate(int ms)` | Vibrates for `ms` milliseconds. No return value. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

aroma_android_toast("Saved", false); // Short toast.
aroma_android_toast("Sync finished", true); // Long toast.
aroma_android_vibrate(50); // 50 ms tap feedback.

// Send the user to system settings when a permission is denied.
if (!aroma_android_check_permission("android.permission.BLUETOOTH_SCAN")) {
    aroma_android_open_settings();
}
#endif
```

**Sources:** [include/aroma_android.h111-144](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L111-L144) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl379-412](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L379-L412)

## Next

* Read [Device APIs](Device-APIs.md) for battery and WiFi.
* Read [Preferences](Preferences.md) to store settings.
