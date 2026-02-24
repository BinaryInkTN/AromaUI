<b>Author: AHMED ALI Mohamed Yassine</b>


This guide provides a structured, comprehensive explanation of the AromaUI Screen Orientation Control API for Android.



## API Capabilities

| Feature                   | Description                    |
| - |  |
| Orientation locking       | Prevent screen rotation        |
| Orientation unlocking     | Allow sensor-based rotation    |
| Force portrait            | Lock screen to portrait mode   |
| Force landscape           | Lock screen to landscape mode  |
| Sensor-based rotation     | Enable automatic rotation      |
| Current orientation query | Get device orientation state   |
| Orientation lock status   | Check if orientation is locked |



## Limitations

| Limitation        | Status       |
| -- |  |
| Portrait mode     | Supported    |
| Landscape mode    | Supported    |
| Reverse portrait  | Not directly |
| Reverse landscape | Not directly |
| Sensor-based      | Supported    |
| User-sensor       | Not directly |

> ⚠ All APIs are valid only when compiling with `ANDROID` defined.




# 1. Android Manifest Configuration

To use orientation control, ensure your Android manifest includes appropriate configuration:

```xml
<activity
    android:name=".MainActivity"
    android:configChanges="orientation|screenSize"
    android:screenOrientation="unspecified">
</activity>
```

The `configChanges` attribute allows your app to handle orientation changes manually rather than letting Android recreate the activity.



# 2. Basic Orientation Control

## Lock Current Orientation

Prevent the screen from rotating based on sensor input:

```c
aroma_android_lock_orientation();
```

This locks the screen to whatever orientation the device is currently in (portrait or landscape).



## Unlock Orientation

Allow the screen to auto-rotate based on device sensors:

```c
aroma_android_unlock_orientation();
```

This returns the device to sensor-based rotation behavior.



# 3. Forcing Specific Orientations

## Force Portrait Mode

Lock the screen to portrait orientation regardless of device position:

```c
aroma_android_set_orientation_portrait();
```

This forces the display to portrait mode (taller than wide).



## Force Landscape Mode

Lock the screen to landscape orientation regardless of device position:

```c
aroma_android_set_orientation_landscape();
```

This forces the display to landscape mode (wider than tall).



## Enable Sensor-Based Rotation

Set the orientation to follow device sensors:

```c
aroma_android_set_orientation_sensor();
```

This is equivalent to unlocking orientation but explicitly sets sensor mode.



# 4. Querying Orientation State

## Get Current Orientation

Retrieve the current screen orientation:

```c
int orientation = aroma_android_get_current_orientation();

if (orientation == 1) {
    // Portrait mode
} else if (orientation == 2) {
    // Landscape mode
}
```

### Return Values

| Value | Description           |
| -- |  |
| 1     | Portrait orientation  |
| 2     | Landscape orientation |
| -1    | Unknown/unavailable   |



## Check if Orientation is Locked

Determine if orientation locking is currently active:

```c
bool is_locked = aroma_android_is_orientation_locked();

if (is_locked) {
    // Orientation is locked to a specific mode
} else {
    // Orientation can auto-rotate
}
```



# 5. Complete Usage Example

```c
#include "aroma_android.h"

// Example: Toggle between portrait and landscape
void toggle_orientation(void) {
    int current = aroma_android_get_current_orientation();
    
    if (current == 1) {
        // Currently in portrait, switch to landscape
        aroma_android_set_orientation_landscape();
        printf("Switched to landscape mode\n");
    } else if (current == 2) {
        // Currently in landscape, switch to portrait
        aroma_android_set_orientation_portrait();
        printf("Switched to portrait mode\n");
    }
}

// Example: Lock/unlock orientation
void handle_orientation_lock(bool should_lock) {
    if (should_lock) {
        aroma_android_lock_orientation();
        printf("Orientation locked\n");
    } else {
        aroma_android_unlock_orientation();
        printf("Orientation unlocked\n");
    }
}

// Example: Initialize with desired orientation
void initialize_with_orientation(void) {
    // Force portrait mode at startup
    aroma_android_set_orientation_portrait();
    
    // Later, allow rotation
    // aroma_android_set_orientation_sensor();
}

// Example: Respond to device state
void on_device_rotated(void) {
    if (!aroma_android_is_orientation_locked()) {
        int orientation = aroma_android_get_current_orientation();
        
        switch(orientation) {
            case 1:
                // Adapt UI for portrait
                break;
            case 2:
                // Adapt UI for landscape
                break;
        }
    }
}
```



# 6. Best Practices

## When to Lock Orientation

| Use Case         | Recommendation                |
| - | -- |
| Video playback   | Lock to landscape             |
| Document reading | Allow auto-rotation           |
| Games            | Lock to preferred orientation |
| Camera apps      | Lock to landscape             |
| E-books          | Allow portrait/landscape      |
| Media viewers    | Follow sensor                 |



## Handling Configuration Changes

When your app handles orientation changes manually:

```c
// In your Android activity's onCreate or native code
aroma_android_set_orientation_portrait();

// The layout will update through aroma_refresh_layout()
```



## Performance Considerations

* Orientation changes trigger surface recreation
* Layout recalculation occurs automatically
* Consider debouncing rapid orientation changes
* Cache orientation-dependent resources



# 7. Error Handling

```c
// Always check if orientation functions are available
if (!aroma_android_get_current_orientation) {
    printf("Orientation API not available\n");
    return;
}

// Check return values where applicable
int orientation = aroma_android_get_current_orientation();
if (orientation < 0) {
    printf("Failed to get orientation\n");
}

// Verify lock state before assuming
if (aroma_android_is_orientation_locked()) {
    // Safe to assume fixed orientation
}
```


