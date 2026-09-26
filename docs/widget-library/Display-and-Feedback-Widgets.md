
Visual and informational widgets: labels, images, cards, progress indicators, snackbars, and dialogs.

## Labels and Icons

```c
AromaNode *label = aroma_ui_label(root, "Hello World", 20, 20, LABEL_STYLE_LABEL_LARGE, font);
AromaNode *icon = aroma_ui_icon(root, AROMA_ICON_HOME, 100, 20, 32, 0xFF0000, font);
```

- `LABEL_STYLE_LABEL_LARGE`, `LABEL_STYLE_LABEL_MEDIUM`, `LABEL_STYLE_LABEL_SMALL`
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
| `CARD_TYPE_ELEVATED` | Raised card with shadow |
| `CARD_TYPE_FILLED` | Solid background blended with primary color |
| `CARD_TYPE_GLASS` | Frosted glass: blurs the backdrop in place, then draws a translucent tint with glossy edge |
| `CARD_TYPE_OUTLINED` | Hollow with border |

### Frosted glass

Glass cards blur whatever was rendered behind them (backdrop blur) before
drawing their translucent tint, so they read as frosted glass instead of
flat translucency:

```c
AromaNode *glass = aroma_ui_frosted_card(root, 20, 20, 300, 100, 14.0f);
aroma_card_set_backdrop_blur(glass, 20.0f); /* retune, 0 disables */
```

- Blur radius is in pixels. Glass cards default to a 14px blur; other card
  types default to 0 (no blur).
- Backdrop blur needs graphics-backend support (GLES3 implements it; query
  with `aroma_graphics_supports_backdrop_blur()`). Unsupported backends
  skip the blur and still draw the tint, so glass degrades gracefully.
- The blur runs in draw order between background and foreground, and honors
  the card's rounded corners.
- One backdrop snapshot is shared per frame: stacked glass surfaces all
  blur the same backdrop, so a sheet over a card (or pill over drawer)
  adds tint without re-blurring and darkening the glass below. The snapshot
  is captured on first use each frame, so blur is correct from the very
  first frame, and is sampled mipmapped for a wide, smooth falloff.

## Progress Indicators

```c
AromaNode *bar = aroma_ui_progressbar(root, 20, 200, 260, 4, PROGRESS_TYPE_DETERMINATE, 0.75f);
AromaNode *gauge = aroma_ui_gauge(root, 300, 20, 120, 120);
aroma_gauge_set_value(gauge, 0.6f);
```

- ProgressBar: linear, 0.0 to 1.0 value
- Gauge: circular arc with needle

## Snackbar

```c
AromaNode *bar = aroma_snackbar_create(root, "Item deleted", 3000);
aroma_snackbar_set_action(bar, "UNDO", on_undo_callback, NULL);
aroma_snackbar_show(bar);
```

Snackbars appear at the bottom with an optional action button. They auto-dismiss after the duration (ms).

## Dialog

```c
AromaNode *dialog = aroma_ui_dialog(root, "Confirm", "Delete this item?",
                                    320, 180, DIALOG_TYPE_BASIC, font);
aroma_dialog_add_action(dialog, "Cancel", on_cancel, NULL);
aroma_dialog_add_action(dialog, "Delete", on_delete, NULL);
aroma_dialog_show(dialog);
```

Dialogs are modal overlays with up to 3 action buttons.

In Incense, declare actions as `DialogAction` children (bare `Action` works too):

```aroma
Dialog {
    title: "Delete item?"
    message: "This cannot be undone."
    width: 280
    height: 200
    DialogAction { text: "Cancel" }
    DialogAction { text: "Delete" }
}
```

```incense-demo dialog
```

## Live demo

```incense-demo cards
```

```incense-demo gauge
```

## What's Next

- Learn [Input & Controls](Input-and-Control-Widgets.md) for interactive widgets.
- Explore [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Try [Incense](Incense-Sandbox.md) for rapid UI prototyping.
