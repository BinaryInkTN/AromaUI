> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

RFID reads HF tag UIDs through the NFC reader using foreground dispatch. It shares dispatch wiring with [NFC](NFC.md): starting either arms the reader, and every scanned tag delivers both the NDEF text payload (when present) and the UID hex. UID delivery works for any 13.56 MHz tag, including blank and non-NDEF ones.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_rfid_register` | `void aroma_android_rfid_register(void (*cb)(const char* uid_hex))` | Sets the callback that receives tag UIDs as uppercase hex. |
| `aroma_android_rfid_start` | `void aroma_android_rfid_start(void)` | Arms foreground dispatch for NDEF, tech, and tag intents. Safe to call every resume. No return value. |
| `aroma_android_rfid_stop` | `void aroma_android_rfid_stop(void)` | Disarms foreground dispatch. Safe to call every pause. No return value. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>
#include <string.h>

static char last_uid[32];

static void on_uid(const char* uid_hex) {
    if (uid_hex) {
        strncpy(last_uid, uid_hex, sizeof(last_uid) - 1);
    }
}

void rfid_setup(void) {
    if (!aroma_android_nfc_available()) {
        return;
    }
    aroma_android_rfid_register(on_uid);
    aroma_android_rfid_start();
}

void rfid_teardown(void) {
    aroma_android_rfid_stop();
}
#endif
```

**Sources:** [include/aroma_android.h](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl)

## Next

* Read [NFC](NFC.md) for NDEF text payloads from the same taps.
* Read [Permissions](Permissions.md) for the NFC declaration.
