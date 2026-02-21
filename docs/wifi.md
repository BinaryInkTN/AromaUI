<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

Provides programmatic access to Wi‑Fi state and control.


## `bool aroma_android_is_wifi_enabled();`

Returns whether Wi‑Fi is enabled.

### Example

```c
if (!aroma_android_is_wifi_enabled()) {
    aroma_android_set_wifi_enabled(true);
}
```

### Notes

* Some Android versions restrict direct Wi‑Fi toggling.


## `void aroma_android_set_wifi_enabled(bool enabled);`

Enables or disables Wi‑Fi.

### Example

```c
aroma_android_set_wifi_enabled(false);
```

### Notes

* Behavior varies across Android API levels.
* May require elevated permissions.
