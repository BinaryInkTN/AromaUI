
Your application code stays the same everywhere: Linux desktops, Windows, Android, Web, and ESP32 displays. The backend handles what is different per platform.

## Architecture and Abstraction

Every backend provides the same three things: window management, input
capture, and shutdown handling for its platform. On top of that, each one
adds what its target offers, such as Bluetooth and notifications on
Android or headless rendering with GLFW.

### Choosing a Backend

GLPS is the default on Linux desktops. Pass `-DENABLE_GLFW=ON` to use
GLFW instead (portable windowing, plus headless rendering without a
display). Android, Web, and ESP32 builds select their backend
automatically for the target.

---

## GLPS (Linux Desktop Default)

The GLPS backend is the default for Linux and desktop development, on both X11 and Wayland.

### Input

Mouse movement and clicks, keyboard input with modifiers, and
vertical and horizontal scrolling all arrive as standard framework
events on both X11 and Wayland.


---

## GLFW (Cross-Platform and Headless)

The GLFW backend provides portable windowing for Windows, macOS, and Linux.

### Headless Rendering

Without a display server (for example, in video pipelines), GLFW
renders offscreen. Pair it with `aroma_ui_read_pixels()` to capture
UI frames directly into shared memory for encoding.


---

## Android Backend

The Android backend runs your app as a native activity and manages its
lifecycle (start, pause, surface changes, shutdown) for you. Frames sync
with the display refresh, and redraws happen only when needed.

Device features such as permissions, Bluetooth, vibration, camera, and
sensors are available through the C API pages in
[Android](android/Overview.md). Your code stays portable: the same calls
work wherever the feature exists.

---

## TFT_eSPI and Emscripten

### TFT_eSPI (Embedded)

The TFT_eSPI backend targets ESP32 and similar microcontrollers. It is unique in its support for **Tiled Rendering**.

- **Dirty regions**: only changed screen tiles are sent, which minimizes slow SPI bus traffic.
- **Memory**: drawing targets a small sprite buffer instead of a full framebuffer.

### Emscripten (Web)

For WebAssembly targets, the backend utilizes Emscripten's browser hooks.

- **DPI scaling**: the UI scales automatically for high-density (Retina) displays.
- **Main loop**: rendering cooperates with the browser, so the tab stays responsive.

Sources: [src/backends/platforms/aroma_platform_interface.h110-141](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/backends/platforms/aroma_platform_interface.h#L110-L141)[src/core/aroma_ui_impl.c84-102](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_ui_impl.c#L84-L102)