# Aroma Notes

A small AromaUI application used by the `19_ide` workspace example. It is a
real project: open this folder in the IDE, edit any file, press `Ctrl+S`, and
use **Run > Build Project** to compile it.

## Layout

- `ui/app.aroma` - window, cards and note list markup
- `src/main.c` - application bootstrap and callbacks
- `src/notes.c` - in-memory note model
- `src/theme.c` - palette shared by the empty and selected states

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/aroma_notes
```

The project uses the AromaUI SDK from this repository through the relative
`AROMA_DIR` path in `CMakeLists.txt`. Set `AROMA_SDK_PATH` to use an installed
SDK instead.

## Workspace tips

- `Ctrl+S` writes the active editor to disk.
- Double-click a symbol or press `Ctrl+Enter` to jump to its definition.
- Open the **Search** view to query every file in this folder.
- The Language Server panel reports diagnostics for Aroma markup and C sources.
