
Structural widgets that arrange children, handle scrolling, and provide navigation patterns.

## Container

```c
AromaNode *container = aroma_container_create(root, 0, 0, 800, 480);
aroma_container_set_layout_mode(container, AROMA_LAYOUT_MODE_FLEX);
aroma_node_set_flex_direction(container, AROMA_FLEX_COLUMN);
```

Containers are the primary structural widget. They support flexbox, grid, and scrolling.

```incense-demo layout
```

## Scrollable Container

```c
aroma_container_set_scrollable(container, true);
aroma_container_set_scroll_direction(container, AROMA_SCROLL_VERTICAL);
```

Features:
- **Velocity tracking** for fling gestures
- **Overscroll bounce** with spring-back animation
- **Auto content sizing** based on children

```incense-demo scroll
```

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
| `aroma_listview_set_item_hidden` | Hide/show individual items (useful for filtering) |

ListView is built on a scrollable container. Item heights are calculated dynamically. Use `aroma_listview_set_item_hidden()` to hide items without removing them, then call `aroma_node_invalidate()` to trigger a redraw.

## Table

```c
AromaNode *table = aroma_table_create(parent, 0, 0, 600, 300, 3);
aroma_table_set_font(table, font);
aroma_table_set_header(table, 0, "Name");
aroma_table_set_header(table, 1, "Size");
aroma_table_set_header(table, 2, "Action");
int row = aroma_table_add_row(table);
aroma_table_set_cell_text(table, row, 0, "Snake");
aroma_table_set_cell_text(table, row, 1, "5.8 KB");
AromaNode *btn = aroma_ui_button(table, "Get", 0, 0, 100, 32,
                                 on_get, NULL, font);
aroma_table_set_cell_widget(table, row, 2, btn);
```

Tables render their own grid (headers, zebra rows, selection
highlight, dividers) and position embedded cell widgets. They live
inside a scroll container for long lists (`aroma_ui_table()` builds
that pair for you).

| Function | Purpose |
| --- | --- |
| `aroma_table_add_row` | Append a row, returns its index |
| `aroma_table_clear_rows(table, destroy_widgets)` | Remove all rows (optionally freeing cell widgets) for rebuilds |
| `aroma_table_set_cell_text` | Set a text cell (63 chars max) |
| `aroma_table_set_cell_widget` | Embed a button/icon/progress widget in a cell |
| `aroma_table_set_header` | Set a column header label |
| `aroma_table_set_header_visible` | Hide the header band (e.g. app lists) |
| `aroma_table_set_col_width` | Size a column in pixels |
| `aroma_table_set_row_height` | Row height in pixels (default 40) |
| `aroma_table_set_selected_row` / `aroma_table_get_selected_row` | Control/read selection (`-1` = none) |
| `aroma_table_get_row_count` | Current row count |
| `aroma_table_set_callback` | Row-tap callback with the row index |
| `aroma_table_set_font` | Text font (required - nothing draws without it) |
| `aroma_table_destroy` | Free rows, cell widgets and the table |

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

```incense-demo menu
```

## Live demo

```incense-demo tabs
```

```incense-demo list
```

## What's Next

- Learn [Input & Controls](Input-and-Control-Widgets.md) for interactive elements.
- Explore the [Map Widget](Map-Widget.md) for geographic data.
- Try [Incense](Incense-Sandbox.md) for declarative UI.
