The `AromaDebugOverlay` widget provides a draggable, semi-transparent panel that displays real-time runtime statistics. It is intended for development and profiling, showing FPS, dirty regions, node count, memory usage, and backend information.

## Widget Structure

| Field | Type | Description |
| --- | --- | --- |
| `visible` | `bool` | Controls overlay visibility. |
| `font` | `AromaFont*` | Font used for debug text. |
| `bg_color` | `uint32_t` | Semi-transparent background (default: frosted glass). |
| `text_color` | `uint32_t` | Text color. |
| `border_color` | `uint32_t` | Border color. |
| `fps` | `float` | Computed frames-per-second. |
| `frame_count` | `int` | Frame counter for FPS calculation. |
| `dragging` | `bool` | True while the user is dragging the overlay. |
| `animating` | `bool` | True while the overlay is settling after a fling. |

### Dragging and Fling Physics

The overlay supports click-drag repositioning with momentum-based fling. When released, the overlay continues moving with decreasing velocity, bouncing off window edges with a restitution coefficient of 0.4 [src/widgets/aroma_debug_overlay.c266-318](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_debug_overlay.c#L266-L318).

### Event Handling

The overlay registers high-priority listeners (priority 100) for mouse and touch events to ensure it can be dragged even over other UI elements [src/widgets/aroma_debug_overlay.c212-214](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_debug_overlay.c#L212-L214).

---

## Displayed Metrics

Each frame, the overlay computes and renders the following lines:

| Line | Metric | Source |
| --- | --- | --- |
| 1 | AromaUI version | Hardcoded string (e.g., `v0.0.1`). |
| 2 | Graphics backend | `aroma_backend_abi.get_graphics_backend_type()` |
| 3 | Platform backend | `aroma_backend_abi.get_platform_backend_type()` |
| 4 | FPS | `frame_count / elapsed` over 0.5s windows. |
| 5 | Dirty regions | `aroma_dirty_list_get(&dirty_count)` |
| 6 | Node count | Recursive `count_nodes()` from root. |
| 7 | Memory | `global_memory_system.widget_pools[0].total_allocated / total_freed` |
| 8 | Focus node ID | `g_focused_node->node_id` |
| 9 | Resolution | `platform->get_window_size()` |
| 10 | Event root ID | `aroma_event_get_root()->node_id` |

**Sources:**[src/widgets/aroma_debug_overlay.c322-436](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_debug_overlay.c#L322-L436)

---

## API Reference

### Factory Function

```c
AromaNode *aroma_debug_overlay_create(
    AromaNode *parent,
    int x, int y,
    int width
);
```

### Configuration

| Function | Description |
| --- | --- |
| `aroma_debug_overlay_set_font` | Sets the font used for debug text. |
| `aroma_debug_overlay_set_visible` | Shows or hides the overlay. |

### Helper Wrapper

`aroma_ui_debug_overlay` in `aroma_ui.h` wraps creation and font setup [include/aroma_ui.h1379-1390](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1379-L1390).

---

## Usage in AromaOS

In the Car Infotainment example, the debug overlay is toggled via the Easter Egg developer mode. When activated, `build_easter_egg_ui()` injects the overlay into the scene graph, and the user can drag it to any position on screen [examples/car_infotainment/easter_egg.c1-50](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/examples/car_infotainment/easter_egg.c#L1-L50).

```c
state.debug_overlay = aroma_ui_debug_overlay(
    state.root_node,
    10, 10, 280
);
aroma_debug_overlay_set_font(state.debug_overlay, state.font_small);
```

**Sources:**[examples/car_infotainment/app_state.h136-137](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/examples/car_infotainment/app_state.h#L136-L137)[include/widgets/aroma_debug_overlay.h14-17](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/widgets/aroma_debug_overlay.h#L14-L17)
