<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

Provides access to Android system services and application storage paths.


## `jobject aroma_android_get_system_service(const char* service_name);`

Returns a system service object.

### Parameters

* `service_name` - e.g. "vibrator", "wifi", "bluetooth"

### Example

```c
jobject service = aroma_android_get_system_service("vibrator");
```

### Notes

* Returned object is a JNI reference.
* Service availability depends on device and Android version.


## `const char* aroma_android_get_internal_path();`

Returns the app's internal storage directory.

### Example

```c
const char* path = aroma_android_get_internal_path();
printf("Internal: %s\n", path);
```

### Notes

* Private to the application.
* No special permissions required.


## `const char* aroma_android_get_external_path();`

Returns the app's external storage directory.

### Example

```c
const char* path = aroma_android_get_external_path();
```

### Notes

* May require storage permission depending on API level.
* Availability depends on device storage state.
