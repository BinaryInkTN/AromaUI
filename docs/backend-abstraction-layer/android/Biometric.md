> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Biometric authentication uses the framework `BiometricPrompt` on API 29 and newer, with no extra dependencies. The system renders the prompt and reports success or failure through a callback. Availability returns 0 when biometric auth is ready and 1 otherwise.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_biometric_register` | `void aroma_android_biometric_register(void (*cb)(bool success))` | Sets the callback that receives the auth result. |
| `aroma_android_biometric_available` | `int aroma_android_biometric_available(void)` | Returns 0 when biometric auth can run, 1 when it cannot. |
| `aroma_android_biometric_authenticate` | `void aroma_android_biometric_authenticate(const char* title, const char* subtitle)` | Shows the system prompt. The registered callback fires once with the result. No return value. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

static void on_auth(bool success) {
    if (success) {
        aroma_android_toast("Unlocked", false);
    }
}

void unlock_vault(void) {
    if (aroma_android_biometric_available() != 0) {
        return;
    }
    aroma_android_biometric_register(on_auth);
    aroma_android_biometric_authenticate("Unlock vault", "Confirm it is you");
}
#endif
```

**Sources:** [include/aroma_android.h1290-1310](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1290-L1310) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl2509-2578](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L2509-L2578)

## Next

* Read [Permissions](Permissions.md) for the `USE_BIOMETRIC` declaration.
* Read [System UI](System-UI.md) for feedback after unlock.
