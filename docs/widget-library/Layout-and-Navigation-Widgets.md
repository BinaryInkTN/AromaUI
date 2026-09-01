
Structural widgets that arrange children, handle scrolling, and provide navigation patterns.

## Container

```c
AromaNode *container = aroma_container_create(root, 0, 0, 800, 480);
aroma_container_set_layout_mode(container, AROMA_LAYOUT_MODE_FLEX);
aroma_node_set_flex_direction(container, AROMA_FLEX_COLUMN);
```

Containers are the primary structural widget. They support flexbox, grid, and scrolling.

## Scrollable Container

```c
aroma_container_set_scrollable(container, true);
aroma_container_set_scroll_direction(container, AROMA_SCROLL_VERTICAL);
```

Features:
- **Velocity tracking** for fling gestures
- **Overscroll bounce** with spring-back animation
- **Auto content sizing** based on children

## ListView

```c
AromaNode *list = aroma_ui_listview(root, 0, 0, 300, 400, on_item_select, NULL, font);
aroma_listview_add_item(list, "Item 1", "Secondary text", NULL);
aroma_listview_add_header(list, "Section");
aroma_listview_add_separator(list);
```

| Function | Purpose |
|---|---|
| `aroma_listview_add_item` | Add a normal row |
| `aroma_listview_add_item_with_icon` | Add row with icon |
| `aroma_listview_add_header` | Add section header |
| `aroma_listview_add_separator` | Add divider line |
| `aroma_listview_clear` | Remove all items |
| `aroma_listview_get_selected` | Get selected index |
| `aroma_listview_set_font` | Change item font |

ListView is built on a scrollable container. Item heights are calculated dynamically.

## Table

```c
AromaNode *table = aroma_ui_table(root, 0, 0, 600, 300, 3);
aroma_table_set_header(table, headers);
aroma_table_add_row(table, cells);
```

Tables use grid layout internally and support scrollable data grids.

## Tabs

```c
AromaNode *tabs = aroma_ui_tabs(root, 0, 0, 800, 48);
aroma_tabs_add_tab(tabs, "Home", home_content);
aroma_tabs_add_tab(tabs, "Settings", settings_content);
```

Supports animated transitions (`AROMA_ANIM_SLIDE_X`, `AROMA_ANIM_FADE`).

## Sidebar

```c
AromaNode *sidebar = aroma_ui_sidebar(root, 0, 0, 280, 480);
aroma_sidebar_add_item(sidebar, AROMA_ICON_HOME, "Home", home_content);
```

Responsive: retracts to icon-only mode below a breakpoint.

## Menu

```c
AromaNode *menu = aroma_menu_create(root, x, y, 200);
aroma_menu_add_item(menu, "Edit", AROMA_MENU_ITEM_NORMAL, on_edit);
aroma_menu_add_item(menu, "Delete", AROMA_MENU_ITEM_DESTRUCTIVE, on_delete);
```

Floating context menu with click-away dismiss.

## What's Next

- Learn [Input & Controls](Input-and-Control-Widgets.md) for interactive elements.
- Explore the [Map Widget](Map-Widget.md) for geographic data.
- Try [Incense](../widget-library/wasm/incense_sandbox/index.html) for declarative UI.
