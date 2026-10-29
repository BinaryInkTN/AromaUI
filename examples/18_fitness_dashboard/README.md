# Fitness Dashboard

A dark fitness tablet demo built with AromaUI widgets and Incense markup.
The UI uses canvas-drawn charts (activity, sleep, heart rate, wellness,
focus) plus a gauge, and every interaction lives in `src/fitness_demo.c` —
the same file that powers the live demo on the docs page.

## Run it

```bash
cd examples/18_fitness_dashboard
python3 ../bin/aroma run linux
```

Web and Android follow the standard example layout (`aroma build web`,
`aroma build android`).

## Source layout

- `ui/app.aroma` - the window root (nav pills, page containers, gauge,
  charts and overlays).
- `src/fitness_demo.c` / `src/fitness_demo.h` - application state and every
  callback.
- `src/main.c` - registers font sizes, binds them to the runtime, registers
  callbacks and mounts the window.
- `assets/` - avatar, hero image, logo and scrim used by the markup.
