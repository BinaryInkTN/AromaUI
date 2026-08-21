
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
