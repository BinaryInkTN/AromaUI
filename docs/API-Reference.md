
This page provides a high-level index of the public AromaUI API. For detailed documentation on subsystems, see the [Core Framework](core-framework/) and [Widget Library](widget-library/) sections.

## Core Lifecycle

| Function | Description | Source |
| --- | --- | --- |
| `aroma_ui_init` | Initializes the framework, slab allocator, and global state. | [include/aroma_ui.h175-183](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L175-L183) |
| `aroma_ui_shutdown` | Releases all resources and resets global state. | [include/aroma_ui.h212-217](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L212-L217) |
| `aroma_ui_is_running` | Returns true while the main loop should continue. | [include/aroma_ui.h225-230](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L225-L230) |
| `aroma_ui_process_events` | Processes pending input, timer, and system events. | [include/aroma_ui.h240-245](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L240-L245) |
| `aroma_ui_render` | Renders a specific window if dirty. | [include/aroma_ui.h254-272](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L254-L272) |
| `aroma_ui_render_all` | Renders all active windows. | [include/aroma_ui.h277-282](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L277-L282) |

## Window Management

| Function | Description | Source |
| --- | --- | --- |
| `aroma_ui_create_window` | Creates a new window with title and dimensions. | [include/aroma_ui.h341-356](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L341-L356) |
| `aroma_ui_destroy_window` | Destroys a window and frees resources. | [include/aroma_ui.h380-385](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L380-L385) |
| `aroma_ui_window_set_background` | Sets the window background clear color. | [include/aroma_ui.h392-397](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L392-L397) |
| `aroma_ui_window_set_visible` | Shows or hides a window. | [include/aroma_ui.h404-409](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L404-L409) |
| `aroma_ui_window_count` | Returns the number of managed windows. | [include/aroma_ui.h415-418](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L415-L418) |
| `aroma_ui_get_window_at` | Retrieves a window by index. | [include/aroma_ui.h425-430](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L425-L430) |

## Theming and Fonts

| Function | Description | Source |
| --- | --- | --- |
| `aroma_ui_set_theme` | Sets the global theme for all widgets. | [include/aroma_ui.h189-196](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L189-L196) |
| `aroma_ui_get_theme` | Returns the current global theme. | [include/aroma_ui.h202-205](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L202-L205) |
| `aroma_ui_load_font` | Loads a font from a filesystem path. | [include/aroma_ui.h290-308](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L290-L308) |
| `aroma_ui_unload_font` | Destroys a loaded font. | [include/aroma_ui.h314-321](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L314-L321) |
| `aroma_ui_prepare_font_for_window` | Binds a font to a window for rendering. | [include/aroma_ui.h323-328](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L323-L328) |

## Widget Factory Functions

### Layout and Navigation

| Helper | Underlying Factory | Description | Source |
| --- | --- | --- | --- |
| `aroma_ui_container` | `aroma_container_create` | Creates a container with flex/grid layout. | [include/aroma_ui.h602-620](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L602-L620) |
| `aroma_ui_scrollable_container` | `aroma_container_create` | Creates a scrollable flex container. | [include/aroma_ui.h636-656](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L636-L656) |
| `aroma_ui_listview` | `aroma_listview_create` | Creates a list view inside a scroll container. | [include/aroma_ui.h1043-1067](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1043-L1067) |
| `aroma_ui_table` | `aroma_table_create` | Creates a scrollable data table. | [include/aroma_ui.h1081-1104](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1081-L1104) |
| `aroma_ui_tabs` | `aroma_tabs_create` | Creates a horizontal tab bar. | [include/aroma_ui.h1171-1189](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1171-L1189) |
| `aroma_ui_tabs_with_icons` | `aroma_tabs_set_icon` | Creates tabs with icon support. | [include/aroma_ui.h1191-1214](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1191-L1214) |
| `aroma_ui_sidebar` | `aroma_sidebar_create` | Creates a vertical sidebar navigation. | [include/aroma_ui.h1280-1298](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1280-L1298) |
| `aroma_ui_sidebar_with_icons` | `aroma_sidebar_set_icon` | Creates a sidebar with icon support. | [include/aroma_ui.h1300-1323](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1300-L1323) |

### Input and Control

| Helper | Underlying Factory | Description | Source |
| --- | --- | --- | --- |
| `aroma_ui_button` | `aroma_button_create` | Creates a text button. | [include/aroma_ui.h514-538](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L514-L538) |
| `aroma_ui_button_with_icon` | `aroma_button_set_icon` | Creates a button with a leading icon. | [include/aroma_ui.h556-572](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L556-L572) |
| `aroma_ui_iconbutton` | `aroma_iconbutton_create` | Creates an icon-only button (FAB, toolbar). | [include/aroma_ui.h888-907](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L888-L907) |
| `aroma_ui_checkbox` | `aroma_checkbox_create` | Creates a checkbox with label. | [include/aroma_ui.h720-738](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L720-L738) |
| `aroma_ui_radiobutton` | `aroma_radiobutton_create` | Creates a radio button in a group. | [include/aroma_ui.h755-774](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L755-L774) |
| `aroma_ui_switch` | `aroma_switch_create` | Creates a toggle switch. | [include/aroma_ui.h789-804](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L789-L804) |
| `aroma_ui_slider` | `aroma_slider_create` | Creates a range slider. | [include/aroma_ui.h821-836](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L821-L836) |
| `aroma_ui_textbox` | `aroma_textbox_create` | Creates a single-line text input. | [include/aroma_ui.h852-872](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L852-L872) |
| `aroma_ui_dropdown` | `aroma_dropdown_create` | Creates a dropdown select input. | [include/aroma_ui.h1341-1367](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1341-L1367) |
| `aroma_ui_chip` | `aroma_chip_create` | Creates a filter/choice chip. | [include/aroma_ui.h922-940](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L922-L940) |
| `aroma_ui_chip_with_icon` | `aroma_chip_set_icon` | Creates a chip with a leading icon. | [include/aroma_ui.h942-959](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L942-L959) |

### Display and Feedback

| Helper | Underlying Factory | Description | Source |
| --- | --- | --- | --- |
| `aroma_ui_label` | `aroma_label_create` | Creates a text label. | [include/aroma_ui.h585-600](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L585-L600) |
| `aroma_ui_icon` | `aroma_icon_create` | Creates a standalone icon glyph. | [include/aroma_ui.h687-704](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L687-L704) |
| `aroma_ui_image` | `aroma_image_create` | Creates an image from a file path. | [include/aroma_ui.h658-664](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L658-L664) |
| `aroma_ui_image_mem` | `aroma_image_create_from_memory` | Creates an image from a memory buffer. | [include/aroma_ui.h666-673](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L666-L673) |
| `aroma_ui_card` | `aroma_card_create` | Creates a styled card container. | [include/aroma_ui.h972-978](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L972-L978) |
| `aroma_ui_progressbar` | `aroma_progressbar_create` | Creates a linear progress indicator. | [include/aroma_ui.h992-1004](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L992-L1004) |
| `aroma_ui_loading` | `aroma_loading_create` | Creates a spinning loader. | [include/aroma_ui.h135-141](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L135-L141) |
| `aroma_ui_divider` | `aroma_divider_create` | Creates a horizontal or vertical divider. | [include/aroma_ui.h1016-1022](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1016-L1022) |
| `aroma_ui_dialog` | `aroma_dialog_create` | Creates a modal dialog. | [include/aroma_ui.h1140-1154](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1140-L1154) |
| `aroma_ui_snackbar` | `aroma_snackbar_create` | Creates a transient bottom notification. | [include/aroma_ui.h1225-1237](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1225-L1237) |
| `aroma_ui_tooltip` | `aroma_tooltip_create` | Creates a floating tooltip. | [include/aroma_ui.h1250-1263](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1250-L1263) |
| `aroma_ui_menu` | `aroma_menu_create` | Creates a floating context menu. | [include/aroma_ui.h1115-1126](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1115-L1126) |
| `aroma_ui_debug_overlay` | `aroma_debug_overlay_create` | Creates a runtime debug overlay. | [include/aroma_ui.h1379-1390](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L1379-L1390) |

### Specialized Widgets

| Helper | Underlying Factory | Description | Source |
| --- | --- | --- | --- |
| `aroma_ui_map` | `aroma_map_create` | Creates an interactive OpenStreetMap widget. | [include/aroma_ui.h153-158](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L153-L158) |
| `aroma_ui_3d_viewer` | `aroma_3d_viewer_create` | Creates a 3D model viewer widget. | [include/widgets/aroma_3d_viewer.h14](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/widgets/aroma_3d_viewer.h#L14-L14) |

## Node Manipulation

| Function | Description | Source |
| --- | --- | --- |
| `aroma_node_set_hidden` | Shows or hides a node and its subtree. | [src/core/aroma_node.c1012](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_node.c#L1012-L1012) |
| `aroma_node_set_z_index` | Sets the draw order priority. | [include/aroma_node.h95-98](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_node.h#L95-L98) |
| `aroma_node_set_layout_mode` | Sets container layout mode (NONE, FLEX, GRID). | [src/core/aroma_layout.c59-61](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_layout.c#L59-L61) |
| `aroma_node_set_layout_fill` | Makes a node fill its parent bounds. | [docs/ui/layouts.md41-54](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/docs/ui/layouts.md#L41-L54) |
| `aroma_node_set_layout_center` | Centers a node within its parent. | [docs/ui/layouts.md58-71](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/docs/ui/layouts.md#L58-L71) |
| `aroma_node_set_layout_anchor` | Pins a node to parent edges. | [docs/ui/layouts.md75-100](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/docs/ui/layouts.md#L75-L100) |
| `aroma_node_invalidate` | Marks a node as dirty for the next render. | [src/core/aroma_ui_impl.c221](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_ui_impl.c#L221-L221) |
| `aroma_node_set_draw_cb` | Assigns a custom draw callback to a node. | [include/aroma_node.h160-165](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_node.h#L160-L165) |

## Animation

| Function | Description | Source |
| --- | --- | --- |
| `aroma_animation_start` | Starts a standard property animation. | [src/core/aroma_animation.c121-142](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_animation.c#L121-L142) |
| `aroma_animation_start_custom` | Starts a custom animation with a user callback. | [src/core/aroma_animation.c155-162](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_animation.c#L155-L162) |
| `aroma_animation_stop` | Stops all animations on a target node. | [src/core/aroma_animation.c144-152](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_animation.c#L144-L152) |

### Animation Types

| Type | Property Modified |
| --- | --- |
| `AROMA_ANIM_SLIDE_X` | `node->rect.x` |
| `AROMA_ANIM_SLIDE_Y` | `node->rect.y` |
| `AROMA_ANIM_SCALE_X` | `node->rect.width` |
| `AROMA_ANIM_SCALE_Y` | `node->rect.height` |
| `AROMA_ANIM_FADE` | `node->opacity` |
| `AROMA_ANIM_CUSTOM` | User-defined callback |

### Easing Functions

| Easing | Description |
| --- | --- |
| `AROMA_EASE_LINEAR` | Constant speed. |
| `AROMA_EASE_IN_QUAD` | Quadratic acceleration. |
| `AROMA_EASE_OUT_QUAD` | Quadratic deceleration. |
| `AROMA_EASE_IN_OUT_QUAD` | Quadratic acceleration then deceleration. |
| `AROMA_EASE_OUT_CUBIC` | Smooth deceleration (default). |
| `AROMA_EASE_OUT_BACK` | Slight overshoot before settling. |
| `AROMA_EASE_OUT_ELASTIC` | Damped oscillation effect. |

**Sources:**[include/aroma_animation.h10-28](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_animation.h#L10-L28)[src/core/aroma_animation.c45-78](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_animation.c#L45-L78)

## Event System

| Function | Description | Source |
| --- | --- | --- |
| `aroma_event_subscribe` | Registers a listener for a node and event type. | [src/core/aroma_event.c231-260](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L231-L260) |
| `aroma_event_unsubscribe` | Removes a listener. | [src/core/aroma_event.c261-280](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L261-L280) |
| `aroma_event_create_mouse` | Creates a synthetic mouse event. | [src/core/aroma_event.c290-320](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L290-L320) |
| `aroma_event_queue` | Queues an event for processing. | [src/core/aroma_event.c510-530](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L510-L530) |
| `aroma_event_process_queue` | Drains the event queue. | [src/core/aroma_event.c510-530](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L510-L530) |

### Event Types

| Event | Description |
| --- | --- |
| `EVENT_TYPE_MOUSE_MOVE` | Pointer moved. |
| `EVENT_TYPE_MOUSE_CLICK` | Primary button pressed. |
| `EVENT_TYPE_MOUSE_RELEASE` | Primary button released. |
| `EVENT_TYPE_MOUSE_SCROLL` | Scroll wheel moved. |
| `EVENT_TYPE_MOUSE_ENTER` | Pointer entered node bounds. |
| `EVENT_TYPE_MOUSE_EXIT` | Pointer left node bounds. |
| `EVENT_TYPE_KEY_PRESS` | Keyboard key pressed. |
| `EVENT_TYPE_KEY_RELEASE` | Keyboard key released. |
| `EVENT_TYPE_TOUCH_DOWN` | Touch contact began. |
| `EVENT_TYPE_TOUCH_MOVE` | Touch contact moved. |
| `EVENT_TYPE_TOUCH_UP` | Touch contact ended. |
| `EVENT_TYPE_TOUCH_CANCEL` | Touch contact was cancelled. |

**Sources:**[include/aroma_event.h40-80](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_event.h#L40-L80)[src/core/aroma_event.c146-160](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_event.c#L146-L160)

## Android Platform

| Function | Description | Source |
| --- | --- | --- |
| `aroma_android_set_app` | Sets the Android app state for JNI. | [src/core/aroma_ui_impl.c31-39](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_ui_impl.c#L31-L39) |
| `aroma_ui_android_intent` | Sends an Android Intent (VIEW, SEND, DIAL, CALL). | [include/aroma_ui.h490-496](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_ui.h#L490-L496) |

**Sources:**[include/aroma_android.h89-204](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_android.h#L89-L204)
