# Pickers & Steppers

Android-faithful pickers: popup-only Calendar/DatePicker/TimePicker
fields opening modal dialogs (pending selection + Cancel/OK), an AOSP-style vertical
NumberPicker, Material wizard steps, and a ViewPager2-style carousel.
All widgets use 48dp touch targets, slop-guarded taps, press feedback,
and follow the standard widget pattern (`_create`, `_set_font`,
`_setup_events`, `_draw`, `_destroy`) with `aroma_ui_*` helpers plus
Incense markup support.

## Calendar

Popup-only Material calendar: the in-tree widget is a compact date
field; tapping it opens a modal floating panel with the month/year
header (48dp chevron targets), weekday row, and day cells. Selected
day = filled primary circle, today = outline ring, pressed day =
shaded circle. Dimmed adjacent-month edge days are tappable and shift
the month (like AOSP CalendarView).

Tapping the header title opens a **year grid** (12 years per page,
chevron paging, 1900–2100) so old dates are two taps away; tapping a
year returns to its month.

With `popup: true` (or `aroma_calendar_set_popup`) the in-tree widget
becomes a compact date field; tapping it opens a modal floating panel
(scrim + calendar, year grid included). Day tap confirms + closes,
outside/scrim tap cancels.

```c
AromaNode *cal = aroma_ui_calendar(parent, 24, 128, 343, 56,
                                  on_date_picked, NULL, font);
aroma_calendar_set_date(cal, 2026, 10, 6);
aroma_calendar_set_date(cal, 2026, 10, 6);
```

```incense
Calendar {
    x: 24 y: 128 width: 343 height: 56
    year: 2026 month: 10 day: 6
    on_change: "cal_changed"
}
```

Callback signature: `void cb(AromaNode *node, int year, int month, int day,
void *ud)`. In Incense, register it as `INCENSE_CALLBACK_DATE_PTR`:
`void cb(int year, int month, int day, void *ud)`.

## DatePicker

Popup-only Material DatePicker: the in-tree widget is a compact field
(`Tue, Oct 6` + calendar glyph); tapping opens a modal floating panel
with the header (`title` + pending-date headline), month grid,
Cancel/OK footer, and year grid (scrim + flip-above when there is no
room below). Taps edit a pending date; OK confirms and fires
`on_change`, Cancel reverts. Programmatic equivalents:
`aroma_datepicker_confirm()` / `aroma_datepicker_cancel()`.

```c
AromaNode *dp = aroma_ui_datepicker(parent, 24, 128, 343, 56,
                                    2026, 10, 6, on_date_picked, NULL, font);
```

```incense
DatePicker {
    x: 24 y: 128 width: 343 height: 56
    year: 2026 month: 10 day: 6 title: "Select date"
    on_change: "date_changed"
}
```

OK/Cancel and the year grid all work inside the panel.

## TimePicker

Popup-only Material TimePicker: the in-tree widget is a compact field
(`02:30 PM` + clock glyph); tapping opens a modal floating panel with
the clock dial. Panel header (`title` + tappable HH:MM headline + AM/PM
column), circular dial with numbers and a selection hand, Cancel/OK
footer. Drag around the dial to set the value; tap the headline to
switch hour/minute mode; AM/PM toggles the period. `mode: "24h"` (or
`aroma_timepicker_set_24h`) shows the AOSP outer (00, 13-23) + inner
(1-12) rings and hides AM/PM. OK confirms and fires `on_change`,
Cancel reverts.

```c
AromaNode *tp = aroma_ui_timepicker(parent, 24, 128, 343, 56,
                                    14, 30, on_time_picked, NULL, font);
```

```incense
TimePicker {
    x: 24 y: 128 width: 343 height: 56
    hour: 14 minute: 30
    on_change: "time_changed"
}
```

Callback signature: `void cb(AromaNode *node, int hour, int minute,
void *ud)`; Incense type is `INCENSE_CALLBACK_TIME_PTR`.

## Stepper (NumberPicker)

AOSP NumberPicker, vertical: previous/selected/next rows with divider
lines. Tap the top/bottom third to step, drag with snap, fling across
values (8-sample velocity tracker, 150ms horizon, same model as the
scroll containers), long-press to auto-repeat. Mouse drags fling the
same way on desktop, and the wheel steps once per tick. `wrap`
(default true) wraps around `min..max`; with `wrap: false` the ends
resist and snap back. `on_change` fires on settle.

```c
AromaNode *qty = aroma_ui_stepper_numeric(parent, 24, 156, 200, 180,
                                          0, 10, 3, 1,
                                          on_qty_changed, NULL, font);
aroma_stepper_set_wrap(qty, false);
```

```incense
Stepper {
    x: 24 y: 156 width: 200 height: 180
    min: 0 max: 10 value: 3 step: 1 wrap: true
    on_change: "stepper_changed"
}
```

Wizard progress (`mode: "steps"` with `Step` children, tap to jump):
completed steps are primary with a check, the current step is primary
with its number, upcoming steps are outlined. 48dp targets.

```incense
Stepper {
    x: 24 y: 380 width: 343 height: 84
    mode: "steps" selected: 1
    on_change: "steps_changed"
    Step { text: "Cart" }
    Step { text: "Ship" }
    Step { text: "Pay" }
}
```

Both modes report `void cb(AromaNode *node, int value_or_index, void *ud)`
(`INCENSE_CALLBACK_NODE_INT_PTR` in Incense).

## Carousel

ViewPager2 pager. Each `Page` child becomes a full-viewport page; only
the current page (plus neighbors mid-swipe) is visible. A horizontal
drag pulls pages with the finger (edge resistance at the ends); release
snaps by distance or flings by velocity. Dots indicate position and are
tappable. Programmatic: `aroma_carousel_next/prev/set_page`.

```c
AromaNode *car = aroma_ui_carousel(parent, 24, 128, 343, 240,
                                   on_page_changed, NULL, font);
AromaNode *p1 = aroma_carousel_add_page(car);
/* build page content into p1 ... */
```

```incense
Carousel {
    x: 24 y: 128 width: 343 height: 240
    on_change: "carousel_changed"
    Page { Card { Label { text: "Page 1" } } }
    Page { Card { text: "Page 2" } }
}
```

## DemoApp

The `DemoApp` catalog (`~/DemoApp`) has one demo per widget under
`src/assets/demos/`: `calendar`, `date`, `time`, `stepper`, `carousel`.
Each demo shows the widget plus a status label updated from its `on_change`
callback (see `demo_incense.c`: `on_cal_changed`, `on_date_changed`,
`on_time_changed`, `on_stepper_changed`, `on_steps_changed`,
`on_carousel_changed`). Note the pickers confirm on OK, so the status
label updates when the dialog is accepted, matching Android. The picker
demos show compact popup fields; tapping one opens its modal dialog.
Demos mount lazily on first
open, so startup only parses the shell (~19ms) instead of all 37 demos
(~140ms).

## DPI, polish, clipping

Every dimensional constant (touch targets, radii, insets, stroke widths,
dot sizes, footer pills) is authored in dp and converted with
`aroma_android_dp_to_px[_f]` on Android, so targets stay 48dp from ldpi
to xxxhdpi while desktop keeps 1:1 pixels. Tappable chrome (chevrons,
Cancel/OK) shows a shaded pressed state; selected days get a filled
circle, today a ring, dials a filled face with a selection hand.
The NumberPicker clips its rolling values with a scissor rect
(`graphics_set_clip`/`clear_clip`, guarded for backends without clip
support) so dragged rows never paint outside the frame.
