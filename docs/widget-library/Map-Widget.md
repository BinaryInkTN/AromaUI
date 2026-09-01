
A high-performance, interactive map component supporting OpenStreetMap tiles, markers, and routing.

## Quick Start

```c
AromaNode *map = aroma_ui_map(root, 0, 0, 700, 400);
aroma_map_set_center(map, 33.8869f, 9.5375f);
aroma_map_add_icon_marker(map, 33.8869f, 9.5375f, 0xFF0000, AROMA_ICON_HOME);
aroma_map_set_route(map, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE);
```

## Features

- **Tile caching**: LRU cache with 128 tiles by default
- **Cross-platform**: Uses libcurl on native, `emscripten_fetch` on web
- **Markers**: Icon and popup markers with click interaction
- **Routing**: OSRM polyline routing with mutex-safe decoding
- **Physics**: Inertial panning with velocity-based fling

## API Reference

| Function | Purpose |
|---|---|
| `aroma_map_set_center(lat, lon)` | Set map center |
| `aroma_map_set_zoom(level)` | Set zoom level (1–18) |
| `aroma_map_add_icon_marker(lat, lon, color, icon)` | Add icon marker |
| `aroma_map_add_popup_marker(lat, lon, color, text)` | Add clickable popup |
| `aroma_map_set_route(lat1, lon1, lat2, lon2, color)` | Draw route polyline |
| `aroma_map_remove_route()` | Clear current route |

## Architecture

The map widget runs tile fetching on background worker threads to avoid blocking the UI. It uses spherical mercator projection to convert lat/lon to pixel coordinates and supports pan/zoom gestures with momentum.

## Try It

Open the [Map Example](../widget-library/wasm/map_example/index.html) in the sandbox.

## What's Next

- Learn [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Explore [Input & Controls](Input-and-Control-Widgets.md) for interactive elements.
- Try [Incense](../widget-library/wasm/incense_sandbox/index.html) for rapid prototyping.
