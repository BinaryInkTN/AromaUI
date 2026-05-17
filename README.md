# AromaUI


<br/>

<div style="text-align:center;">
  <img src="docs/images/example_screenshot.png" />
</div>

<br/>

AromaUI is a modern, lightweight C UI framework designed for embedded and desktop applications. It provides a set of customizable widgets and tools to build beautiful, responsive user interfaces in C.

## Documentation
Get started with AromaUI: Visit <a href="https://binaryinktn.github.io/AromaUI/">Docs</a> or <a href="https://binaryinktn.github.io/AromaUI/docs.pdf"> Download PDF</a>.

## Contributing
Contributions are welcome! Please open issues or pull requests to help improve AromaUI.

## Building for Web (Emscripten)
To build AromaUI for the web using Emscripten, clone and initialize the emscripten submodule and install the SDK:

```bash
git submodule update --init --recursive vendors/emscripten
cd vendors/emscripten
./emsdk install latest
./emsdk activate latest
```

Configure your build with `emcmake` and build with `emmake` or `emmake make` as usual.

## License
AromaUI is released under the MIT License.
