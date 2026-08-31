 
# AromaUI
 
<br/>
 
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-16-48" src="https://github.com/user-attachments/assets/0dd6097b-2eb0-4142-a5f6-d463f618df36" />
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-16-43" src="https://github.com/user-attachments/assets/9896b23b-ffcc-4426-93d7-30443c549cc2" />
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-17-41" src="https://github.com/user-attachments/assets/b3175973-a9aa-43e6-b99e-519f0eff380c" />
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-17-33" src="https://github.com/user-attachments/assets/1f9b639f-b58c-47ad-a0a3-694a2b1e9ede" />
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-17-25" src="https://github.com/user-attachments/assets/cc6da86e-e3ca-4bb9-9f65-9edac46a46e4" />
<img width="1031" height="611" alt="Screenshot From 2026-08-21 16-16-54" src="https://github.com/user-attachments/assets/75212a9c-c3f2-447a-ac82-bbb0dc47512a" />
 
<br/>
 
AromaUI is a modern, lightweight C UI framework designed for embedded and desktop applications. It provides a set of customizable widgets and tools to build beautiful, responsive user interfaces in C.
 
## Key Features
 
- **Cross-Platform**: Linux (GLPS/GLFW), Android (NDK/ANativeWindow), Web (Emscripten/WASM), and Embedded (ESP32/TFT_eSPI)
- **High Performance**: Slab allocator, dirty-region tracking, deferred DrawList rendering, and Z-index sorting
- **Rich Widget Library**: Buttons, containers, list views, tabs, sidebars, maps, 3D viewers, and more
- **Theme System**: Material Design presets, dark mode, high contrast, and custom color palettes
- **Animation Engine**: Smooth property transitions with easing functions
- **Voice Control**: Integrated offline speech recognition (Vosk) for automotive HMI
- **3D Rendering**: Built-in software 3D viewer with camera orbit, zoom, and auto-rotate
 
## Platform Support
 
| Platform | Backend | Graphics | Status |
| --- | --- | --- | --- |
| Linux Desktop | GLPS / GLFW | GLES3 / Vulkan | Stable |
| Android | ANativeActivity | GLES3 / Vulkan | Stable |
| Web (WASM) | Emscripten | GLES3 (WebGL) | Stable |
| ESP32 | TFT_eSPI | SPI TFT | Experimental |
 
## Quick Start
 
 ```bash
 # Clone the repository
 git clone https://github.com/BinaryInkTN/AromaUI.git --recursive
 cd AromaUI
 
 # Build the car infotainment example on Linux
 cd examples/car_infotainment/build
 cmake ..
 make -j$(nproc)
 ./infotainment
 ```
 
## Documentation
 
Get started with AromaUI: Visit <a href="https://binaryinktn.github.io/AromaUI/">Docs</a> or <a href="https://binaryinktn.github.io/AromaUI/docs.pdf"> Download PDF</a>.
 
- [Getting Started](docs/overview/Getting-Started.md) - Setup, project creation, and Hello World
- [Architecture Overview](docs/overview/Architecture-Overview.md) - Four-layer system design
- [Widget Library](docs/widget-library/) - Buttons, containers, maps, 3D viewers, and more
- [Core Framework](docs/core-framework/) - Scene graph, layout, events, animation, rendering
- [Backend Abstraction](docs/backend-abstraction-layer/) - Graphics and platform backends
- [CLI Toolchain](docs/cli-toolchain/) - Project creation, building, and deployment
- [Examples](docs/example-applications/) - Car infotainment, voice control, smartwatch
 
## Building for Web (Emscripten)
 
To build AromaUI for the web using Emscripten, clone and initialize the emscripten submodule and install the SDK:
 
 ```bash
 git submodule update --init --recursive vendors/emscripten
 cd vendors/emscripten
 ./emsdk install latest
 ./emsdk activate latest
 ```
 
Configure your build with `emcmake` and build with `emmake` or `emmake make` as usual.
 
## Contributing
 
Contributions are welcome! Please open issues or pull requests to help improve AromaUI.
 
## License
 
AromaUI is released under the MIT License.
