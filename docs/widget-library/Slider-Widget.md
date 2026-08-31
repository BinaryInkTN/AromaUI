<br />
```c
AromaNode *slider = aroma_ui_slider(
    (AromaNode *)window,
    40, 200, 400, 48,
    0, 100, 50,
    on_volume_changed, NULL
);
```
<br />

The `AromaSlider` widget provides a horizontal range input with a draggable thumb. It maps pixel positions to integer or float values and supports hover and drag states with real-time callbacks.

## Widget Structure

| Field | Type | Description |
| --- | --- | --- |
| `rect` | `AromaRect` | Position and size of the slider track. |
| `min_value` | `int` | Minimum selectable value. |
| `max_value` | `int` | Maximum selectable value. |
| `current_value` | `int` | Current value. |
| `track_color` | `uint32_t` | Background track color. |
| `thumb_color` | `uint32_t` | Thumb fill color. |
| `thumb_hover_color` | `uint32_t` | Thumb color when hovered. |
| `track_height` | `int` | Height of the track bar in pixels. |
| `track_corner_radius` | `float` | Corner radius of the track. |
| `thumb_size` | `int` | Width and height of the thumb in pixels. |
| `thumb_corner_radius` | `float` | Corner radius of the thumb. |
| `thumb_border_color` | `uint32_t` | Border color of the thumb. |
| `is_hovered` | `bool` | True when the pointer is over the thumb. |
| `is_dragging` | `bool` | True while the thumb is being dragged. |
| `on_change` | `callback` | Called when the value changes. |

**Sources:**[include/widgets/aroma_slider.h15-37](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/widgets/aroma_slider.h#L15-L37)

---

## Interaction Model

The slider supports mouse and touch input. It uses priority-based event subscriptions to intercept drag events even when nested inside scrollable containers.

### Event Handling

| Event | Action |
| --- | --- |
| `EVENT_TYPE_MOUSE_CLICK` / `TOUCH_DOWN` | If inside track bounds, begin drag and jump thumb to position. |
| `EVENT_TYPE_MOUSE_MOVE` / `TOUCH_MOVE` | If dragging, update thumb position and fire `on_change`. |
| `EVENT_TYPE_MOUSE_RELEASE` / `TOUCH_UP` | End drag. |
| `EVENT_TYPE_MOUSE_SCROLL` | Adjust value by scroll delta. |
| `EVENT_TYPE_MOUSE_ENTER` / `EXIT` | Update hover state for visual feedback. |

### Value Mapping

The slider maps screen coordinates to values linearly:

```c
value = min_value + (thumb_x - track_x) * (max_value - min_value) / track_width
```

**Sources:**[src/widgets/aroma_slider.c1-60](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_slider.c#L1-L60)

---

## API Reference

### Factory Function

```c
AromaNode *aroma_slider_create(
    AromaNode *parent,
    int x, int y, int width, int height,
    int min_value, int max_value,
    int initial_value
);
```

### Configuration

| Function | Description |
| --- | --- |
| `aroma_slider_set_value` | Programmatically sets the current value. |
| `aroma_slider_get_value` | Returns the current value. |
| `aroma_slider_set_on_change` | Sets the callback invoked when the value changes. |

### Helper Wrapper

`aroma_ui_slider` in `aroma_ui.h` wraps creation, callback setup, and event subscription into a single call [include/aroma_ui.h821-836](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L821-L836).

---

## Styling

Colors default to the global theme but can be overridden. The track and thumb are rendered as rounded rectangles using the graphics backend's `fill_rectangle` and `draw_hollow_rectangle` primitives.

| Property | Default Source |
| --- | --- |
| Track color | `theme.colors.surface` or custom `track_color` |
| Thumb color | `theme.colors.primary` or custom `thumb_color` |
| Thumb border | `theme.colors.border` |
| Track height | `rect.height / 2` |
| Thumb size | `rect.height` |

**Sources:**[src/widgets/aroma_slider.c61-120](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_slider.c#L61-L120)
