
Incense is the AromaUI language for describing UI in plain text. Write widgets, click Run, and see the result in the browser at once.

## Open the Sandbox

**[Launch Incense Sandbox](https://binaryinktn.github.io/AromaUI/sandbox.html)**

Edit code on the left, click **Run**, and see the preview update at once with WebAssembly.

## Syntax

A container with `layout: flex` positions its children itself. Inside a flex
container, a child keeps its size but its `x` and `y` are set by the flex
row or column, so explicit `x` and `y` on direct children are ignored. Use a
plain container (no `layout`) when you want exact `x` and `y` positions.

```aroma
Window {
    width: 320
    height: 480
    title: "My App"

    Container {
        x: 0
        y: 0
        width: 320
        height: 480
        layout: flex
        direction: column

        Label {
            text: "Hello"
            style: large
            color: #333333
        }

        Button {
            text: "Click Me"
            x: 20
            y: 100
            width: 120
            height: 40
            on_click: "handle_click"
        }
    }
}
```

## Supported Widgets

| Widget | Key Properties | Children |
|---|---|---|
| `Window` | `width`, `height`, `title` | Any widget |
| `Button` | `text`, `x`, `y`, `width`, `height`, `on_click` | - |
| `Label` | `text`, `x`, `y`, `style`, `color` | - |
| `Container` | `x`, `y`, `width`, `height`, `layout`, `direction` | Any widget |
| `Card` | `x`, `y`, `width`, `height`, `type` | - |
| `Checkbox` | `label`, `x`, `y`, `width`, `height`, `checked`, `on_change` | - |
| `Switch` | `x`, `y`, `width`, `height`, `value`, `on_change` | - |
| `Slider` | `x`, `y`, `width`, `height`, `min`, `max`, `value`, `on_change` | - |
| `Textbox` | `x`, `y`, `width`, `height`, `placeholder`, `on_change`, `on_submit` | - |
| `ProgressBar` | `x`, `y`, `width`, `height`, `value`, `type` | - |
| `Divider` | `x`, `y`, `length`, `orientation` | - |
| `IconButton` | `x`, `y`, `size`, `icon`, `variant`, `on_click` | - |
| `Icon` | `x`, `y`, `size`, `text`, `src`, `color` | - |
| `Image` | `x`, `y`, `width`, `height`, `src` | - |
| `Dropdown` | `x`, `y`, `width`, `height`, `max_visible`, `on_change` | `Option` |
| `RadioButton` | `label`, `x`, `y`, `width`, `height`, `group`, `selected`, `on_click` | - |
| `Tabs` | `x`, `y`, `width`, `height`, `selected`, `on_change` | `Tab` |
| `Sidebar` | `x`, `y`, `width`, `height`, `on_select` | `Item` |
| `Menu` | `x`, `y` | `MenuItem`, `Separator` |
| `Chip` | `x`, `y`, `label`, `icon`, `type`, `selected` | - |
| `Tooltip` | `text`, `x`, `y`, `position` | - |
| `GIF` | `x`, `y`, `width`, `height`, `src`, `autoplay` | - |
| `Loading` | `x`, `y`, `radius`, `thickness`, `color` | - |
| `Gauge` | `x`, `y`, `width`, `height`, `value`, `min`, `max` | - |
| `Canvas` | `x`, `y`, `width`, `height` | - |
| `DebugOverlay` | `x`, `y`, `width`, `visible` | - |
| `Map` | `x`, `y`, `width`, `height`, `lat`, `lon`, `zoom`, `attribution` | `Marker` |
| `Marker` | `lat`, `lon`, `color`, `popup`, `icon` | - |
| `ThreeDViewer` | `x`, `y`, `width`, `height`, `model`, `auto_rotate`, `interactive` | - |
| `Snackbar` | `message`, `duration`, `action`, `on_click`, `show` | - |
| `ListView` | `x`, `y`, `width`, `height`, `on_select` | `ListItem`, `Header`, `Separator` |
| `Dialog` | `title`, `message`, `width`, `height`, `type` | `DialogAction`, `Action` |
| `DialogAction` | `text`, `label`, `on_click` | - |
| `ScrollView` | `x`, `y`, `width`, `height`, `direction` | Any widget |
| `Table` | `x`, `y`, `width`, `height`, `columns` | `Column` |

Every widget also accepts `id`, `font`, `visible`, `z_index`, `parent`, and the
animation props `animation`, `animation_duration`, `animation_start_val`,
`animation_end_val`, `animation_easing`, `animation_loop` (`true` restarts
each cycle, `pingpong` reverses direction for a seamless loop). `Card` types are
`elevated`/`outlined`/`filled`; `RadioButton` uses `selected` (not `checked`);
`IconButton` is sized with `size` (not `width`/`height`); `Table` columns are
`Column { header, width }` items.

## Property Types

| Type | Example | Notes |
|---|---|---|
| Integer | `x: 20` | Whole numbers |
| Float | `value: 0.5` | Decimal numbers |
| Boolean | `checked: true` | `true` or `false` |
| Color | `color: #FF0000` | Hex color |
| String | `text: "Hello"` | Double-quoted |
| Enum | `style: large` | Unquoted keyword |
| Object | `Container { ... }` | Nested widget |

## Special Features

- **Callbacks**: Reference C-registered callbacks: `on_click: "handle_click"`
- **Embeds**: Include other files: `@embed "shared.aroma"`
- **Comments**: Single-line with `//`
- **List children**: `ListItem { text: "..." secondary: "..." }` for ListView
- **Navigation**: Use `visible: 0/1` to show/hide pages and `on_click: "back"` for back navigation
- **Icons**: Material icon names: `AROMA_ICON_*` constants for `IconButton` and `Icon` widgets
- **Virtual Keyboard**: Textbox widgets automatically show a responsive virtual keyboard when focused on Emscripten/WebAssembly builds. The keyboard anchors at the bottom of the canvas, keys scale to fit screen width, and labels abbreviate when space is limited. Enable it programmatically with `aroma_textbox_enable_virtual_keyboard(root, true)`.

## Capabilities and Limits

1. No arrays/lists as property values - use child objects instead (`Option`, `Marker`, `Column`, `Tab`, `Item`).
2. Conditional UI via `If { condition: "state.key == value" }` with `Then`/`Else` branches. Conditions compare `state.*` values with `==`, `!=`, `>`, `<`, `>=`, `<=`, and re-evaluate when state changes.
3. Data binding via `state.*` property values (e.g. `value: state.volume`), driven from C with `IncenseStateSetInt/Float/Bool/String`.
4. No arithmetic or string interpolation - conditions support comparisons only, and values substitute whole `state.*` / `embed_*` references.
5. No user-defined widget types - reuse files with `@embed "shared.aroma"` instead.
6. Declarative animations on any widget via `animation` (`fade`, `slide_x`, `slide_y`, `scale_x`, `scale_y`) plus `animation_duration`, `animation_start_val`, `animation_end_val`, `animation_easing`.
7. Limited event types - `on_click`, `on_change`, `on_select`, `on_submit` (Textbox only).
8. One `Window` root per file; hosts can mount several documents into one parent.
9. Hard limits: 64 properties per widget, 64 items per `Option`/`Marker`/`Column` list, 128 child nodes per parent.

## Emscripten Build

For WebAssembly builds, the following must be included in your `CMakeLists.txt`:

- **`aroma_textbox.c`** must be added to the executable's source list (it is not part of the default library link on Emscripten)
- `aroma_textbox_enable_virtual_keyboard` and related functions must be listed in `EXPORTED_FUNCTIONS`
- `aroma_textbox.c` uses `__EMSCRIPTEN__` guards for web-only VK code paths

Example CMakeLists.txt configuration:
```cmake
add_executable(my_app main.c ../../src/widgets/aroma_textbox.c)

target_link_options(my_app PRIVATE
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free',...,'_aroma_textbox_enable_virtual_keyboard','_aroma_textbox_show_virtual_keyboard','_aroma_textbox_hide_virtual_keyboard']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','addRunDependency','removeRunDependency']"
)
```

## Sample: Wi-Fi Connect Page

The Incense Sandbox includes a sample iOS-style Wi-Fi connection page demonstrating multi-page navigation with text input:

```aroma
Container {
    id: "page_wifi_connect"
    x: 0
    y: 0
    width: 320
    height: 480
    layout: flex
    direction: column
    visible: 0

    Container {
        x: 0
        y: 0
        width: 320
        height: 56
        color: #121212

        IconButton {
            x: 8
            y: 8
            width: 40
            height: 40
            icon: "AROMA_ICON_ARROW_BACK"
            variant: standard
            on_click: "back"
        }

        Label {
            text: "Wi-Fi"
            style: large
            color: #FFFFFF
            y: 20
            x: 60
        }
    }

    Container {
        x: 20
        y: 70
        width: 280
        height: 340

        Textbox {
            text: ""
            placeholder: "Network Name (SSID)"
            x: 20
            y: 30
            width: 240
            height: 44
        }

        Textbox {
            text: ""
            placeholder: "Password"
            x: 20
            y: 90
            width: 240
            height: 44
        }

        Button {
            text: "Connect to Network"
            x: 40
            y: 170
            width: 200
            height: 44
            on_click: "wifi_connect"
        }
    }
}

## What's Next

- Learn the C APIs in [Widget Library](Layout-and-Navigation-Widgets.md).
- Read [Core Framework](../core-framework/Scene-Graph-and-Node-System.md) to see how Incense maps to `AromaNode`.
- Try the [live sandbox](../sandbox.html).
