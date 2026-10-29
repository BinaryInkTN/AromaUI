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
| `01_hello_world` | Build production level UIs |
| `02_buttons` | Build production level UIs |
| `03_form_inputs` | Build production level UIs |
| `04_layout` | Build production level UIs |
| `05_navigation` | Build production level UIs |
| `06_feedback` | Build production level UIs |
| `07_lists_tables` | Build production level UIs |
| `08_theming` | Build production level UIs |
| `09_animation` | Build production level UIs |
| `10_datetime` | Build production level UIs |
| `11_3d_viewer` | Build production level UIs |
| `12_incense` | Build production level UIs |
| `13_gestures` | Build production level UIs |
| `14_video` | Build production level UIs |
| `15_webview` | Build production level UIs |
| `16_loud_dashboard` | Build production level UIs |
| `17_travel_dashboard` | Build production level UIs |
| `18_fitness_dashboard` | Build production level UIs |
| `14_python_bindings` | Build production level UIs |
