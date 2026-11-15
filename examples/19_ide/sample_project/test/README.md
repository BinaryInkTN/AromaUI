# test

Created with AromaUI Studio using the **Notes app** template.

## Layout

- `ui/app.aroma` - declarative UI, editable in the drag-and-drop designer
- `src/main.c` - application bootstrap and callbacks
- `aroma.json` - project metadata
- `CMakeLists.txt` - CMake target

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/test
```

The project links to the AromaUI SDK through `AROMA_SDK_PATH`, or
the path recorded by Studio in `CMakeLists.txt`.
