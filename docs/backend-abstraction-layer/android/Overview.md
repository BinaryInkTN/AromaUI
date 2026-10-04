> Android specific. This section only applies to Android builds. All C APIs here need `__ANDROID__`. On Linux, Web, or embedded targets these symbols do not exist.

Android support lives in three files. They call each other across JNI.

| Layer | File | What it does |
|---|---|---|
| C API | `include/aroma_android.h` | Inline wrappers your app calls |
| Backend | `src/backends/platforms/aroma_platform_android.c` | NativeActivity lifecycle, EGL display, input, JNI bridge |
| Java bridge | `tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl` | AromaHelper class copied into each generated app |
| Java activity | `tools/cli/templates/android/app/src/main/java/AromaActivity.java.tpl` | NativeActivity subclass that forwards permission and activity results |

```mermaid
flowchart LR
    APP["App C code\naroma_android_*()"] --> HDR["aroma_android.h"]
    HDR --> IFACE["AromaPlatformInterface\nandroid_* slots"]
    IFACE --> BACK["aroma_platform_android.c"]
    BACK <-->|"JNI"| JAVA["AromaHelper.java"]
    JAVA --> ANDROID["Android OS services"]
```

**Sources:** [include/aroma_android.h1-20](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1-L20) [src/backends/platforms/aroma_platform_android.c5029-5168](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L5029-L5168)

## Pages in this section

Read them in order. Each page covers one topic. The header range column tells you where each API lives in `aroma_android.h`.

| Page | Topic | Header range |
|---|---|---|
| [App Setup](App-Setup.md) | Entry point, manifest, JNI handles | `aroma_ui.h` + `aroma_android.h` 65-81 |
| [Permissions](Permissions.md) | Check and request runtime permissions | `aroma_android.h` 84-108 |
| [System UI](System-UI.md) | Toast, settings screen, vibration | `aroma_android.h` 111-144 |
| [Bluetooth Scan](Bluetooth-Scan.md) | Scan modes, device types, discovery, callbacks | `aroma_android.h` 26-62, 197-257 |
| [Bluetooth Connect](Bluetooth-Connect.md) | Pair, connect, send data, status | `aroma_android.h` 189-195, 260-419 |
| [Bluetooth LE](Bluetooth-LE.md) | BLE scan, GATT connect, services, notify | `aroma_android.h` 826-827, 1162-1240 |
| [Location](Location.md) | GPS and network fixes, start, stop | `aroma_android.h` 1312-1333 |
| [Biometric](Biometric.md) | System biometric prompt | `aroma_android.h` 1290-1310 |
| [NFC](NFC.md) | Foreground dispatch and NDEF text | `aroma_android.h` 1242-1288 |
| [Notifications](Notifications.md) | Channels and tray notifications | `aroma_android.h` 835-839, 874-908 |
| [Sensors](Sensors.md) | Accelerometer, gyroscope, magnetometer | `aroma_android.h` 828-830, 1133-1160 |
| [Device Info](Device-Info.md) | Device, app, memory, network, power, locale | `aroma_android.h` 831-834, 939-1110 |
| [Device APIs](Device-APIs.md) | Battery, WiFi, camera, gallery, storage, services | `aroma_android.h` 146-183, 422-481 |
| [Display](Display.md) | Density, units, screen size, orientation | `aroma_android.h` 484-749 |
| [Preferences](Preferences.md) | SharedPreferences for settings | `aroma_android.h` 750-826 |
| [Internals](Internals.md) | Backend lifecycle and Java bridge reference | `aroma_platform_android.c` |

## Rule for all pages

Guard Android code with `#ifdef __ANDROID__`. Include `aroma_android.h` only inside that guard. The backend slots for these APIs are stubs on other platforms.

```c
#ifdef __ANDROID__
#include <aroma_android.h>
#endif
```
