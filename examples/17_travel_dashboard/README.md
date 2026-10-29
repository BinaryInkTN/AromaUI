# Travel Dashboard

A car infotainment demo built with AromaUI widgets and Incense markup. The
live behaviour (message playback, music controls, map route, notifications)
lives in `src/travel_demo.c`, the same file that powers the live demo on the
docs page, so an improvement here lands in both places.

## Run it

```bash
cd examples/17_travel_dashboard
python3 ../bin/aroma run linux
```

Web and Android follow the standard example layout (`aroma build web`,
`aroma build android`).

## Source layout

- `ui/app.aroma` - the window root (messages card, music card, map, bottom
  nav, snackbars).
- `src/travel_demo.c` / `src/travel_demo.h` - application state and every
  callback.
- `src/main.c` - registers font sizes, binds them to the runtime, registers
  callbacks and mounts the window.
- `assets/` - avatar, album art, map corner clips and mic icon used by the
  markup.
