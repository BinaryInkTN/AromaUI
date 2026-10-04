> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_toast` | `void aroma_android_toast(const char* msg, bool long_duration)` | Shows an Android toast on the UI thread through `AromaHelper.showToast`. Pass true for long duration, false for short. No return value. |
| `aroma_android_open_settings` | `void aroma_android_open_settings()` | Opens the Android system settings screen. No return value. |
| `aroma_android_vibrate` | `void aroma_android_vibrate(int ms)` | Vibrates for `ms` milliseconds. No return value. |
| `aroma_android_vibrate_effect` | `void aroma_android_vibrate_effect(int ms, int amplitude)` | Vibrates with amplitude 1-255 through `VibrationEffect` on API 26+, legacy path below. No return value. |
| `aroma_android_set_clipboard_text` | `void aroma_android_set_clipboard_text(const char* text)` | Copies text to the system clipboard. No return value. |
| `aroma_android_get_clipboard_text` | `const char* aroma_android_get_clipboard_text(void)` | Returns current clipboard text, or empty string. |
| `aroma_android_share_text` | `void aroma_android_share_text(const char* text, const char* title)` | Opens the system share sheet with a chooser titled `title`. No return value. |
| `aroma_android_set_immersive` | `void aroma_android_set_immersive(bool enabled)` | Hides or shows status and navigation bars, sticky immersive on older releases. No return value. |
| `aroma_android_set_keep_screen_on` | `void aroma_android_set_keep_screen_on(bool enabled)` | Keeps the screen on while enabled. No return value. |
| `aroma_android_tts_speak` | `void aroma_android_tts_speak(const char* text)` | Speaks text with the system TTS engine, flushing the queue. No return value. |
| `aroma_android_tts_stop` | `void aroma_android_tts_stop(void)` | Stops speech. No return value. |
| `aroma_android_tts_is_speaking` | `bool aroma_android_tts_is_speaking(void)` | Returns true while speech is playing. |
| `aroma_android_shortcut_add` | `bool aroma_android_shortcut_add(const char* id, const char* short_label, const char* long_label)` | Pins a dynamic launcher shortcut. Returns true when accepted. |
| `aroma_android_shortcut_remove` | `bool aroma_android_shortcut_remove(const char* id)` | Removes a dynamic shortcut. Returns true when accepted. |
| `aroma_android_shortcut_count` | `int aroma_android_shortcut_count(void)` | Counts pinned dynamic shortcuts. |
| `aroma_android_pip_enter` | `bool aroma_android_pip_enter(int w, int h)` | Enters picture-in-picture with aspect `w` by `h`. Needs the manifest flag. Returns true when entered. |
| `aroma_android_pip_available` | `bool aroma_android_pip_available(void)` | Returns true when PiP is supported. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

aroma_android_toast("Saved", false);
aroma_android_toast("Sync finished", true);
aroma_android_vibrate_effect(50, 200);
aroma_android_set_clipboard_text("aroma-ui-1.0");
aroma_android_tts_speak("Export complete");

if (!aroma_android_check_permission("android.permission.BLUETOOTH_SCAN")) {
    aroma_android_open_settings();
}
#endif
```

**Sources:** [include/aroma_android.h111-144](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L111-L144) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl379-412](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L379-L412)

## Next

* Read [Device APIs](Device-APIs.md) for battery and WiFi.
* Read [Preferences](Preferences.md) to store settings.
