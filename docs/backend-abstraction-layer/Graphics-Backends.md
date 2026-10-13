
Your interface renders with GLES3, Vulkan, or TFT_eSPI depending on the
target. You never pick one in code; the platform backend selects it, and
every backend supports the same operations: shapes, text, images, and
clipping.

## Architecture Overview

All three backends draw the same primitives (rectangles, arcs), render
text, and load images. Capability differences come from the hardware,
not from your code: desktop and mobile targets get GPU acceleration,
while ESP32 displays use tiled software rendering that fits small RAM.

---

## GLES3 Backend

The default renderer for Linux desktops and Android. It batches shapes
for speed, draws smooth anti-aliased rounded corners, caches font glyphs
per window (up to 16 fonts), and loads common raster and vector image
formats.

---

## Vulkan Backend

A high-efficiency alternative for Android and Linux. It submits work to
the GPU with low overhead, renders crisp alpha-blended text, and suits
apps that need predictable frame timing.

---

## TFT_eSPI Backend (Embedded)

For ESP32-class microcontrollers with small RAM. The screen renders in
tiles, and only changed tiles are sent over the bus. Text uses compact
built-in bitmap fonts.

---

## DrawList System

Widgets record drawing commands instead of drawing immediately. Each
frame, the pipeline sorts them, skips offscreen work, and flushes the
rest to the backend. On small screens, partial updates keep memory low.
