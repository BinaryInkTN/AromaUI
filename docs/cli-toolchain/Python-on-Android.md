# Python on Android

Write your app in Python, flash it with the aroma CLI. Python projects
embed CPython via [Chaquopy](https://chaquo.com/chaquopy/) and drive
AromaUI through the generated ctypes bindings, the same `aroma.*` API
as the desktop Python demos.

## Creating the Project

```bash
aroma create myapp --python
```

This creates `src/main.py` (your app: `build_ui()` + `teardown()`),
`src/main.c` (a fixed native bootstrap; do not put UI code there), and
the usual `android/` Gradle project with Chaquopy wired in
(plugin 17.0.0, Python 3.11, `arm64-v8a` + `x86_64`).

## Writing the Application

Edit `src/main.py`. It runs on the Android UI thread:

```python
import aroma, ctypes

def build_ui():
    aroma.aroma_ui_init()
    theme = aroma.aroma_theme_create_material_blue_dark()
    aroma.aroma_theme_set_global(ctypes.byref(theme))
    font = aroma.aroma_font_create(b"/system/fonts/Roboto-Regular.ttf", 20)
    window = aroma.aroma_ui_create_window(b"My App", 400, 600)
    # ... widgets, callbacks ...
```

Rules: `build_ui()` must return promptly (the native loop renders after
it); keep callback callables in module globals so ctypes never frees
them; use `/system/fonts/Roboto-Regular.ttf` (always on-device).

## Dependencies

Third-party packages are declared once and work on both targets:

* `requirements.txt` in the project root (pip format, `#` comments allowed), and/or
* `aroma.json` under `project.python.requirements` (a JSON list).

Both are merged (duplicates removed). Lines starting with `-` (e.g.
`--index-url https://...`) are passed through as pip options.

```txt
requests==2.32.3
packaging
```

```json
{ "project": { "python": { "requirements": ["requests==2.32.3"] } } }
```

* Linux: `aroma build linux` / `aroma run linux` checks the list and
  `pip install`s anything missing into the Python that runs the CLI
  (skipped entirely when everything is already satisfied). On
  PEP 668 systems (externally managed Python) use a virtualenv.
* Android: every `aroma build android` renders the list into the
  Chaquopy `pip { install "..." }` block of
  `android/app/build.gradle` (a managed section: do not hand-edit it;
  a hand-written `pip` block is rejected with an error telling you to
  move entries here).

Stick to packages Chaquopy can install: pure-Python wheels are safest,
native wheels only when Chaquopy ships them. Each package grows the APK
and the build time, so keep the list tight.

## How It Works

You only touch `src/main.py`. The fixed `src/main.c` next to it handles
the Android plumbing for you: when the app starts it runs your
`build_ui()`, then drives the event/render loop, and when the app closes
it runs your `teardown()`. Your widgets, callbacks, and cleanup all run on
the UI thread, in that order, every launch.

`aroma build android` regenerates the bindings into
`android/app/src/main/python/` on every build, so the packaged `aroma`
module always matches the framework.

## Deploying to a Device

```bash
aroma build android
aroma run android            # install + launch on the connected phone
aroma run android --emu      # or boot the emulator and run there
```

## Running on Linux

The same `src/main.py` runs on desktop through the CLI (its
`__main__` block drives the event loop locally instead of the
bootstrap):

```bash
aroma build linux            # generates bindings + builds desktop libaroma.so
aroma run linux              # launches src/main.py
```

So the full matrix works from one codebase: C and Python projects,
each targeting Android and Linux, plus Rust projects on Linux,
all via `aroma create/build/run`. (Rust on Android/Web is not supported
yet -- see [Rust Bindings](../Rust-Bindings.md).)
