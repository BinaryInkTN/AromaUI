Line, scatter, line-plus-points, and grouped bar charts, rasterised by the vendored [pbPlots](https://github.com/InductiveComputerScience/pbPlots) library and themed by AromaUI.

Every plot AromaUI can draw is on this page. pbPlots exposes two primitives, a scatter plot and a bar plot, and the four chart types below are the useful combinations of those primitives. There is no pie, histogram, heatmap, or stacked bar in pbPlots, so the chart widget does not pretend to have one.

## Live Demo

Press **Shuffle** in the preview: it replaces the data behind all four charts and the widget re-rasterises them on the next frame. These are real plots, not images.

```incense-demo plots
```

## Plot Types

| `type` | C enum | What is drawn |
|---|---|---|
| `line` | `AROMA_CHART_LINE` | A polyline through the points. |
| `scatter` | `AROMA_CHART_SCATTER` | Markers only, no connecting line. |
| `line_points` | `AROMA_CHART_LINE_POINTS` | A polyline plus markers at every point. |
| `bar` | `AROMA_CHART_BAR` | Grouped bars: one group per data index, one bar per series in the group. |

`line_points` is rendered as two overlaid pbPlots series (the line and the markers) because pbPlots suppresses markers when linear interpolation is on. The result behaves as one series: same data, same colour, same thickness.

`bar` charts share one category axis, so every series must cover the same indices. If lengths differ, the extra values at the end of the longer series are ignored instead of rejecting the plot.

## C API

```c
AromaNode *chart = aroma_ui_chart(root, 24, 24, 280, 140);

/* Two series. x values are optional; NULL means 0, 1, 2, ... */
aroma_chart_set_series_csv(chart, NULL, "4,7,5,9,6,11,8");
aroma_chart_add_series_csv(chart, "0,1,2,3,4,5,6", "2,4,3,6,5,8,7");

aroma_chart_set_type(chart, AROMA_CHART_LINE_POINTS);
aroma_chart_set_series_color(chart, 0, 0xFF8AB4F8);
aroma_chart_set_series_color(chart, 1, 0xFFF28B82);
aroma_chart_set_series_line_style(chart, 1, AROMA_CHART_DASHED);
aroma_chart_set_series_point_type(chart, 1, AROMA_CHART_POINT_TRIANGLES);
aroma_chart_set_bar_gap(chart, 9.5f);
aroma_chart_set_range(chart, true, 0, 1, false, 0, 12);
aroma_chart_set_tick_labels(chart, "Mon,Tue,Wed,Thu,Fri,Sat,Sun", "0,6,12");
```

Series data is copied, so the arrays can go out of scope after the call. `aroma_chart_set_series` replaces the whole series list; `aroma_chart_add_series` appends up to `AROMA_CHART_MAX_SERIES` (16). `aroma_chart_clear_series` removes the data but keeps the per-series style slots, so a refresh can re-add data and only restyle where needed.

## Incense Markup

```aroma
Chart {
    x: 20
    y: 20
    width: 280
    height: 140
    type: "line_points"
    series: "4,7,5,9,6,11,8; 2,4,3,6,5,8,7"
    series_colors: "#FF8AB4F8,#FFF28B82"
    series_points: "circles,triangles"
    series_lines: "solid,dashed"
    series_thickness: "2,1.5"
    x_labels: "Mon,Tue,Wed,Thu,Fri,Sat,Sun"
    y_labels: "0,6,12"
    y_min: 0
    y_max: 12
    pad_left: 26
    pad_bottom: 16
    size: 12
}
```

`series` is a semicolon-separated list of series; each series is a comma-separated list of y values. That is why the inner values stay comma-separated and only the series boundary uses `;`. Quote every value that contains a comma.

`x_series` uses the same shape and is index-aligned with `series`:

```aroma
Chart {
    type: "scatter"
    series: "1,4,2,6; 5,2,7,3"
    x_series: "0,1,2,3; 0,1,2,3"
    series_points: "dots,crosses"
}
```

### Properties

| Property | Default | Meaning |
|---|---|---|
| `type` | `line` | `line`, `scatter`, `line_points`, or `bar`. |
| `series` | n/a | Semicolon-separated series of comma-separated y values. |
| `x_series` | `0,1,2,...` per series | Semicolon-separated x values, paired with `series` by position. |
| `data` | n/a | Legacy single-series y values, used when `series` is absent. |
| `x_data` | `0,1,2,...` | Legacy x values for `data`. |
| `series_colors` / `colors` | Theme primary + palette | Comma-separated per-series colours (`#RGB`, `#RRGGBB`, or `#AARRGGBB`). |
| `series_points` / `points` / `point_type` | `dots` for scatter and line+points | Comma-separated marker styles, one per series. |
| `series_lines` / `lines` / `line` | `solid` | Comma-separated line styles, one per series. |
| `series_thickness` / `thicknesses` | `line_thickness` | Comma-separated stroke widths, one per series. |
| `line_color` | Theme primary | Colour for series 0 when `series_colors` is absent. |
| `line_thickness` | `2` | Default stroke width for every series, in design px. |
| `bar_gap` | pbPlots auto | Gap between bar groups in design px. Useful for compact charts with many bars. |
| `grid_color` | Theme border/surface blend | Grid line colour. |
| `frame_color` | Theme border | Plot frame colour. |
| `text_color` | Theme secondary text | Axis label colour. |
| `x_labels` / `y_labels` | n/a | Comma-separated free text, evenly spread across the axis. |
| `x_min` / `x_max` | From data | Explicit x range. Set neither to keep auto-fitting. |
| `y_min` / `y_max` | From data | Explicit y range. Set neither to keep auto-fitting. |
| `pad_left` / `pad_top` / `pad_right` / `pad_bottom` | `34` / `8` / `10` / `22` | Space reserved for axis furniture, in design px. |
| `grid` | `true` | Draw grid lines. |
| `frame` | `true` | Draw the plot frame. |
| `size` | `12` | Axis label font size. The size must be registered with `IncenseRegisterFontSize`. |

The `series_*` aliases all accept the same comma-separated form, so a single-series chart can use the short names:

```aroma
Chart {
    type: "line_points"
    data: "2,6,4,9,7,12,10"
    line_color: #FFC58AF9
    point_type: "circles"
    line: "dashed"
    line_thickness: 2
}
```

### Line and Marker Styles

Line styles: `solid`, `dashed`, `dotted`, `dotdash`, `longdash`, `twodash`.

Marker styles: `none`, `crosses`, `circles`, `dots`, `triangles`, `filled_triangles`, `pixels`, `dot_line_to_axis`. `filled triangles` and `dotlinetoxaxis` are accepted as aliases. `line` charts ignore markers; `scatter` and `line_points` default to `dots` when `series_points` is not given.

## Multi-Series Rules

- Series 0 follows the theme primary unless `line_color` or `series_colors` overrides it.
- Series 1 and later use a categorical palette, so a multi-series chart is readable without configuring colours.
- Each series carries its own colour, line style, marker style, and thickness.
- Auto ranges consider every series, not just the first.
- `bar` requires equal series lengths; longer series are trimmed to the shortest one.
- Semicolon separators only appear between series. Commas separate values inside a series and colours/styles inside the per-series style lists.

## Axis Furniture

Unlike the data series, the grid, frame, and tick labels are drawn by AromaUI with a real font at device resolution, so they stay crisp and follow the theme. Reserve room for them with `pad_left` (y labels), `pad_bottom` (x labels), and the other padding properties.

- Leave an axis on auto-fitting for exploratory data; set `x_min`/`x_max` or `y_min`/`y_max` when labels must land on exact gridlines.
- Tick labels are spread evenly across the axis, so `"0,1,2,3"` over `y_min: 0 y_max: 3` aligns with the gridlines.
- Bar labels are centred over each group rather than at the edges.
- `grid: 0` and `frame: 0` remove the furniture while keeping the plot itself.
- A theme switch re-reads unset colours and repaints the chart along with the rest of the UI.

## Rendering Notes

- pbPlots rasterises into an RGBA buffer at device density. Its white canvas, grid, frame, and black/grey chrome are keyed to transparency, leaving only the series visible over whatever sits behind the chart.
- The raster is cached until the data, type, or per-series colours change; resizing rebakes it at the new density.
- Stroke widths are clamped to at least 2 device pixels because pbPlots selects a separate rasteriser per integer width.
- Degenerate auto ranges are widened instead of silently falling back to pbPlots' `0..10`.
- Bars always include zero on the y axis, so the baseline is visible even when every value is positive.

## See Also

- [Display & Feedback](Display-and-Feedback-Widgets.md) for labels, cards, progress, snackbars, and dialogs.
- [Theming and Styling](../core-framework/Theming-and-Styling.md) for theme colours shared by every chart.
