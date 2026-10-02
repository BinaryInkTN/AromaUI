> Android specific. This page only applies to Android builds. All functions here need `__ANDROID__`. The backend caches screen data at startup and serves these calls from that cache.

Use DP for layout sizes. Use SP for text sizes. Use PX only when you need raw pixels.

## Density

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_density` | `float aroma_android_get_density(void)` | Density scale. Examples: 1.0 for mdpi, 2.0 for xhdpi, 3.0 for xxhdpi. Returns 1.0 when unknown. |
| `aroma_android_get_density_dpi` | `int aroma_android_get_density_dpi(void)` | Density bucket. Examples: 160, 240, 320, 480, 640. Returns 160 when unknown. |
| `aroma_android_get_scaled_density` | `float aroma_android_get_scaled_density(void)` | Text scale that honors the user font size setting. Returns 1.0 when unknown. |
| `aroma_android_get_xdpi` | `float aroma_android_get_xdpi(void)` | Physical dots per inch on the X axis. Returns 160.0 when unknown. |
| `aroma_android_get_ydpi` | `float aroma_android_get_ydpi(void)` | Physical dots per inch on the Y axis. Returns 160.0 when unknown. |

## Unit conversion

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_dp_to_px` | `int aroma_android_dp_to_px(int dp)` | Converts DP to pixels. Returns input unchanged when the backend is missing. |
| `aroma_android_px_to_dp` | `int aroma_android_px_to_dp(int px)` | Converts pixels to DP. Returns input unchanged when the backend is missing. |
| `aroma_android_sp_to_px` | `int aroma_android_sp_to_px(int sp)` | Converts SP to pixels for text. Returns input unchanged when the backend is missing. |
| `aroma_android_px_to_sp` | `int aroma_android_px_to_sp(int px)` | Converts pixels to SP. Returns input unchanged when the backend is missing. |

```c
int pad_px = aroma_android_dp_to_px(16); // Layout padding.
int text_px = aroma_android_sp_to_px(14); // Text size.
```

## Screen size

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_get_available_size_dp` | `void aroma_android_get_available_size_dp(int* w_dp, int* h_dp)` | Usable window size in DP. Excludes system bars. Writes 0, 0 when unknown. |
| `aroma_android_get_screen_size_inches` | `void aroma_android_get_screen_size_inches(float* w, float* h)` | Physical size in inches. Writes 0.0, 0.0 when unknown. |
| `aroma_android_get_screen_diagonal_inches` | `float aroma_android_get_screen_diagonal_inches(void)` | Diagonal size in inches. Returns 0.0 when unknown. |
| `aroma_android_get_screen_size_category` | `const char* aroma_android_get_screen_size_category(void)` | One of: small, normal, large, xlarge, xxlarge. Returns normal when unknown. |

```c
int w = 0, h = 0;
aroma_android_get_available_size_dp(&w, &h);
// w and h now hold the usable size in DP.
```

## Orientation

| Function | Signature | What it does |
|---|---|---|
| `aroma_android_lock_orientation` | `void aroma_android_lock_orientation(void)` | Freezes the current orientation. |
| `aroma_android_unlock_orientation` | `void aroma_android_unlock_orientation(void)` | Allows sensor rotation again. |
| `aroma_android_set_orientation_portrait` | `void aroma_android_set_orientation_portrait(void)` | Forces portrait. |
| `aroma_android_set_orientation_landscape` | `void aroma_android_set_orientation_landscape(void)` | Forces landscape. |
| `aroma_android_set_orientation_sensor` | `void aroma_android_set_orientation_sensor(void)` | Uses sensor based auto rotate. |
| `aroma_android_get_current_orientation` | `int aroma_android_get_current_orientation(void)` | Returns 1 for portrait, 2 for landscape, -1 for unknown. |
| `aroma_android_is_orientation_locked` | `bool aroma_android_is_orientation_locked(void)` | Returns true when locked, false if not. |

```c
aroma_android_set_orientation_landscape();
// Later, allow rotation again.
aroma_android_set_orientation_sensor();
```

**Sources:** [include/aroma_android.h484-749](https://github.com/BinaryInkTN/AromaUI/blob/main/include/aroma_android.h#L484-L749) [src/backends/platforms/aroma_platform_android.c591-823](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L591-L823) [src/backends/platforms/aroma_platform_android.c1304-1415](https://github.com/BinaryInkTN/AromaUI/blob/main/src/backends/platforms/aroma_platform_android.c#L1304-L1415)

## Next

* Read [App Setup](App-Setup.md) for lifecycle basics.
* Read [Device APIs](Device-APIs.md) for storage paths.
