
Every AromaUI app shares one C core. What differs per target is the platform backend (windows, input, event loop) and the graphics backend (draw calls). This page lists what is implemented on each platform and what is still missing. Status is derived from the backend sources in `src/backends/`.

## At a glance

| Platform | Window backend | Graphics | Status |
|---|---|---|---|
| Linux (GLPS) | `aroma_platform_glps.c` (default) | GLES3, Vulkan (partial) | Stable |
| Linux (GLFW) | `aroma_platform_glfw.c` (`-DENABLE_GLFW=ON`) | GLES3, Vulkan | Stable |
| Android | `aroma_platform_android.c` | GLES3, Vulkan | Stable, fullest device access |
| Web (WASM) | `aroma_platform_emscripten.c` | GLES3 via WebGL | Stable core, gaps below |
| ESP32 (TFT_eSPI) | `aroma_platform_tft_espi.cpp` | `aroma_graphics_tft_espi.cpp` | Experimental |
| Windows | GLPS Win32 / GLFW | GLES3 via WGL context (unverified) | Partial, no build/CI wiring |

## Windowing and main loop

| Feature | Linux GLPS | Linux GLFW | Android | Web | ESP32 | Windows |
|---|---|---|---|---|---|---|
| Create window / context | Yes | Yes | Yes (NativeActivity) | Yes (WebGL2, WebGL1 fallback) | Single fixed window, geometry ignored | Partial (GLPS Win32 + WGL, unverified) |
| Event loop | Yes | Yes | Yes (looper) | Yes (`emscripten_set_main_loop_arg`) | No, app pumps manually (`run_event_loop` returns false) | Partial (same `glps_wm` path, unverified) |
| VSync control | No | Yes, always on | Yes | Browser-driven | N/A | Missing |
| Fullscreen toggle | Missing (`set_fullscreen` NULL) | Missing | Yes | Missing | N/A | Missing |
| Multiple windows | Yes | Yes | No | No | No | Partial (win32 wm array, unverified) |
| Headless / offscreen render + `read_pixels` | Missing | Yes (EGL surfaceless) | Missing | Missing | N/A | Missing |
| Open URL in browser | Missing (`open_url` NULL) | Missing | Yes (`ACTION_VIEW`) | Missing | Missing | Missing |

## Graphics

| Feature | Linux GLES3 | Linux Vulkan | Android | Web (WebGL) | ESP32 | Windows |
|---|---|---|---|---|---|---|
| Rects, lines, arcs, clipping | Yes | Yes | Yes | Yes | Yes (TFT primitives, tiled) | Partial (GLES backend has no EGL dependency; driver entry points unverified) |
| `draw_rectangle` | No-op (use `fill_rectangle`) | No-op | No-op | No-op | 1px hollow white only | No-op (same, all backends) |
| Text rendering | Yes (FreeType) | Yes (FreeType) | Yes (FreeType) | Yes (FreeType) | Fixed bitmap font only, size ignored | Partial (FreeType is portable; Windows build unverified) |
| Images (PNG/JPG/SVG) | Yes (`stb_image`, nanosvg) | Yes | Yes | Via WebGL path | Raw RGB565 slots only, no file decode, no scaling | Partial (same portable decoders; driver path unverified) |
| Rounded-corner images | Yes | Ignored (drawn square) | Depends on backend | Yes | Ignored | Partial (same backend limits) |
| Backdrop blur | Yes (only backend) | Missing | Depends on backend | Missing | Missing | Partial (GLES backend code, unverified) |
| Dirty-region tracking | Missing | Yes | Depends on backend | Missing | Yes (tile based) | Missing |

## Input

| Feature | Linux GLPS | Linux GLFW | Android | Web | ESP32 | Windows |
|---|---|---|---|---|---|---|
| Mouse move / click / release | Yes | Yes | N/A (touch) | Yes | No input driver | Partial (WM_MOUSEMOVE/LBUTTON via Win32, unverified) |
| Scroll wheel | Yes (H+V) | Yes | N/A | Yes | Missing | Partial (WM_MOUSEWHEEL wired, unverified) |
| Touch | Yes (mapped from touch events) | Missing | Yes (multi-touch, gestures) | Single touch only | Missing | Missing (no Win32 touch path) |
| Physical keyboard | Yes (press only, arrows/Home/End/Del/Backspace/CapsLock/Ctrl) | Yes (press + release, Enter/Delpecial keys, printable via char callback) | Yes (limited key set) | Subset (arrows/Home/End/F1-F12, Ctrl/Shift/Alt; no CapsLock) | Missing | Partial (printables via ToUnicode; X11-oriented modifier keycodes) |
| OS virtual keyboard | Missing (`show/hide_keyboard` NULL) | Missing | Yes (IME bridge) | Missing | Missing | Missing |
| In-app keyboard overlay | Yes (opt-in via `aroma_textbox_enable_virtual_keyboard`) | Yes | Yes | Yes | Yes | Partial (core code, unverified) |
| System back button | Yes (Escape) | Yes (Escape) | Yes (back key/gesture) | Yes (Escape) | Missing | Partial (shared Escape path, unverified) |

## Text, media, network

| Feature | Linux | Android | Web | ESP32 | Windows |
|---|---|---|---|---|---|
| Fonts (bundled Ubuntu, any TTF) | Yes (FreeType) | Yes (FreeType, dp/sp scaling) | Yes (FreeType) | Bitmap only, bundled font ignored | Partial (FreeType portable; unverified) |
| GIF playback | Yes | Yes | Yes | No decoder backend | Partial (same decoder path, unverified) |
| Software 3D viewer | Yes | Yes | Yes | No (`aroma_3d.c` excluded) | Partial (needs pthreads + GLES3; unverified) |
| HTTP fetch | Yes (libcurl) | Yes (native bridge) | Yes (`emscripten_fetch`) | Missing | Missing |
| Map: online tiles + routing | Yes | Yes | Online only (no offline MBTiles) | Offline MBTiles only in practice | Missing |
| Clipboard copy/paste | Missing | Yes | Missing | Missing | Missing |
| Camera / gallery picker | Missing | Yes (intents + activity results) | Missing | Missing | Missing |

## Android-only device APIs

These exist only on Android (`aroma_platform_android.c` + `AromaHelper.java` bridge, 19 guides under `Backend Abstraction / Android`): runtime permissions, toast, vibration (incl. effects), sensors, notifications, Bluetooth classic + BLE, NFC/RFID, biometric, location, preferences, TTS, torch, wake lock, orientation lock, PiP, shortcuts, wallpaper, volume, storage info, screenshots, contacts, IR, display metrics, assets. No other backend implements these slots.
