# Camera & Gallery – `aroma_android.h`

Provides access to Android media intents for capturing and selecting images.

---

## `void aroma_android_launch_camera();`

Launches the default camera application.

### Example

```c
aroma_android_launch_camera();
```

### Notes

* Requires camera permission.
* Result must be handled on the Java side.

---

## `void aroma_android_launch_gallery();`

Launches image picker.

### Example

```c
aroma_android_launch_gallery();
```

### Notes

* Used for selecting existing media.
* Result is delivered asynchronously.
