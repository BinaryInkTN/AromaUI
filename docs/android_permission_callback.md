# Android Permission Callback Integration

## Summary
This project now supports asynchronous Android permission requests with callback handling from Java to native C code. This is essential for NativeActivity-based apps that cannot override `onRequestPermissionsResult` in Java directly.

## How it works
- Java: `AromaHelper.handlePermissionResult` should be called from your Activity's `onRequestPermissionsResult`.
- JNI: The native method `onPermissionResult` is registered and will invoke the C callback set by `aroma_android_set_permission_callback`.
- C: Use `aroma_android_request_permission_with_callback` to request permissions and handle the result in your callback.

## Integration Steps
1. **NativeActivity Limitation**: If you use `android.app.NativeActivity`, you cannot override `onRequestPermissionsResult` in Java. You must subclass `NativeActivity` in Java, override `onRequestPermissionsResult`, and call `AromaHelper.handlePermissionResult` for each permission result.

2. **Example Java Activity**
```java
public class MyNativeActivity extends android.app.NativeActivity {
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        for (int i = 0; i < permissions.length; i++) {
            boolean granted = grantResults[i] == android.content.pm.PackageManager.PERMISSION_GRANTED;
            AromaHelper.handlePermissionResult(permissions[i], granted);
        }
    }
}
```

3. **AndroidManifest.xml**
```xml
<activity android:name=".MyNativeActivity" ... >
    ...
</activity>
```

4. **C Usage Example**
```c
void permission_callback(const char* permission, bool granted, void* userdata) {
    if (granted) {
        // Permission granted, proceed
    } else {
        // Permission denied, handle error
    }
}
aroma_android_request_permission_with_callback("android.permission.BLUETOOTH_CONNECT", permission_callback, NULL);
```

## Notes
- Only one callback is stored at a time (last request wins). If you need to handle multiple permissions, coordinate your logic accordingly.
- This pattern is required for all runtime permissions on Android 6.0+.
