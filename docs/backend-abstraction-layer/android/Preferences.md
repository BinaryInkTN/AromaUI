> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`. Values are backed by `AromaHelper` SharedPreferences helpers.

Use these for small settings like theme flags and counters. Use storage paths in [Device APIs](Device-APIs.md) for files.

## Functions

| Functions | Signatures | What they do |
|---|---|---|
| String | `const char* aroma_android_get_preference_string(const char* key, const char* def)` and `void aroma_android_set_preference_string(const char* key, const char* value)` | Read and write a string. The getter returns `def` when the key is missing. |
| Int | `int aroma_android_get_preference_int(const char* key, int def)` and `void aroma_android_set_preference_int(const char* key, int value)` | Read and write an int. The getter returns `def` when the key is missing. |
| Float | `float aroma_android_get_preference_float(const char* key, float def)` and `void aroma_android_set_preference_float(const char* key, float value)` | Read and write a float. The getter returns `def` when the key is missing. |
| Bool | `bool aroma_android_get_preference_bool(const char* key, bool def)` and `void aroma_android_set_preference_bool(const char* key, bool value)` | Read and write a bool. The getter returns `def` when the key is missing. |
| Long | `long aroma_android_get_preference_long(const char* key, long def)` and `void aroma_android_set_preference_long(const char* key, long value)` | Read and write a long. The getter returns `def` when the key is missing. |

Setters write at once.

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

aroma_android_set_preference_string("theme", "dark");
const char* theme = aroma_android_get_preference_string("theme", "light");

int count = aroma_android_get_preference_int("launch_count", 0);
aroma_android_set_preference_int("launch_count", count + 1);

bool first = aroma_android_get_preference_bool("first_run", true);
if (first) {
    aroma_android_set_preference_bool("first_run", false);
}
#endif
```

**Sources:** [include/aroma_android.h750-826](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L750-L826) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl1413-1463](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L1413-1463)

## Next

* Read [Device APIs](Device-APIs.md) for file storage paths.
* Read [System UI](System-UI.md) for user visible feedback.
