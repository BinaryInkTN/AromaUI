> Android specific. This page covers the system back button. The core callback API is cross-platform; this page adds the Android wiring.

## Overview

The system back button (back key, system back gesture, predictive back) is delivered to native code as `AKEYCODE_BACK` and as `Activity.onBackPressed()`. AromaUI funnels both into one place:

1. An `EVENT_TYPE_BACK_PRESS` event dispatched to the event root.
2. The registered `AromaBackCallback` (see `aroma_ui_set_back_callback`).

Return `true` (consume) to stay in the app. Return `false` to let Android perform its default action (finish the activity).

```c
#include <aroma.h>

static bool on_back(void *ud) {
    (void)ud;
    if (app_is_on_detail_page()) {
        app_show_list();
        return true;  // handled: stay in app
    }
    return false;     // at root: let Android exit
}

// once, after aroma_event_set_root(root):
aroma_ui_set_back_callback(on_back, NULL);
```

You can also listen at the event level instead of (or in addition to) the callback:

```c
static bool back_listener(AromaEvent *ev, void *ud) {
    (void)ev; (void)ud;
    // same handling; return true to consume
    return on_back(NULL);
}

aroma_event_subscribe(root->node_id, EVENT_TYPE_BACK_PRESS,
                      back_listener, NULL, 100);
```

`aroma_ui_handle_back_press()` runs listeners first, then the callback (only if no listener consumed). Backends call it for you; call it yourself from a custom on-screen back button to share the same path.

| Function | Signature | What it does |
|---|---|---|
| `aroma_ui_set_back_callback` | `void aroma_ui_set_back_callback(AromaBackCallback cb, void *user_data)` | Registers (or replaces) the back-press callback. Pass `NULL` to clear. |
| `aroma_ui_clear_back_callback` | `void aroma_ui_clear_back_callback(void)` | Removes the registered callback. |
| `aroma_ui_has_back_callback` | `bool aroma_ui_has_back_callback(void)` | Returns true when a callback is registered. |
| `aroma_ui_handle_back_press` | `bool aroma_ui_handle_back_press(void)` | Dispatches `EVENT_TYPE_BACK_PRESS` then runs the callback. Returns true when consumed. |
| `aroma_event_create_back` | `AromaEvent *aroma_event_create_back(uint64_t target_node_id)` | Allocates a back-press event (pass `0` for the event root). |

`AromaBackCallback` is `bool (*)(void *user_data)`.

## Android wiring

* NDK: `handle_input()` in `aroma_platform_android.c` intercepts `AKEYCODE_BACK` on `ACTION_DOWN` and calls `aroma_ui_handle_back_press()`. Consumed presses return `1` and stay in the app. Unconsumed presses call `ANativeActivity_finish()` (NativeActivity does not exit on its own when the native queue reports unhandled) and return `1`. The release (`ACTION_UP`) is swallowed.
* Java: `AromaActivity.onBackPressed()` calls `nativeOnBackPressed()` (registered dynamically onto the app's `AromaActivity` class as `()Z`). When native returns `true` the activity stays; otherwise `super.onBackPressed()` runs. This covers gesture / predictive-back paths that bypass the key event.
* Desktop / web parity: `ESC` on GLFW, GLPS, and Emscripten calls the same `aroma_ui_handle_back_press()`, so one callback works everywhere.

No manifest or permission changes are needed. `AromaActivity` and `AromaHelper` templates already contain the override; new projects from `aroma create` get it automatically.

## Example: catalog drill-down

```c
// DemoApp pattern: detail page -> back returns to list.
bool demo_incense_handle_back(void *ud) {
    (void)ud;
    if (demo_incense_current() != -1) {
        show_list();      // your router pop
        return true;
    }
    return false;         // catalog root: exit app
}

aroma_ui_set_back_callback(demo_incense_handle_back, NULL);
```

Test on device with `adb logcat`: the DemoApp logs `back_press handled: closing <demo>` when it pops, and `back_press unhandled: at catalog root` when it lets Android exit.

## Next

* Read [System UI](System-UI.md) for toast, vibration, and immersive mode.
* Read [Event System](../../core-framework/Event-System.md) for `aroma_event_subscribe()` semantics.
* Read [Layout & Navigation](../../widget-library/Layout-and-Navigation-Widgets.md) for in-app navigation widgets.
