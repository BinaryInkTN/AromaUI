> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

NFC reads NDEF text tags through foreground dispatch. Start dispatch in `onResume` and stop it in `onPause`; `AromaActivity` already wires both, plus tag intents arriving while the app runs. The payload callback receives decoded text from the first text record.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_nfc_register` | `void aroma_android_nfc_register(void (*cb)(const char* payload))` | Sets the callback that receives tag text. |
| `aroma_android_nfc_start` | `void aroma_android_nfc_start(void)` | Arms foreground dispatch for NDEF, tech, and tag intents. Safe to call every resume. No return value. |
| `aroma_android_nfc_stop` | `void aroma_android_nfc_stop(void)` | Disarms foreground dispatch. Safe to call every pause. No return value. |
| `aroma_android_nfc_available` | `bool aroma_android_nfc_available(void)` | Returns true when the device has NFC hardware. |
| `aroma_android_nfc_enabled` | `bool aroma_android_nfc_enabled(void)` | Returns true when NFC is switched on. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>
#include <string.h>

static char tag_text[256];

static void on_tag(const char* payload) {
    if (payload) {
        strncpy(tag_text, payload, sizeof(tag_text) - 1);
    }
}

void scan_setup(void) {
    if (!aroma_android_nfc_available()) {
        return;
    }
    aroma_android_nfc_register(on_tag);
}
#endif
```

**Sources:** [include/aroma_android.h1242-1288](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1242-L1288) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl2440-2507](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L2440-L2507)

## Next

* Read [App Setup](App-Setup.md) for the `AromaActivity` intent wiring.
* Read [Permissions](Permissions.md) for the NFC declaration.
