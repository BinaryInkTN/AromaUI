> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`.

Location streams through `LocationManager` with the GPS and network providers. Request runtime location permissions first with [Permissions](Permissions.md), then start updates with a minimum time and distance. Fixes arrive in the callback as latitude, longitude, accuracy in meters, and wall-clock millis.

## Functions

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_location_register` | `void aroma_android_location_register(void (*cb)(double lat, double lon, float accuracy, long long time_ms))` | Sets a persistent callback shared by location updates. |
| `aroma_android_location_available` | `bool aroma_android_location_available(void)` | Returns true when a GPS or network provider is enabled. |
| `aroma_android_location_start` | `bool aroma_android_location_start(long min_time_ms, float min_dist_m)` | Starts updates roughly every `min_time_ms` milliseconds or `min_dist_m` meters. Returns true when at least one provider accepted. |
| `aroma_android_location_stop` | `void aroma_android_location_stop(void)` | Stops updates. Call it when pausing to save battery. No return value. |

## Example

```c
#ifdef __ANDROID__
#include <aroma_android.h>

static void on_fix(double lat, double lon, float acc, long long t) {
    (void)acc;
    (void)t;
    if (lat < -90.0 || lat > 90.0) {
        return;
    }
    if (lon < -180.0 || lon > 180.0) {
        return;
    }
    aroma_android_location_stop();
}

void track_once(void) {
    if (!aroma_android_location_available()) {
        return;
    }
    aroma_android_location_register(on_fix);
    aroma_android_location_start(1000, 1.0f);
}
#endif
```

**Sources:** [include/aroma_android.h1312-1333](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L1312-L1333) [tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl2580-2637](https://github.com/BinaryInkTN/AromaUI/blob/main/tools/cli/templates/android/app/src/main/java/AromaHelper.java.tpl#L2580-L2637)

## Next

* Read [Permissions](Permissions.md) for location permissions.
* Read [Sensors](Sensors.md) for motion input without permissions.
