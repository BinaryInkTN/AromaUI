> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

## Battery and WiFi

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_battery_level` | `int aroma_android_get_battery_level()` | Returns battery percent from 0 to 100. Returns -1 if unavailable. |
| `aroma_android_is_wifi_enabled` | `bool aroma_android_is_wifi_enabled()` | Returns true when WiFi is on, false if not. |
| `aroma_android_set_wifi_enabled` | `void aroma_android_set_wifi_enabled(bool enabled)` | Turns WiFi on or off. New Android versions may block this. No return value. |

```c
int level = aroma_android_get_battery_level();
if (level >= 0 && level < 20) {
    aroma_android_toast("Low battery", true);
}
```

## Camera and gallery

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_launch_camera` | `void aroma_android_launch_camera()` | Launches the camera app with an intent. Needs the camera permission. |
| `aroma_android_launch_gallery` | `void aroma_android_launch_gallery()` | Launches the gallery app with an intent. |
| `android_open_url` | `android_open_url(const char* url)` | Backend only. Opens a URL through `android_send_intent`. Wired to the `open_url` slot. |
| `android_send_intent` | `android_send_intent(...)` | Backend only. Low level intent dispatcher for URL, camera, and gallery paths. |

Request camera and storage permissions first. See [Permissions](Permissions.md). You do not call the two `android_*` backend functions directly. Call the `aroma_android_*` wrappers or the platform interface slots.

## Storage and services

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_system_service` | `jobject aroma_android_get_system_service(const char* service_name)` | Returns the named Android system service as a `jobject`, or NULL when missing. |
| `aroma_android_get_internal_path` | `const char* aroma_android_get_internal_path()` | Returns the app internal storage directory, or NULL when missing. Always available on stock Android. Use for private files. |
| `aroma_android_get_external_path` | `const char* aroma_android_get_external_path()` | Returns the app external storage directory, or NULL when missing. May be NULL when no shared storage is mounted. |

Check the returned path for NULL before you use it.

```c
const char* dir = aroma_android_get_internal_path();
if (dir) {
    // Save app files under dir.
}
```

**Sources:** [include/aroma_android.h146-183](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L146-L183) [include/aroma_android.h422-481](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L422-L481) [src/backends/platforms/aroma_platform_android.c832-987](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L832-L987)

## Next

* Read [Display](Display.md) for density and screen size.
* Read [Preferences](Preferences.md) to persist small values.
