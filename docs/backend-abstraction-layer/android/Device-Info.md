> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Device info getters read `Build`, `PackageManager`, `ActivityManager`, `ConnectivityManager`, `PowerManager`, `BatteryManager`, `TelephonyManager`, `CameraManager`, and the system locale. Every getter is null-safe and returns a default when the service is missing.

## Network types

| Constant | Value | Meaning |
|---|---|---|
| `AROMA_NET_NONE` | 0 | No usable connection. |
| `AROMA_NET_WIFI` | 1 | Connected over Wi-Fi. |
| `AROMA_NET_MOBILE` | 2 | Connected over mobile data. |
| `AROMA_NET_OTHER` | 3 | Connected over another transport. |

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_manufacturer` | `const char* aroma_android_get_manufacturer(void)` | Device manufacturer, for example Google or samsung. |
| `aroma_android_get_model` | `const char* aroma_android_get_model(void)` | Device model, for example Pixel 8. |
| `aroma_android_get_os_version` | `const char* aroma_android_get_os_version(void)` | Android release string, for example 14. |
| `aroma_android_get_sdk_int` | `int aroma_android_get_sdk_int(void)` | API level as an integer, for example 34. |
| `aroma_android_get_package_name` | `const char* aroma_android_get_package_name(void)` | Application package name. |
| `aroma_android_get_version_name` | `const char* aroma_android_get_version_name(void)` | `versionName` from the package manager. |
| `aroma_android_get_version_code` | `long aroma_android_get_version_code(void)` | Version code as a number. |
| `aroma_android_get_install_time` | `long aroma_android_get_install_time(void)` | First install time as Unix millis. |
| `aroma_android_get_memory_avail_mb` | `long aroma_android_get_memory_avail_mb(void)` | Free RAM in megabytes. |
| `aroma_android_is_memory_low` | `bool aroma_android_is_memory_low(void)` | Returns true when the OS flags memory pressure. |
| `aroma_android_is_network_connected` | `bool aroma_android_is_network_connected(void)` | Returns true with a validated internet route. |
| `aroma_android_get_network_type` | `int aroma_android_get_network_type(void)` | One of the `AROMA_NET_*` constants. |
| `aroma_android_is_charging` | `bool aroma_android_is_charging(void)` | Returns true while on charger. |
| `aroma_android_is_interactive` | `bool aroma_android_is_interactive(void)` | Returns true while the screen is on and awake. |
| `aroma_android_get_locale_tag` | `const char* aroma_android_get_locale_tag(void)` | BCP-47 locale tag, for example en-US. |
| `aroma_android_get_timezone_id` | `const char* aroma_android_get_timezone_id(void)` | Timezone id, for example Europe/Berlin. |
| `aroma_android_get_network_operator` | `const char* aroma_android_get_network_operator(void)` | Carrier name, or empty without `android.permission.READ_PHONE_STATE`. |
| `aroma_android_wakelock_acquire` | `void aroma_android_wakelock_acquire(long timeout_ms)` | Holds a partial wake lock. Pass 0 for no timeout. Declare `android.permission.WAKE_LOCK`. |
| `aroma_android_wakelock_release` | `void aroma_android_wakelock_release(void)` | Releases the wake lock. |
| `aroma_android_set_torch_enabled` | `bool aroma_android_set_torch_enabled(bool enabled)` | Switches the flashlight. Needs `android.permission.CAMERA`. Returns true on success. |
| `aroma_android_storage_free_mb` | `long aroma_android_storage_free_mb(void)` | Free internal storage in megabytes. |
| `aroma_android_storage_total_mb` | `long aroma_android_storage_total_mb(void)` | Total internal storage in megabytes. |
| `aroma_android_sysbar_status_px` | `int aroma_android_sysbar_status_px(void)` | Status bar height in pixels, 0 when unknown. |
| `aroma_android_sysbar_nav_px` | `int aroma_android_sysbar_nav_px(void)` | Navigation bar height in pixels, 0 when unknown or gestural. |
| `aroma_android_keyboard_visible` | `bool aroma_android_keyboard_visible(void)` | Returns true while the soft keyboard is visible. |
| `aroma_android_volume_music_get` | `int aroma_android_volume_music_get(void)` | Current music stream volume step. |
| `aroma_android_volume_music_max` | `int aroma_android_volume_music_max(void)` | Music stream maximum step. |
| `aroma_android_volume_music_set` | `void aroma_android_volume_music_set(int level)` | Sets the music stream volume, clamped to the maximum. No return value. |
| `aroma_android_ringer_get` | `int aroma_android_ringer_get(void)` | Ringer mode: 0 silent, 1 vibrate, 2 normal. |
| `aroma_android_app_installed` | `bool aroma_android_app_installed(const char* pkg)` | Returns true when the package is installed and visible. |
| `aroma_android_app_open` | `bool aroma_android_app_open(const char* pkg)` | Launches another app by package name. Returns true when an intent resolved. |
| `aroma_android_ir_available` | `bool aroma_android_ir_available(void)` | Returns true with an infrared emitter. |
| `aroma_android_ir_transmit` | `bool aroma_android_ir_transmit(int freq_hz, const int* pattern, int pattern_len)` | Transmits an IR pattern at `freq_hz`. Returns true when accepted. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>
#include <stdio.h>

void log_device(char* out, int cap) {
    snprintf(out, (size_t)cap, "%s %s api %d net %d batt %d",
        aroma_android_get_manufacturer(),
        aroma_android_get_model(),
        aroma_android_get_sdk_int(),
        aroma_android_get_network_type(),
        aroma_android_is_charging() ? 1 : 0);
}
#endif
```

**Sources:** [include/aroma_android.h831-834](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L831-L834) [include/aroma_android.h939-1110](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L939-L1110) [include/aroma_android.h1422-1511](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1422-L1511)

## Next

* Read [Device APIs](Device-APIs.md) for battery level and WiFi state.
* Read [Display](Display.md) for density and screen metrics.
