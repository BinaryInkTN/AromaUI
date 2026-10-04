> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

## Entry point

Every Android app starts in `android_main`. Register the native app state, then run the same `main` used on other platforms. Do not rename the `aroma_app` library.

```c
#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
#endif
```

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_set_app` | `void aroma_android_set_app(struct android_app *state)` | Registers NativeActivity state so JNI can reach the Activity. Call it from `android_main` before `main`. Declared in `aroma_ui.h` under `#ifdef __ANDROID__`. |
| `android_main` | `void android_main(struct android_app *state)` | OS entry point. Provided by the template. Forwards to `aroma_android_set_app` then `main`. |

## Manifest

The generated `AndroidManifest.xml` declares one `AromaActivity` with `android.app.lib_name` set to `aroma_app`. It sets `configChanges` to `orientation|keyboardHidden|screenSize|density|screenLayout|smallestScreenSize`.

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.CAMERA" />
```

Add each runtime permission next to the other `uses-permission` entries. Without the manifest entry, permission checks always report denied. See [Permissions](Permissions.md) for the exact strings each feature needs.

## AromaActivity

The generated manifest declares `.AromaActivity`, a thin `NativeActivity` subclass in your app package. It forwards `onRequestPermissionsResult` and `onActivityResult` into native code, which powers permission result callbacks, the image picker, photo capture, and the document picker. Plain `NativeActivity` cannot receive those results, so keep this class.

```xml
<activity android:name=".AromaActivity"
          android:label="{{PROJECT_NAME}}"
          android:exported="true"
          android:configChanges="orientation|keyboardHidden|screenSize|density|screenLayout|smallestScreenSize">
```

## JNI handles

Use these to get raw JNI handles. Prefer the typed wrappers on other pages unless you write custom JNI code.

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_env` | `JNIEnv* aroma_android_get_env()` | Returns `JNIEnv*` for the current thread. Attaches the thread if needed. |
| `aroma_android_get_activity` | `jobject aroma_android_get_activity()` | Returns the current `Activity` as a `jobject`. |
| `aroma_android_get_jvm` | `JavaVM* aroma_android_get_jvm()` | Returns the process `JavaVM*`. |

**Sources:** [include/aroma_ui.h1516-1525](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_ui.h#L1516-L1525) [tools/cli/templates/app/main.c.tpl63-71](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/app/main.c.tpl#L63-L71) [tools/cli/templates/android/app/src/main/AndroidManifest.xml](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/AndroidManifest.xml) [include/aroma_android.h65-81](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L65-L81) [src/backends/platforms/aroma_platform_android.c184-218](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L184-L218)

## Next

* Read [Permissions](Permissions.md) to request runtime permissions.
* Read [Internals](Internals.md) to see how the backend uses these handles.
