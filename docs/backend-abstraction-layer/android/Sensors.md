> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Sensors stream through `SensorManager`. Register a listener for the sensor you need, read values from the callback, unregister when the screen hides. No manifest permission is needed for motion and magnetic sensors.

## Sensor types

| Constant | Value | Sensor |
|---|---|---|
| `AROMA_SENSOR_ACCELEROMETER` | 1 | Acceleration including gravity, in m/s². |
| `AROMA_SENSOR_MAGNETIC_FIELD` | 2 | Geomagnetic field in microtesla. |
| `AROMA_SENSOR_GYROSCOPE` | 4 | Angular velocity in rad/s. |

The callback signature is `void (*cb)(int type, float x, float y, float z, long long timestamp_ns)`. The timestamp is nanoseconds since boot.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_sensor_register_callbacks` | `void aroma_android_sensor_register_callbacks(void (*cb)(int type, float x, float y, float z, long long timestamp_ns))` | Sets a persistent callback shared by every sensor. |
| `aroma_android_sensor_start` | `void aroma_android_sensor_start(int type, int rate_us, void (*cb)(int type, float x, float y, float z, long long timestamp_ns))` | Starts delivery for one sensor type. `rate_us` is the requested period in microseconds, 20000 is a good default. Passing a callback also installs it. |
| `aroma_android_sensor_stop` | `void aroma_android_sensor_stop(int type)` | Stops delivery for one sensor type. Call it when pausing to save battery. |
| `aroma_android_sensor_available` | `bool aroma_android_sensor_available(int type)` | Returns true when the device has this sensor. Check before starting. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

static float tilt_x = 0;
static float tilt_y = 0;

static void on_motion(int type, float x, float y, float z, long long ts) {
    (void)z;
    (void)ts;
    if (type == AROMA_SENSOR_ACCELEROMETER) {
        tilt_x = x;
        tilt_y = y;
    }
}

void motion_on(void) {
    if (!aroma_android_sensor_available(AROMA_SENSOR_ACCELEROMETER)) {
        return;
    }
    aroma_android_sensor_start(AROMA_SENSOR_ACCELEROMETER, 20000, on_motion);
}

void motion_off(void) {
    aroma_android_sensor_stop(AROMA_SENSOR_ACCELEROMETER);
}
#endif
```

**Sources:** [include/aroma_android.h828-830](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L828-L830) [include/aroma_android.h1133-1160](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1133-L1160)

## Next

* Read [Device APIs](Device-APIs.md) for battery and connectivity state.
* Read [Display](Display.md) for orientation locking during motion input.
