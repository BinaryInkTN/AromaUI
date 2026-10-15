
# Python Bindings

Write your AromaUI app in Python. You get the same windows, widgets, and
themes as C (buttons, labels, containers, and fonts), callable directly from
Python. When the framework updates, one command refreshes the bindings, so
your code keeps working.

## Getting Started

```bash
git submodule update --init vendors/ctypesgen
aroma bindings --check
```

If that prints `bound=... missing=0`, you are ready. Now run a first app:

```bash
LD_LIBRARY_PATH=build/src AROMA_LIB=build/src/libaroma.so python3 my_app.py
```

(`build/src/libaroma.so` is your framework build. `aroma build linux`
inside a Python project produces it for you.)

## First Application

```python
import ctypes
import aroma

aroma.aroma_ui_init()

theme = aroma.aroma_theme_create_material_blue_dark()
aroma.aroma_theme_set_global(ctypes.byref(theme))

font = aroma.aroma_font_create(
    b"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20)
window = aroma.aroma_ui_create_window(b"Hello", 400, 600)

def on_tap():
    print("tapped", flush=True)

box = aroma.aroma_ui_container(window, 0, 0, 400, 600)
aroma.aroma_ui_label(box, b"Hello, Python", 40, 60, 1, font)
aroma.aroma_ui_button(box, b"Tap me", 40, 140, 200, 52, on_tap, None, font)

while aroma.aroma_ui_is_running():
    aroma.aroma_ui_process_events()
    aroma.aroma_ui_render(window)
```

The pattern is always the same: init, create a window, add widgets,
loop over events + render. Compare with the C version in
[Getting Started](../overview/Getting-Started.md): the calls match, in the same order.

## Usage Guidelines

- **Refresh after updating.** The bindings are generated files (never
  committed). After pulling new framework code, run `aroma bindings`
  again so Python matches it.
- **Text is bytes.** Pass `b"..."` for titles, labels, and file paths.
  The button/label helpers also accept plain `str` and encode it for you.
- **Keep callbacks in globals.** Store button functions in module-level
  variables (like `on_tap` above). Locals can be freed while the UI still
  needs them, and taps will stop working.
- **On Android, `build_ui()` must return fast.** It runs on the UI thread;
  build your widgets and return. The framework drives the loop for you.
- **Fonts live in different places.** Use `/system/fonts/Roboto-Regular.ttf`
  on Android and a desktop path (e.g. DejaVuSans) on Linux, picking whichever
  file exists like the starter template does.

## Generated Files

`aroma bindings` writes an `aroma.py` module next to your project
(`bindings/python/` by default; `aroma run linux` adds it to `PYTHONPATH` for you). `AROMA_LIB` tells it where your built
`libaroma.so` is; `aroma.is_available()` returns `True` when everything
is found.

Useful flags: `--output` to choose where the module goes, `--check` to
verify it loads, `--stats` to peek at what's inside without writing files.

## Troubleshooting

| You see | Do this |
| --- | --- |
| `No public headers (aroma*.h)` | Run from the repo root or pass `--include-dir` pointing at the SDK `include/` |
| `ctypesgen submodule is empty` | `git submodule update --init vendors/ctypesgen` |
| `Could not load aroma` / `lib available: False` | Build `libaroma.so` first (`aroma build linux` in a Python project does it), then set `AROMA_LIB` to its full path |
| Taps stop working | Your callback was a local. Move it to module level |

To ship a full app in Python, see [Python on Android](Python-on-Android.md).
Prefer Rust? The same generator backend produces a Rust crate instead --
see [Rust Bindings](Rust-Bindings.md).
