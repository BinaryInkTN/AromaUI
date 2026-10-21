
Visual and informational widgets: labels, images, cards, charts, progress indicators, snackbars, and dialogs.

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
aroma_image_set_corner_radius(img, 12.0f);  // rounded corners, 0 = square
aroma_gif_play(gif_node);  // start animation
```

Images are loaded via stb_image and cached as GPU textures. GIFs maintain a frame timer for automatic playback.

### Rounded Corners

Both widgets clip with anti-aliased rounded rectangles (GLES3; other
backends draw square):

```c
aroma_image_set_corner_radius(img, 16.0f);
aroma_image_get_corner_radius(img);  // current radius in px
aroma_gif_set_corner_radius(gif, 16.0f);
```

- Radius is in pixels and clamps to `[0, min(width, height) / 2]`, so
  `corner_radius: 30` on a 60x60 avatar makes a perfect circle.
- In Incense: `Image { src: "..." corner_radius: 12 }`
  (`radius:` works as an alias). Remote `https://` URLs load
  asynchronously on web builds (e.g. Pexels photos) and pop in when
  ready; place a `Card` behind the `Image` as a placeholder.

### Scale Modes

Like iOS contentMode / Android scaleType / CSS object-fit, the source
pixels can map onto the widget rectangle three ways:

```c
aroma_image_set_scale_mode(img, AROMA_IMAGE_SCALE_COVER);
aroma_image_get_scale_mode(img);
```

| Mode | Behavior |
|---|---|
| `AROMA_IMAGE_SCALE_FILL` (default) | Stretch to fill; may distort. |
| `AROMA_IMAGE_SCALE_FIT` | Whole image visible, aspect kept, centered (works on every backend). |
| `AROMA_IMAGE_SCALE_COVER` | Fills the rect, aspect kept, center-cropped (needs GLES3/Vulkan; others fall back to fill). |

In Incense: `Image { src: "..." scale: cover }`. Avatars pair
`scale: cover` with `corner_radius` set to half the size for round
cropped portraits; full-bleed feed photos use `scale: cover` with
square corners.

```aroma
Image {
    x: 32
    y: 88
    width: 256
    height: 128
    src: "https://images.pexels.com/photos/1640777/pexels-photo-1640777.jpeg?auto=compress&cs=tinysrgb&w=600"
    corner_radius: 12
    scale: cover
}
```

## Gallery

A photo library app like Apple Photos, fully tappable: a full-bleed
3-column grid of square `cover` thumbnails with 2px gutters, a
`Years | Months | Days | All Photos` switcher, and a bottom tab bar
(`Library`, `For You`, `Albums`, `Search`) whose tabs switch screens.

The switcher is a `SegmentedControl`: a single-select pill group where
tapping a segment slides the thumb and fires `on_change`:

```c
static const char *views[] = {"Years", "Months", "Days", "All Photos"};
AromaNode *seg = aroma_ui_segmented(root, 20, 72, 280, 32,
                                    views, 4, on_view_change, NULL, font);
aroma_segmented_set_selected(seg, 3);
```

```aroma
SegmentedControl {
    x: 20
    y: 80
    width: 280
    height: 30
    selected: 3
    Segment { text: "Years" }
    Segment { text: "Months" }
    Segment { text: "Days" }
    Segment { text: "All Photos" }
}
```

The tab bar is `Tabs` in the `bar` variant: icons stack above their
labels, the active tab tints, and tapping a tab swaps its content
screens in and out:

```c
static const char *tabs[] = {"Library", "For You", "Albums", "Search"};
static const char *icons[] = {AROMA_ICON_PHOTO_LIBRARY, AROMA_ICON_FAVORITE,
                              AROMA_ICON_PHOTO_ALBUM, AROMA_ICON_SEARCH};
AromaNode *bar = aroma_ui_tabbar(root, 0, 424, 320, 56, tabs, icons, 4,
                                 on_tab_change, NULL, font, icon_font);
```

```aroma
Tabs {
    x: 0
    y: 424
    width: 320
    height: 56
    variant: bar
    Tab {
        text: "Library"
        icon: "AROMA_ICON_PHOTO_LIBRARY"
        // ... grid content, shown while selected
    }
    Tab {
        text: "Albums"
        icon: "AROMA_ICON_PHOTO_ALBUM"
        // ... album content
    }
}
```

Square cells with `AROMA_IMAGE_SCALE_COVER` center-crop every source to
fill its tile, so mixed portrait and landscape photos form a uniform grid
with no distortion:

```c
for (int i = 0; i < 9; i++) {
    int col = i % 3, row = i / 3;
    AromaNode *img = aroma_ui_image(root, photos[i],
        col * 107, 112 + row * 107, 105, 105);
    aroma_image_set_scale_mode(img, AROMA_IMAGE_SCALE_COVER);
}
```

```incense-demo gallery
```

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

### Frosted Glass

Glass cards blur whatever was rendered behind them (backdrop blur) before
drawing their translucent tint, so they read as frosted glass instead of
flat translucency:

```c
AromaNode *glass = aroma_ui_frosted_card(root, 20, 20, 300, 100, 14.0f);
aroma_card_set_backdrop_blur(glass, 20.0f); /* retune, 0 disables */
```

In Incense, use `type: glass` with an optional `blur_radius` (default 14px):

```aroma
Card {
    x: 20
    y: 368
    width: 280
    height: 88
    type: glass
    blur_radius: 16
}
```

Draw the glass card after the content it should blur (later siblings draw
on top): a sheet over a map, a pill over a photo, or a sticky header over
a scrolling feed.

- Blur radius is in pixels. Glass cards default to a 14px blur; other card
  types default to 0 (no blur).
- Backdrop blur needs graphics-backend support (GLES3 implements it; query
  with `aroma_graphics_supports_backdrop_blur()`). Unsupported backends
  skip the blur and still draw the tint, so glass degrades gracefully.

### Corner Radius

Cards accept an optional `corner_radius` to override the default corner
rounding, which is useful for building device frames and large panels:

```aroma
Card {
    x: 32
    y: 24
    width: 1216
    height: 752
    type: outlined
    color: #0A0F1A
    corner_radius: 40
}
```

```c
aroma_card_set_corner_radius(frame, 40.0f); /* dp; values below 0 clamp to 0 */
```

Omitting the prop leaves the theme's default card radius.

### Hover and Press Feedback

Cards are decorative by default. Opt in to interaction to make a card
lighten while the pointer is over it and darken while it is held, which is
what a tappable panel or a selectable row should do:

```aroma
Card {
    x: 24
    y: 96
    width: 320
    height: 180
    type: filled
    color: #1C1C1E
    corner_radius: 22
    interactive: true
}
```

```c
aroma_card_set_states(row, 0.06f, -0.10f); /* hover: +6%, press: -10% */
```

- `interactive: true` uses the default factors (+6% on hover, -10% on
  press). Set `hover_lift` and `press_lift` to tune them; both are relative
  adjustments applied per channel by `aroma_color_adjust`.
- Cards without `interactive` never subscribe to pointer events, so
  decorative panels cost nothing.
- A card only receives hover while its own surface is the topmost hit.
  Content drawn over a card (a chart, a label covering the whole face)
  takes the pointer first, so expect the effect on the exposed parts.

<div align="center"><img src="__ASSETS__/divider.svg" width="100%"></div>
- The blur runs in draw order between background and foreground, and honors
  the card's rounded corners.
- One backdrop snapshot is shared per frame: stacked glass surfaces all
  blur the same backdrop, so a sheet over a card (or pill over drawer)
  adds tint without re-blurring and darkening the glass below. The snapshot
  is captured on first use each frame, so blur is correct from the very
  first frame, and is sampled mipmapped for a wide, smooth falloff.

Cards also accept explicit colours, which is how dashboards mix tinted
stat tiles and dark panels without changing the app theme. A lone `color`
paints both fill and border; add `border_color` for an outlined panel:

```aroma
Card {
    x: 20
    y: 20
    width: 220
    height: 96
    type: outlined
    color: #0F1A2A
    border_color: #1E2B40
}
```

## Progress Indicators

```c
AromaNode *bar = aroma_ui_progressbar(root, 20, 200, 260, 4, PROGRESS_TYPE_DETERMINATE, 0.75f);
AromaNode *gauge = aroma_ui_gauge(root, 300, 20, 120, 120);
aroma_gauge_set_value(gauge, 0.6f);
```

- ProgressBar: linear, 0.0 to 1.0 value
- Gauge: circular arc with needle
- Tint them per instance with `track_color` + `indicator_color`
  (ProgressBar) or `track_color` + `fill_color` (Gauge); anything left
  unset follows the theme.
- Gauge angles (`start_angle`, `end_angle`) are in **radians**, matching
  the Canvas arc: `-1.5708` is 12 o'clock and `4.71239` is 9 o'clock, so
  `start_angle: -1.5708, end_angle: 4.71239` is a full ring.
- `ring_inset` (default `50`) is the gap between the widget edge and the
  arc. Set `ring_inset: 0` to push the ring out to the widget bounds —
  which is what you want for donuts and multi-segment rings, where
  several Gauges sharing one rect each draw a slice:

```aroma
Gauge {
    x: 0
    y: 0
    width: 140
    height: 140
    value: 1
    start_angle: -1.5708
    end_angle: 4.71239
    track_thickness: 14
    fill_thickness: 0
    ring_inset: 0
    track_color: #1E293B
}
```

## Charts

A line chart over a series of readings, with the axes labelled by the
widget itself:

```c
AromaNode *chart = aroma_ui_chart(root, 24, 24, 280, 120);
aroma_chart_set_series_csv(chart, NULL, "0.6,0.9,1.4,1.2,0.8,0.7,1.1");
aroma_chart_set_range(chart, true, 0, 1, false, 0, 3);
aroma_chart_set_tick_labels(chart, "6a,9a,12p,3p", "0,1,2,3");
aroma_chart_set_line_thickness(chart, 2.0f);
```

```aroma
Chart {
    x: 24
    y: 24
    width: 280
    height: 120
    size: 12
    data: "0.6,0.9,1.4,1.2,0.8,0.7,1.1,1.0,0.9,1.3,1.9,2.4,2.8"
    x_labels: "6a,9a,12p,3p,6p"
    y_labels: "0,1,2,3"
    y_min: 0
    y_max: 3
    pad_left: 22
    pad_bottom: 20
}
```

The series is rasterised by vendored [pbPlots](https://github.com/InductiveComputerScience/pbPlots)
(MIT, C99, no dependencies) at device density, then keyed: pbPlots' white
canvas and its black chrome are cut to transparent, so only the line is
uploaded and whatever sits behind the chart shows through.

- Axis furniture — gridlines, frame, tick labels — is drawn by AromaUI
  itself, so it stays crisp at any density and follows the theme. Leave
  `pad_left`, `pad_top`, `pad_right`, `pad_bottom` (defaults 34/8/10/22)
  room for the labels you ask for.
- Anything you do not set follows the theme, and is re-read on every
  draw: `line_color` defaults to the theme primary, `grid_color` to a
  blend of border and surface, `frame_color` to the border and
  `text_color` to the secondary text colour. A theme switch repaints the
  chart along with the rest of the UI.
- Each axis fits the data with a little headroom unless you give an
  explicit range (`aroma_chart_set_range`, or `x_min`/`x_max`/`y_min`/
  `y_max`). Use one when labels must line up with values: ticks are
  spread evenly across the axis, so `0,1,2,3` over `y_min: 0 y_max: 3`
  lands exactly on the gridlines.
- Tick labels are comma-separated free text (`"Mon,Tue"`, `"6a,9a"`),
  unlike `data` and `x_data`/`y_data` which are numbers.
- Use `aroma_chart_set_series` when the x values are not evenly spaced,
  and `aroma_chart_set_furniture` to drop the grid or the frame.

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

## Live Demonstration

```incense-demo cards
```

```incense-demo gauge
```

## Next Steps

- Learn [Input & Controls](Input-and-Control-Widgets.md) for interactive widgets.
- Explore [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Try [Incense](Incense-Sandbox.md) for rapid UI prototyping.
