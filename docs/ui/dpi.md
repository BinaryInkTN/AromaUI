<img src="ui/dpi.png"/>

## 1. Overview

The AromaUI DPI System provides density-aware utilities for Android builds.  
It ensures consistent layout scaling across different screen densities, physical sizes, and user font preferences.

This subsystem allows you to:

- Query density metrics
- Convert between DP, PX, and SP units
- Detect physical screen size
- Retrieve available window dimensions
- Respect user font scaling
- Access true hardware DPI values

## 2. Core Density Concepts

### Density (Scale Factor)

Density represents logical scaling relative to the baseline **160 DPI (mdpi)**.

| DPI Category | Density | Example DPI |
|--------------|----------|-------------|
| mdpi         | 1.0      | 160         |
| hdpi         | 1.5      | 240         |
| xhdpi        | 2.0      | 320         |
| xxhdpi       | 3.0      | 480         |
| xxxhdpi      | 4.0      | 640         |

Retrieve density scale factor:

```c
float density = aroma_android_get_density();
```

Retrieve raw density DPI:

```c
int dpi = aroma_android_get_density_dpi();
```

## 3. Text Scaling (Scaled Density)

Scaled density accounts for the user's font size setting.

Use for text scaling (SP units).

```c
float scaledDensity = aroma_android_get_scaled_density();
```

Never use plain density for text scaling.


## 4. Unit Conversions

### DP -> PX

Density-independent pixels to physical pixels.

```c
int px = aroma_android_dp_to_px(16);
```

Conceptual formula:

```
px = dp × density
```


### PX -> DP

```c
int dp = aroma_android_px_to_dp(48);
```


### SP -> PX (Text)

```c
int px = aroma_android_sp_to_px(14);
```

Conceptual formula:

```
px = sp × scaledDensity
```


### PX -> SP

```c
int sp = aroma_android_px_to_sp(28);
```


## 5. Window Size (DP Units)

Retrieve available layout space excluding system UI (status bar, navigation bar).

```c
int width_dp;
int height_dp;

aroma_android_get_available_size_dp(&width_dp, &height_dp);
```

This should be used for responsive layout logic instead of raw pixel values.


## 6. Physical Screen Size

### Width and Height in Inches

```c
float width_in;
float height_in;

aroma_android_get_screen_size_inches(&width_in, &height_in);
```


### Diagonal Size in Inches

```c
float diagonal = aroma_android_get_screen_diagonal_inches();
```

Useful for differentiating:

- Phone
- Tablet
- Large tablet
- Embedded display

## 7. Screen Size Category

Returns a logical size classification string:

```c
const char* category = aroma_android_get_screen_size_category();
```

Possible values:

| Value     | Meaning              |
|-----------|---------------------|
| small     | Small phone         |
| normal    | Standard phone      |
| large     | Phablet / small tab |
| xlarge    | Tablet              |
| xxlarge   | Large tablet / TV   |

## 8. True Physical DPI

Retrieve hardware DPI along each axis:

```c
float xdpi = aroma_android_get_xdpi();
float ydpi = aroma_android_get_ydpi();
```

These values represent the actual physical dots-per-inch of the panel and may differ from logical density.


