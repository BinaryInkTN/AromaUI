> Android specific. This page only applies to Android builds. You do not call anything here directly. It maps how the backend connects your app to the OS, which helps when tracking down device-specific behavior.

## Platform Backend

* Lifecycle: the backend sets up and tears down graphics, the Java bridge, and helpers as the activity starts and stops. Surface, focus, memory, and destroy events are handled for you.
* Frames: rendering syncs with the display refresh, with no polling.
* Display: window creation, sizing, orientation, and density just work, including surface changes on rotation.
* Input: touches, keys, and the system back button arrive as framework events.
* Keyboard: the on-screen keyboard shows and hides on request.
* Device features: one backend covers open URL, intents, preferences, Bluetooth classic and BLE, toast, vibrate, permissions, camera, gallery, document picker, notifications, clipboard, sharing, sensors, TTS, device info, storage, and keyboard.
* Results: permission answers and picker results are routed back to your callbacks automatically.

**Sources:** [src/backends/platforms/aroma_platform_android.c86-184](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L86-L184) [src/backends/platforms/aroma_platform_android.c1036-1551](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L1036-L1551) [src/backends/platforms/aroma_platform_android.c3170-3258](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L3170-L3258)

## Java Bridge

`AromaHelper` is copied from the project template into each app with the
app package applied. It is initialized for you at startup.

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
| `AromaActivity` class | `NativeActivity` subclass in the app package. Forwards permission and activity results into native code. Request codes 1001, 2001, 2002, 2003. |
| BLE: `btleStartScan`, `btleStopScan`, `btleConnect`, `btleDisconnect`, `btleIsConnected`, `btleDiscover`, `btleRead`, `btleWrite`, `btleNotify` | Full GATT client. Results land in `onBleDevice`, `onBleScanFinished`, `onBleConnection`, `onBleServices`, `onBleData`, `onBleWrite`. |
| Location, NFC, biometrics, capture: `locStart`, `locStop`, `locAvailable`, `nfcStart`, `nfcStop`, `nfcReadText`, `dispatchNfc`, `bioAvailable`, `bioAuth`, `contactsPick`, `contactsResolve`, `shotCapture` | System services with native sinks `onLocation`, `onNfcTag`, `onBiometric`, `onScreenshot`. |
| Media extras: `recStart`, `recStop`, `scAdd`, `scRemove`, `scCount`, `pipEnter`, `pipAvailable`, `wpSetImage` | Recording, shortcuts, picture-in-picture, wallpaper. |
| Hardware state: `volMusicGet`, `volMusicMax`, `volMusicSet`, `ringerGet`, `storageFreeMb`, `storageTotalMb`, `sysbarStatusPx`, `sysbarNavPx`, `kbVisible`, `appInstalled`, `appOpen`, `irAvailable`, `irTransmit` | Volume, storage, bars, keyboard, apps, infrared. |
| Notifications: `notifyChannel`, `notifyShow`, `notifyCancel`, `notifyCancelAll`, `notifyEnabled` | Tray notifications with channels on API 26+. |
| Sensors: `sensorStart`, `sensorStop`, `sensorAvailable`, `addSensorCallback`, `removeSensorCallback` | Motion and magnetic sensors with a native event sink. |
| Media results: `pickImage`, `capturePhoto`, `openDocument`, `copyUriToCache`, `saveBitmapToCache` | Pickers that copy results into the app cache and return paths. |
| System: `vibrateEffect`, `clipSet`, `clipGet`, `shareText`, `uiImmersive`, `uiKeepScreenOn`, `orientLock`, `orientSet`, `orientUnlock`, `orientLocked` | Feedback, clipboard, sharing, and display control. |
| Info: `devManufacturer`, `devModel`, `devOsVersion`, `devSdkInt`, `appPackage`, `appVersionName`, `appVersionCode`, `appInstallTime`, `memAvailMb`, `memLow`, `netConnected`, `netType`, `battCharging`, `devInteractive`, `localeTag`, `timezoneId`, `netOperator`, `wakeAcquire`, `wakeRelease`, `torchSet` | Device, app, power, network, locale, and torch state. |
| Speech: `ttsSpeak`, `ttsStop`, `ttsIsSpeaking` | System text to speech. |
| `cleanup()` | Tears down receivers, threads, and sockets. |

**Sources:** [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl112-167](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L112-L167) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl1359-1463](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L1359-1463)

## Building

The CLI builds these sources with Gradle through `aroma build android`. It runs them on devices or emulators with `aroma run android --emu`. It scaffolds the Java bridge and manifest with `aroma create`. See [Building and Deployment](../../cli-toolchain/Building-and-Deployment.md) and [Project Creation](../../cli-toolchain/Project-Creation-and-Scaffolding.md).

## Next Steps

* Read [Platform Backends](../Platform-Backends.md) for the cross platform interface.
* Read [Overview](Overview.md) for the full Android page list.
