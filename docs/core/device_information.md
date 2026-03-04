<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

Provides runtime access to device-level system information.


## `int aroma_android_get_battery_level();`

Returns the current battery percentage.

### Returns

* 0–100
* `-1` if unavailable

### Example

```c
int level = aroma_android_get_battery_level();
if (level >= 0) {
    printf("Battery: %d%%\n", level);
}
```

### Notes

* May not update in real-time unless refreshed.
* Suitable for dashboards and monitoring apps.
