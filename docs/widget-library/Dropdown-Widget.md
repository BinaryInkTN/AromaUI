<br />
```c
AromaNode *dropdown = aroma_ui_dropdown(
    (AromaNode *)window,
    40, 300, 300, 48,
    (char *[]){"Option A", "Option B", "Option C"},
    3,
    on_selection_changed, NULL,
    state.font
);
```
<br />

The `AromaDropdown` widget provides a compact select input that expands into a floating list of options. It supports theme-aware styling, hover highlighting, and selection change callbacks.

## Widget Structure

| Field | Type | Description |
| --- | --- | --- |
| `options` | `char**` | Array of option strings (max 32 options). |
| `option_count` | `int` | Number of available options. |
| `selected_index` | `int` | Index of the currently selected option. |
| `hover_index` | `int` | Index of the option currently under the pointer. |
| `is_expanded` | `bool` | True when the dropdown list is visible. |
| `on_selection_changed` | `callback` | Called with `(index, option, user_data)` on selection. |

### Overlay System

The dropdown uses a global overlay list to render its expanded list above all other UI elements. This allows the list to escape the bounds of its parent container.

| Function | Description |
| --- | --- |
| `aroma_dropdown_render_overlays` | Draws all expanded dropdown lists for the current frame. |
| `aroma_dropdown_overlay_hit_test` | Performs hit testing for dropdown overlays before normal scene graph traversal. |

**Sources:**[include/widgets/aroma_dropdown.h14-38](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/widgets/aroma_dropdown.h#L14-L38)[src/widgets/aroma_dropdown.c1-40](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_dropdown.c#L1-L40)

---

## Interaction Model

### Expanding and Collapsing

1. **Toggle**: Clicking the collapsed dropdown toggles `is_expanded`.
2. **Selection**: Clicking an option sets `selected_index`, collapses the list, and invokes `on_selection_changed`.
3. **Dismiss**: Clicking outside the dropdown bounds or pressing Escape collapses the list without changing the selection.

### Event Priority

The dropdown registers overlay hit-test callbacks at a high priority to intercept clicks before they reach underlying widgets [src/widgets/aroma_dropdown.c25-35](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_dropdown.c#L25-L35).

---

## API Reference

### Factory Function

```c
AromaNode *aroma_dropdown_create(
    AromaNode *parent,
    int x, int y,
    int width, int height
);
```

### Configuration

| Function | Description |
| --- | --- |
| `aroma_dropdown_add_option` | Appends an option string to the dropdown list. |
| `aroma_dropdown_set_on_change` | Sets the selection change callback. |
| `aroma_dropdown_set_font` | Sets the font for option text. |
| `aroma_dropdown_set_text_color` | Overrides the text color. |

### Helper Wrapper

`aroma_ui_dropdown` in `aroma_ui.h` wraps creation, option population, callback setup, and font assignment [include/aroma_ui.h1341-1367](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1341-L1367).

---

## Styling

Colors default to the global theme. The dropdown renders a rounded rectangle background, a selected indicator, and a dropdown arrow icon.

| Property | Default |
| --- | --- |
| Background | `theme.colors.surface` |
| Text | `theme.colors.text_primary` |
| Hover highlight | `theme.colors.surface_variant` |
| Border | `theme.colors.outline` |

**Sources:**[src/widgets/aroma_dropdown.c60-120](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_dropdown.c#L60-L120)
