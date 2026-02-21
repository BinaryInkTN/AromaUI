<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

This section exposes low‑level access to the Android runtime. These APIs are intended for advanced use cases such as custom JNI bridging, third‑party SDK integration, or manual thread attachment.

All functions are available only when compiling with `__ANDROID__` defined.

## `JNIEnv* aroma_android_get_env();`

Returns the `JNIEnv` for the current thread.

### Returns

* Pointer to `JNIEnv`
* `NULL` if the thread is not attached to the JVM

### Example

```c
JNIEnv* env = aroma_android_get_env();
if (!env) {
    // Thread not attached
    return;
}
```

### Notes

* Each native thread must be attached to the JVM before using JNI.
* Do not store `JNIEnv*` globally across threads.


## `jobject aroma_android_get_activity();`

Returns a global reference to the active Android `Activity`.

### Example

```c
jobject activity = aroma_android_get_activity();
if (activity) {
    // Use activity in JNI calls
}
```

### Notes

* Managed internally by the platform layer.
* Do not delete this reference.


## `JavaVM* aroma_android_get_jvm();`

Returns the Java VM instance.

### Example

```c
JavaVM* jvm = aroma_android_get_jvm();
if (jvm) {
    // Attach worker threads if needed
}
```

### Notes

* Useful for background threads requiring JNI access.
* Not required for standard AromaUI usage.
