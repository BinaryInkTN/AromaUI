> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Notifications post to the system tray through `NotificationManager`. On API 26 and newer every notification belongs to a channel. Create the channel once, then post any number of notifications into it.

## Setup first

Posting needs nothing on older releases. On API 33 and newer the app must hold `android.permission.POST_NOTIFICATIONS`, requested at runtime with [Permissions](Permissions.md). The template manifest already declares it. Use `aroma_android_notify_enabled` to check whether the user muted the app.

## Importance levels

| Constant | Value | Meaning |
|---|---|---|
| `AROMA_NOTIFY_IMPORTANCE_NONE` | 0 | Channel exists but stays silent. |
| `AROMA_NOTIFY_IMPORTANCE_MIN` | 1 | No sound, no heads-up. |
| `AROMA_NOTIFY_IMPORTANCE_LOW` | 2 | No sound. |
| `AROMA_NOTIFY_IMPORTANCE_DEFAULT` | 3 | Sound and heads-up. |
| `AROMA_NOTIFY_IMPORTANCE_HIGH` | 4 | Urgent sound and heads-up. |

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_notify_channel` | `void aroma_android_notify_channel(int id, const char* name, const char* desc, int importance)` | Creates or updates a channel. Channel ids are namespaced per app as `aroma-channel-<id>`. Safe to call on every launch. |
| `aroma_android_notify_show` | `void aroma_android_notify_show(int id, const char* channel, const char* title, const char* text)` | Posts a notification. Pass `NULL` or `""` as channel to auto-create a default channel for that id. Reusing an id replaces the previous notification. |
| `aroma_android_notify_cancel` | `void aroma_android_notify_cancel(int id)` | Removes one notification. |
| `aroma_android_notify_cancel_all` | `void aroma_android_notify_cancel_all(void)` | Removes every notification from this app. |
| `aroma_android_notify_enabled` | `bool aroma_android_notify_enabled(void)` | Returns true when the app may post notifications. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

void download_done(void) {
    if (!aroma_android_notify_enabled()) {
        return;
    }
    aroma_android_notify_channel(1, "Downloads", "Finished downloads", AROMA_NOTIFY_IMPORTANCE_DEFAULT);
    aroma_android_notify_show(1, NULL, "Download finished", "Update package is ready to install.");
}
#endif
```

**Sources:** [include/aroma_android.h835-839](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L835-L839) [include/aroma_android.h874-908](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L874-L908)

## Next

* Read [Permissions](Permissions.md) for requesting `POST_NOTIFICATIONS` on API 33+.
* Read [System UI](System-UI.md) for toast and vibration feedback.
