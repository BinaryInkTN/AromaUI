
Visual and informational widgets: labels, images, cards, progress indicators, snackbars, and dialogs.

## Labels and Icons

```c
AromaNode *label = aroma_ui_label(root, "Hello World", 20, 20, LABEL_STYLE_LARGE, font);
AromaNode *icon = aroma_ui_icon(root, AROMA_ICON_HOME, 100, 20, 32, 0xFF0000);
```

- `LABEL_STYLE_LARGE`, `LABEL_STYLE_MEDIUM`, `LABEL_STYLE_SMALL`
- Icons render a single glyph from the icon font

## Images and GIFs

```c
AromaNode *img = aroma_ui_image(root, "assets/photo.png", 0, 0, 200, 150);
aroma_gif_play(gif_node);  // start animation
```

Images are loaded via stb_image and cached as GPU textures. GIFs maintain a frame timer for automatic playback.

## Cards

```c
AromaNode *card = aroma_ui_card(root, 20, 20, 300, 100, CARD_TYPE_ELEVATED);
```

| Type | Description |
|---|---|
| `CARD_TYPE_FILLED` | Solid background blended with primary color |
| `CARD_TYPE_TONAL` | Subtle variant of filled |
| `CARD_TYPE_GLASS` | Translucent with glossy edge |
| `CARD_TYPE_OUTLINED` | Hollow with border |

## Progress Indicators

```c
AromaNode *bar = aroma_ui_progressbar(root, 20, 200, 260, 4, 0.75f);
AromaNode *gauge = aroma_ui_gauge(root, 300, 20, 120, 120, 0.6f);
```

- ProgressBar: linear, 0.0–1.0 value
- Gauge: circular arc with needle

## Snackbar

```c
aroma_snackbar_show(window, "Item deleted", 3000, "UNDO", on_undo_callback);
```

Snackbars appear at the bottom with an optional action button. They auto-dismiss after the duration (ms).

## Dialog

```c
AromaNode *dialog = aroma_ui_dialog(root, "Confirm", "Delete this item?", 320, 180);
aroma_dialog_add_action(dialog, "Cancel", DIALOG_ACTION_CANCEL, on_cancel);
aroma_dialog_add_action(dialog, "Delete", DIALOG_ACTION_DESTRUCTIVE, on_delete);
```

Dialogs are modal overlays with up to 3 action buttons.

## What's Next

- Learn [Input & Controls](Input-and-Control-Widgets.md) for interactive widgets.
- Explore [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Try [Incense](../widget-library/wasm/incense_sandbox/index.html) for rapid UI prototyping.
