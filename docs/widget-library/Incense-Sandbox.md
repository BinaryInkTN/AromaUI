
Incense is AromaUI's declarative language for building UIs. Write widget trees in a clean syntax and see results instantly in the browser sandbox.

## Open the Sandbox

**[Launch Incense Sandbox](../widget-library/wasm/incense_sandbox/index.html)**

Edit code on the left, click **Run**, and see the preview update instantly via WebAssembly.

## Syntax

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
| `Button` | `text`, `x`, `y`, `width`, `height`, `on_click` | - |
| `Label` | `text`, `x`, `y`, `style`, `color` | - |
| `Container` | `x`, `y`, `width`, `height`, `layout`, `direction` | Any widget |
| `Card` | `x`, `y`, `width`, `height`, `type` | - |
| `Checkbox` | `label`, `x`, `y`, `checked`, `on_change` | - |
| `Switch` | `x`, `y`, `width`, `height`, `value`, `on_change` | - |
| `Slider` | `x`, `y`, `width`, `height`, `min`, `max`, `value`, `on_change` | - |
| `Textbox` | `x`, `y`, `width`, `height`, `placeholder`, `on_change` | - |
| `ProgressBar` | `x`, `y`, `width`, `height`, `value` | - |
| `Divider` | `x`, `y`, `length`, `orientation` | - |
| `IconButton` | `x`, `y`, `width`, `height`, `icon`, `on_click` | - |
| `Icon` | `x`, `y`, `size`, `text`, `color` | - |
| `Image` | `x`, `y`, `width`, `height`, `src` | - |
| `Dropdown` | `x`, `y`, `width`, `height`, `on_change` | `Option` |
| `RadioButton` | `label`, `x`, `y`, `width`, `height`, `group`, `checked`, `on_click` | - |
| `Tabs` | `x`, `y`, `width`, `height`, `on_change` | `Tab` |
| `Sidebar` | `x`, `y`, `width`, `height` | `Item` |
| `Menu` | `x`, `y`, `width` | `MenuItem` |
| `Chip` | `x`, `y`, `label`, `icon`, `type`, `selected` | - |
| `Tooltip` | `text`, `x`, `y`, `position` | - |
| `GIF` | `x`, `y`, `width`, `height`, `src`, `autoplay` | - |
| `Loading` | `x`, `y`, `radius`, `thickness` | - |
| `Gauge` | `x`, `y`, `width`, `height`, `value` | - |
| `Canvas` | `x`, `y`, `width`, `height` | - |
| `DebugOverlay` | `x`, `y`, `width`, `height`, `visible` | - |
| `Map` | `x`, `y`, `width`, `height`, `lat`, `lon`, `zoom` | - |
| `Snackbar` | `message`, `duration`, `action`, `on_click` | - |
| `ListView` | `x`, `y`, `width`, `height`, `on_select` | `ListItem`, `Header`, `Separator` |
| `Dialog` | `title`, `message`, `width`, `height`, `type` | - |
| `ScrollView` | `x`, `y`, `width`, `height`, `direction` | Any widget |
| `Table` | `x`, `y`, `width`, `height` | `Column`, `Row`, `HeaderCell` |

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

## Known Limitations

1. No arrays/lists as property values - use child objects instead
2. No control flow - all widgets render every frame
3. No data binding - properties are static literals
4. No expressions - cannot compute values from other values
5. No component system - no user-defined widgets yet
6. No declarative animations
7. Limited event types - `on_click`, `on_change`, `on_select`, `on_submit`
8. Single window per file
9. No string interpolation
10. Hard limits: 64 children per parent, 64 properties per widget

## What's Next

- Learn the underlying C APIs in [Widget Library](Layout-and-Navigation-Widgets.md).
- Explore [Core Framework](Scene-Graph-and-Node-System.md) to understand how Incense maps to `AromaNode`.
- Try the [interactive sandbox](../widget-library/wasm/incense_sandbox/index.html).
