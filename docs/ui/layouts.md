<img src="ui/layouts.png"/>


## 1  Introduction

The layout system computes the screen-space position and dimensions of every widget in the UI tree. It is driven by two orthogonal properties attached to each node: a **self layout type**, which determines how the node positions itself within its parent, and a **container layout mode**, which determines how the node arranges its own children.

Layout is resolved by calling the layout update function on the root node of a window or sub-tree. The engine traverses the tree recursively, writing final pixel coordinates and dimensions into each widget's geometry record.

---

> **NOTE**
> All coordinates are in pixels, expressed as signed 32-bit integers. The origin `(0, 0)` is the top-left corner of the parent bounds.

---

## 2  Layout model

### 2.1  Self layout

The self layout type controls how a node positions and sizes itself relative to the bounds provided by its parent. It is set using one of the functions described in Section 3. The default type is absolute positioning  the node's geometry is left unchanged by the engine.

### 2.2  Container layout

The container layout mode controls how a node arranges its direct children. It is set using `aroma_node_set_layout_mode()`. The three available modes are described in Section 4.

The two properties are independent. A node may, for example, use anchor self layout to pin itself to the bottom of its parent, while simultaneously acting as a flex container for its own children.

---

## 3  Self layout types

### 3.1  Absolute positioning

**Default behavior  no function call required.**

The widget geometry (`x`, `y`, `width`, `height`) is not modified by the layout engine. The application is responsible for setting these values directly at creation time.

---

### 3.2  Fill parent

```c
aroma_node_set_layout_fill(node);
```

The node is resized and repositioned to exactly match the bounds provided by its parent. All four geometry fields are overwritten on every layout pass.

| Field | Resolved value |
|---|---|
| `x` | `parent_x` |
| `y` | `parent_y` |
| `width` | `parent_width` |
| `height` | `parent_height` |

---

### 3.3  Center

```c
aroma_node_set_layout_center(node);
```

The node is centered within its parent bounds. The application must set `width` and `height` on the widget before the layout pass. If either dimension is zero at layout time, it is set to the corresponding parent dimension before centering is applied.

| Field | Resolved value |
|---|---|
| `x` | `parent_x + (parent_width − width) / 2` |
| `y` | `parent_y + (parent_height − height) / 2` |
| `width` | Unchanged (or parent dimension if zero) |
| `height` | Unchanged (or parent dimension if zero) |

---

### 3.4  Anchor

```c
aroma_node_set_layout_anchor(node, left, top, right, bottom);
```

Pins the node to one or more edges of its parent. Each parameter is a pixel offset from the corresponding edge. Pass `-1` to leave an edge unanchored.

**Table 1. Anchor resolution rules**

| `left` | `right` | Horizontal resolution |
|---|---|---|
| ≥ 0 | −1 | `x = parent_x + left`; width unchanged |
| −1 | ≥ 0 | `x = parent_x + parent_width − right − width`; width unchanged |
| ≥ 0 | ≥ 0 | `x = parent_x + left`; `width = parent_width − left − right` |

**Table 2. Anchor resolution rules (vertical)**

| `top` | `bottom` | Vertical resolution |
|---|---|---|
| ≥ 0 | −1 | `y = parent_y + top`; height unchanged |
| −1 | ≥ 0 | `y = parent_y + parent_height − bottom − height`; height unchanged |
| ≥ 0 | ≥ 0 | `y = parent_y + top`; `height = parent_height − top − bottom` |

> **NOTE**
> When both edges on an axis are anchored, the widget's `width` or `height` value is overwritten by the engine. Any value previously set by the application on that axis will be discarded.

**Example  pin to bottom edge, full width:**

```c
// left = 0, top = -1 (unanchored), right = 0, bottom = 0
aroma_node_set_layout_anchor(action_bar, 0, -1, 0, 0);
```

**Example  8 px inset on all sides:**

```c
aroma_node_set_layout_anchor(panel, 8, 8, 8, 8);
```

---

## 4  Container layout modes

### 4.1  No layout (NONE)

```c
aroma_node_set_layout_mode(container, AROMA_LAYOUT_MODE_NONE);
```

No automatic child positioning is performed. Each child uses its own self layout type to determine its position. This is the default mode.

---

### 4.2  Flexbox layout

```c
aroma_node_set_layout_mode(container, AROMA_LAYOUT_MODE_FLEX);
```

Children are arranged sequentially along a single main axis. The axis direction, main-axis alignment, cross-axis alignment, and inter-child spacing are all configurable. Refer to Sections 5, 6, and 7 for the relevant properties.

**Setting the flex direction:**

```c
aroma_node_set_flex_direction(container, AROMA_FLEX_ROW);     // left-to-right
aroma_node_set_flex_direction(container, AROMA_FLEX_COLUMN);  // top-to-bottom
```

**Full flex container configuration:**

```c
aroma_node_set_layout_mode(container, AROMA_LAYOUT_MODE_FLEX);
aroma_node_set_flex_direction(container, AROMA_FLEX_ROW);
aroma_node_set_justify_content(container, AROMA_JUSTIFY_START);
aroma_node_set_align_items(container, AROMA_ALIGN_CENTER);
aroma_node_set_gap(container, 8);
```

The `aroma_ui_container()` helper combines all of the above into a single call:

```c
AromaNode *cont = aroma_ui_container(
    parent,
    x, y, width, height,
    AROMA_LAYOUT_MODE_FLEX,
    AROMA_FLEX_ROW,
    AROMA_JUSTIFY_START,
    AROMA_ALIGN_CENTER
);
```

> **NOTE**
> Only visible children with valid widget data participate in flex layout. Hidden nodes do not consume space on the main axis.

> **CAUTION**
> If any child has `flex_grow > 0` and remaining space is available, `justify_content` distribution is **not** applied. The growing children absorb all remaining space. Do not combine `flex_grow` with `justify_content` spacing modes on the same container. Refer to Section 6.

---

### 4.3  Grid layout

```c
aroma_node_set_layout_mode(container, AROMA_LAYOUT_MODE_GRID);
aroma_node_set_grid_cols(container, 3);
aroma_node_set_grid_rows(container, 2);
aroma_node_set_gap(container, 12);
```

Children are placed into equal-sized cells in row-major order (left-to-right, then top-to-bottom). Every child is resized to exactly fill its assigned cell.

Cell dimensions are computed as follows:

```
cell_width  = (container_width  − (cols − 1) × gap) / cols
cell_height = (container_height − (rows − 1) × gap) / rows
```

> **CAUTION**
> The grid does not scroll or wrap to additional rows. If the number of children exceeds `cols × rows`, excess children are not positioned. Size the grid to accommodate the maximum expected child count.

---

## 5  Alignment properties

Alignment properties apply to flex containers only. They have no effect in `AROMA_LAYOUT_MODE_GRID` or `AROMA_LAYOUT_MODE_NONE`.

### 5.1  justify_content

Controls the distribution of children along the **main axis** when total child size is less than the container size and no child has `flex_grow > 0`.

```c
aroma_node_set_justify_content(container, justify);
```

**Table 3. justify_content values**

| Value | Description |
|---|---|
| `AROMA_JUSTIFY_START` | Children packed toward the start of the main axis. Default. |
| `AROMA_JUSTIFY_CENTER` | Children centered as a group along the main axis. |
| `AROMA_JUSTIFY_END` | Children packed toward the end of the main axis. |
| `AROMA_JUSTIFY_SPACE_BETWEEN` | Children distributed evenly; first and last children are flush with the container edges. |
| `AROMA_JUSTIFY_SPACE_AROUND` | Children distributed evenly; half the inter-child gap is placed at each end. |
| `AROMA_JUSTIFY_SPACE_EVENLY` | Children distributed evenly; equal space between all children and at each end. |

---

### 5.2  align_items

Controls the alignment of children along the **cross axis** (perpendicular to the main axis).

```c
aroma_node_set_align_items(container, align);
```

**Table 4. align_items values**

| Value | Description |
|---|---|
| `AROMA_ALIGN_START` | Children aligned to the start of the cross axis. |
| `AROMA_ALIGN_CENTER` | Children centered on the cross axis. |
| `AROMA_ALIGN_END` | Children aligned to the end of the cross axis. |
| `AROMA_ALIGN_STRETCH` | Children stretched to fill the full cross-axis dimension of the container. |

> **NOTE**
> `AROMA_ALIGN_STRETCH` overwrites the child's cross-axis dimension (`width` for column containers, `height` for row containers) on every layout pass.

---

## 6  Flex grow

A child node may declare a grow factor to consume remaining main-axis space after fixed-size children and gaps have been accounted for.

```c
aroma_node_set_flex_grow(child, grow_factor);
```

When multiple children have `flex_grow > 0`, remaining space is distributed among them in proportion to their respective grow factors.

| Condition | Result |
|---|---|
| All children have `flex_grow = 0` | `justify_content` governs remaining space distribution |
| One or more children have `flex_grow > 0` | Remaining space is distributed proportionally; `justify_content` has no effect |

> **CAUTION**
> A `flex_grow` value of `0.0f` (the default) means the child is fixed-size and does not participate in space distribution.

**Example  fixed sidebar with a growing content area:**

```c
// Sidebar: fixed width, no growth
aroma_node_set_flex_grow(sidebar, 0.0f);

// Content: grows to fill all remaining width
aroma_node_set_flex_grow(content, 1.0f);
```

**Example  toolbar with a flexible spacer:**

```c
AromaNode *toolbar = aroma_ui_container(
    root, 0, 0, 800, 48,
    AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW,
    AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER
);
aroma_node_set_gap(toolbar, 8);

AromaNode *logo   = aroma_ui_label(toolbar, "MyApp", 0, 0, LABEL_STYLE_TITLE, font);
AromaNode *spacer = aroma_ui_container(toolbar, 0, 0, 0, 0,
                        AROMA_LAYOUT_MODE_NONE, 0, 0, 0);
aroma_node_set_flex_grow(spacer, 1.0f);

AromaNode *btn    = aroma_ui_button(toolbar, "Settings", 0, 0, 120, 36,
                        on_settings, NULL, font);
```

---

## 7  Gap

Sets the uniform spacing in pixels between consecutive children in a flex or grid container.

```c
aroma_node_set_gap(container, gap_px);
```

Gap is applied **between** children only. No space is added before the first child or after the last child.

---

## 8  Scrollable containers

`aroma_ui_scrollable_container()` creates a container whose children may extend beyond the visible viewport. Scroll position, clipping, and scrollbar rendering are handled internally.

```c
AromaNode *scroll = aroma_ui_scrollable_container(
    parent,
    x, y,
    viewport_width, viewport_height,
    AROMA_SCROLL_VERTICAL
);
```

The content area is measured automatically after each layout pass. Scrolling is activated only when the total child extent exceeds the viewport dimension in the configured scroll direction.

> **NOTE**
> The default child arrangement for a scrollable container is a vertical flex column (`AROMA_JUSTIFY_START`, `AROMA_ALIGN_CENTER`). Child layout properties may be overridden after creation if a different arrangement is required.

---

## 9  API summary

**Table 5. Self layout functions**

| Function | Description |
|---|---|
| `aroma_node_set_layout_fill(node)` | Node fills parent bounds completely. |
| `aroma_node_set_layout_center(node)` | Node is centered within parent bounds. |
| `aroma_node_set_layout_anchor(node, left, top, right, bottom)` | Node is pinned to one or more parent edges. |

**Table 6. Container layout functions**

| Function | Description |
|---|---|
| `aroma_node_set_layout_mode(node, mode)` | Sets the child layout strategy (`NONE`, `FLEX`, `GRID`). |
| `aroma_node_set_flex_direction(node, dir)` | Sets flex axis direction (`ROW` or `COLUMN`). |
| `aroma_node_set_justify_content(node, justify)` | Sets main-axis alignment. |
| `aroma_node_set_align_items(node, align)` | Sets cross-axis alignment. |
| `aroma_node_set_flex_grow(node, grow)` | Sets grow factor for a flex child. |
| `aroma_node_set_gap(node, gap)` | Sets inter-child spacing in pixels. |
| `aroma_node_set_grid_cols(node, cols)` | Sets the number of grid columns. |
| `aroma_node_set_grid_rows(node, rows)` | Sets the number of grid rows. |

**Table 7. Convenience creation helpers**

| Function | Description |
|---|---|
| `aroma_ui_window(title, w, h, fullscreen)` | Creates a root window node. |
| `aroma_ui_container(parent, x, y, w, h, mode, dir, justify, align)` | Creates a container with layout configured in one call. |
| `aroma_ui_scrollable_container(parent, x, y, w, h, direction)` | Creates a scrollable container with auto content sizing. |

---

## 10  Code examples

### 10.1  Centered card within a full-screen background

```c
AromaNode *root = aroma_ui_window("Application", 800, 600, false);

AromaNode *bg = aroma_ui_container(
    root, 0, 0, 800, 600,
    AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
    AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER
);
aroma_node_set_layout_fill(bg);

AromaNode *card = aroma_ui_card(bg, 0, 0, 320, 200, AROMA_CARD_ELEVATED);
```

---

### 10.2  Two-panel layout: fixed sidebar and growing content area

```c
AromaNode *root_layout = aroma_ui_container(
    root, 0, 0, 800, 600,
    AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW,
    AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH
);
aroma_node_set_layout_fill(root_layout);

AromaNode *sidebar = aroma_ui_container(
    root_layout, 0, 0, 200, 0,
    AROMA_LAYOUT_MODE_NONE, 0, 0, 0
);

AromaNode *content = aroma_ui_container(
    root_layout, 0, 0, 0, 0,
    AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
    AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH
);
aroma_node_set_flex_grow(content, 1.0f);
```

---

### 10.3  Bottom-anchored action bar

```c
AromaNode *bar = aroma_ui_container(
    root, 0, 0, 800, 56,
    AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW,
    AROMA_JUSTIFY_END, AROMA_ALIGN_CENTER
);
aroma_node_set_layout_anchor(bar, 0, -1, 0, 0);
aroma_node_set_gap(bar, 8);

aroma_ui_button(bar, "Cancel",  0, 0, 100, 36, on_cancel,  NULL, font);
aroma_ui_button(bar, "Confirm", 0, 0, 100, 36, on_confirm, NULL, font);
```

---

### 10.4  3 × 2 uniform card grid

```c
AromaNode *grid = aroma_ui_container(
    root, 16, 16, 768, 400,
    AROMA_LAYOUT_MODE_GRID, 0, 0, 0
);
aroma_node_set_grid_cols(grid, 3);
aroma_node_set_grid_rows(grid, 2);
aroma_node_set_gap(grid, 12);

for (int i = 0; i < 6; i++) {
    aroma_ui_card(grid, 0, 0, 0, 0, AROMA_CARD_OUTLINED);
}
```

---

### 10.5  Vertically scrollable list

```c
AromaNode *list = aroma_ui_listview(
    root,
    0, 56,      /* x, y  offset below a 56 px toolbar   */
    400, 544,   /* width, height  viewport dimensions    */
    on_item_click, NULL, font
);

for (int i = 0; i < 50; i++) {
    char label[32];
    snprintf(label, sizeof(label), "Item %d", i);
    aroma_listview_add_item(list, label);
}
```
