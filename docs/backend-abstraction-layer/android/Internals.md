> Android specific. This page only applies to Android builds. You do not call these directly. They implement the `AromaPlatformInterface` slots for Android.

Read this page when you debug the backend or change the Java bridge.

## Backend

Each row maps to code in `aroma_platform_android.c`.

* Lifecycle: `initialize` and `shutdown` set up and tear down EGL, the JNI bridge, and `AromaHelper`. `handle_cmd` handles `APP_CMD_*` events for surface, focus, memory, and destroy.
* VSYNC: `choreographer_callback` and `request_frame` sync frames with the display through `AChoreographer`. No polling.
* Display: `init_display`, `term_display`, `update_surface_size`, `create_window`, `make_context_current`, `swap_buffers`, `get_window_size`.
* Input: `handle_input` turns `AInputEvent` values into `AromaEvent` values.
* Keyboard: `android_show_keyboard` and `android_hide_keyboard` fill the `show_keyboard` and `hide_keyboard` slots.
* Event loop: `run_event_loop` polls the native looper and dispatches sources.
* Table: `aroma_platform_android` wires each `android_*` slot to its `impl_*` or `android_*` function. Slots cover open URL, intents, orientation, density, preferences, Bluetooth, toast, vibrate, permissions, camera, gallery, storage, and keyboard.

**Sources:** [src/backends/platforms/aroma_platform_android.c86-184](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L86-L184) [src/backends/platforms/aroma_platform_android.c1036-1551](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L1036-L1551) [src/backends/platforms/aroma_platform_android.c3170-3258](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L3170-L3258)

## Java bridge

`AromaHelper` is copied from the `.tpl` template into each app with the app package applied. Call `init` once before any other call.

| Member | What it does |
|---|---|
| `init(Context)` | One time setup. Required first. |
| `addCallback(cb)` and `removeCallback(cb)` | Add or remove `BluetoothCallback` listeners. |
| `setConnectionMode(int mode)` | Sets the default `MODE_*` for connects. |
| `BluetoothCallback` interface | Defines `onDeviceDiscovered`, `onScanFinished`, `onPairingResult`, `onConnectionResult`, `onDataReceived`, `onConnectionStateChanged`. |
| `NativeCallback` class | Implements the interface with native methods that land in `native_on_*` C functions. |
| `showToast(Activity, msg, longDuration)` | Shows a toast on the UI thread. |
| `startScan`, `stopScan`, `isScanning` | Controls discovery. |
| `btGetPairedDevices`, `btPair`, `btUnpair`, `btGetPairState` | Manages bonding. |
| `btConnect`, `btConnectWithMode`, `btDisconnect`, `btSend`, `btIsConnected` | Connects and moves data. |
| `btGetDeviceType`, `btGetDeviceName`, `btGetCurrentMode`, `btGetModeName` | Reads connection state. |
| `setPref*` and `getPref*` for String, Boolean, Long, Int, Float | Backs the C preference API. |
| `cleanup()` | Tears down receivers, threads, and sockets. |

**Sources:** [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl112-167](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L112-L167) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl1359-1463](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L1359-1463)

## Build

The CLI builds these sources with Gradle through `aroma build android`. It runs them on devices or emulators with `aroma run android --emu`. It scaffolds the Java bridge and manifest with `aroma create`. See [Building and Deployment](../../cli-toolchain/Building-and-Deployment.md) and [Project Creation](../../cli-toolchain/Project-Creation-and-Scaffolding.md).

## Next

* Read [Platform Backends](../Platform-Backends.md) for the cross platform interface.
* Read [Overview](Overview.md) for the full Android page list.
