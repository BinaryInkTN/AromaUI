# UI Utilities – `aroma_android.h`

Provides lightweight access to common Android UI feedback mechanisms.

---

## `void aroma_android_toast(const char* msg, bool long_duration);`

Displays a Toast message.

### Example

```c
aroma_android_toast("Operation successful", false);
```

### Notes

* Non-blocking UI feedback.
* Avoid excessive usage.

---

## `void aroma_android_open_settings();`

Opens Android system settings.

### Example

```c
aroma_android_open_settings();
```

### Notes

* Useful when users must manually enable permissions.

---

## `void aroma_android_vibrate(int ms);`

Triggers device vibration.

### Example

```c
aroma_android_vibrate(150);
```

### Notes

* Keep vibration short for good UX.
