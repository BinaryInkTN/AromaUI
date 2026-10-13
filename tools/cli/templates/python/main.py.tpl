"""{{PROJECT_NAME}} — AromaUI app written in Python.

On Android this module is imported by the native bootstrap (src/main.c)
on the UI thread: the bootstrap calls :func:`build_ui` once, drives the
standard Aroma event/render loop itself, then calls :func:`teardown`.
Keep this module import-safe and do not block inside :func:`build_ui`.

On Linux you can run the same file directly for quick iteration
(requires generated bindings and a desktop libaroma.so; see README):

    python3 tools/generate_python_bindings.py
    AROMA_LIB=build/src/libaroma.so python3 src/main.py
"""

import ctypes
import time

import aroma

FONT_PATH = "/system/fonts/Roboto-Regular.ttf"
DESKTOP_FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

_taps = 0
_window = None
_font = None
_counter = None


def _as_node(p):
    return ctypes.cast(p, ctypes.POINTER(aroma.AromaNode))


def _on_tap():
    global _taps
    _taps += 1
    if _counter is not None:
        aroma.aroma_label_set_text(
            _as_node(_counter), ("Taps: %d" % _taps).encode())
    print("tapped %d" % _taps, flush=True)


def build_ui():
    """Create the window and widgets. Runs on the Android UI thread."""
    global _window, _font, _counter
    import os as _os
    font_path = (DESKTOP_FONT_PATH
                 if _os.path.isfile(DESKTOP_FONT_PATH) else FONT_PATH)
    assert aroma.aroma_ui_init(), "aroma_ui_init failed"
    theme = aroma.aroma_theme_create_material_blue_dark()
    aroma.aroma_theme_set_global(ctypes.byref(theme))
    _font = aroma.aroma_font_create(font_path.encode(), 20)
    assert _font, "font load failed: %s" % font_path
    _window = aroma.aroma_ui_create_window(b"{{PROJECT_NAME}}", 400, 600)
    assert _window, "window creation failed"
    root = _as_node(_window)
    box = aroma.aroma_ui_container(root, 0, 0, 400, 600)
    aroma.aroma_ui_label(_as_node(box), b"{{PROJECT_NAME}}", 40, 60,
                         int(aroma.LABEL_STYLE_LABEL_LARGE), _font)
    _counter = aroma.aroma_ui_label(
        _as_node(box), b"Taps: 0", 40, 140,
        int(aroma.LABEL_STYLE_LABEL_MEDIUM), _font)
    aroma.aroma_ui_button(_as_node(box), b"Tap me", 40, 220, 200, 52,
                          _on_tap, None, _font)
    aroma.aroma_ui_request_redraw(None)
    print("{{PROJECT_NAME}} UI built", flush=True)


def teardown():
    """Destroy everything build_ui created. Runs on the UI thread."""
    global _window, _font, _counter
    if _window is not None:
        aroma.aroma_ui_destroy_window(_window)
        _window = None
    if _font is not None:
        aroma.aroma_font_destroy(_font)
        _font = None
    _counter = None
    aroma.aroma_ui_shutdown()
    print("{{PROJECT_NAME}} shut down", flush=True)


if __name__ == "__main__":
    # Desktop dev loop: same UI, driven locally instead of the bootstrap.
    build_ui()
    try:
        while aroma.aroma_ui_is_running():
            aroma.aroma_ui_process_events()
            aroma.aroma_ui_render(_window)
            time.sleep(1.0 / 60.0)
    finally:
        teardown()
