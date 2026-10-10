# AromaUI Examples

One minimal example per feature. Each directory was scaffolded with the CLI:

```bash
python3 bin/aroma create <Name>
```

and keeps the standard project layout (`src/main.c`, `CMakeLists.txt`,
`aroma.json`, `android/`). The UI of every example lives in `ui/app.aroma`
and is loaded with Incense; `src/main.c` only registers callbacks and runs
the main loop.

Build and run any example on Linux/Android:

```bash
cd <example_name> 
python3 ../bin/aroma build linux/android
python3 ../bin/aroma run linux/android
```

The binary name always matches the directory. Each example also builds
with `aroma build linux` and runs with `aroma run linux`.

| Directory | Feature showcased |
| --- | --- |
| `01_hello_world` | Window plus a button wired through Incense |
| `02_buttons` | Button types, icon buttons, chips |
| `03_form_inputs` | Textbox, checkbox, radio, switch, slider, dropdown, stepper |
| `04_layout` | Flex container, scroll view, cards, divider |
| `05_navigation` | Sidebar, tabs, segmented control |
| `06_feedback` | Dialog, snackbar, menu, tooltip, loading, progress bar |
| `07_lists_tables` | List view, data table, carousel |
| `08_theming` | Live theme switching |
| `09_animation` | Declarative entrance plus C-driven motion with easings |
| `10_datetime` | Calendar, date picker, time picker |
| `11_3d_viewer` | Built-in 3D model with orbit, zoom, auto-rotate |
| `12_incense` | State bindings and conditional UI |
| `13_gestures` | Gesture engine: tap, double-tap, long-press, swipes, pan, pinch |
