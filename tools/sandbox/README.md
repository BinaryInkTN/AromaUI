# Incense sandbox runtime (WebAssembly)

`incense_sandbox_main.c` is the harness built into
`docs/incense_sandbox.{js,wasm}` (loaded by `docs/sandbox.html`) and its
copy under `docs/widget-library/wasm/incense_sandbox/`. It mounts Incense
markup with the current framework sources, so every widget works in the
browser — including Calendar/DatePicker/TimePicker/Stepper/Carousel.

## Rebuild

```bash
emcmake cmake -S vendors/freetype -B /tmp/ft-wasm \
  -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON
cmake --build /tmp/ft-wasm
FT_BUILD=/tmp/ft-wasm bash tools/sandbox/build_wasm.sh
```

Then bump the `?v=N` cache-buster in `docs/sandbox.html` (script tag +
`locateFile`) and `wasm/incense_sandbox/index.html` (`?v=M`), or browsers
keep running a stale js/wasm pair (mismatched pairs fail with
`_emscripten_stack_get_current is not a function`).

`stub/` + `stub_curl.c` stand in for libcurl, which has no Emscripten
port; map/image widgets fall back to their offline paths on web.

## JS interface

- `aroma_sandbox_reload(source)` — mount markup, record errors
- `aroma_sandbox_has_error()` / `aroma_sandbox_get_last_error()` — status
- `aroma_sandbox_set_theme(dark)` — light: Material Blue, dark: Black
- `aroma_sandbox_get_width()` / `get_height()` — canvas size
- `aroma_sandbox_init()` — idempotent first-boot setup

Curated docs demos reference callbacks (`on_click: "back"`, ...) with no
app behind them; they are registered as no-ops so every demo loads with
status Ready. `select_theme` is wired for real (option text → preset) so
the theming preview works.

## Verify (headless Chrome)

```bash
(cd docs && python3 -m http.server 8901 &) ; sleep 1
google-chrome --headless --no-sandbox --disable-gpu \
  --use-gl=swiftshader --enable-unsafe-swiftshader \
  --virtual-time-budget=25000 --window-size=500,700 \
  --screenshot=/tmp/shot.png \
  "http://localhost:8901/sandbox.html?embed=1&example=pickers"
```

The shot must show rendered widgets and the page status must read Ready.
