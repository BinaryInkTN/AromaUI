# Code Editor

A demonstration of the `aroma_code_editor` widget, AromaUI's programmatic code
editing component.  The editor is created entirely in C (no Incense markup) and
shows syntax highlighting, line numbers, minimap, find bar, completion popup,
hover tooltips, undo/redo, and diagnostic underlines.

## Run it

```bash
cd examples/19_code_editor
python3 ../bin/aroma run linux
```

Web and Android follow the standard example layout (`aroma build web`,
`aroma build android`).

## Source layout

- `src/main.c` - creates the window, instantiates the code editor, loads sample
  C source, and wires up all callbacks (change, cursor, save, completion,
  hover, definition).
- `CMakeLists.txt` - build configuration for Linux, Web (Emscripten), Android.

## Features demonstrated

- Syntax highlighting for C (keywords, strings, numbers, types, functions)
- Line numbers and current-line highlight
- Minimap with scrollbar
- Find bar with match count and navigation
- Completion popup auto-triggered while typing, after `.`, and on Ctrl+Space
  (the widget filters candidates against the typed prefix; Up/Down select and
  Tab or Enter accepts the highlighted suggestion)
- Hover tooltip on mouse move (with delay)
- Go-to-definition on double-click and Ctrl+Enter
- Undo (Ctrl+Z) / redo (Ctrl+Y)
- Copy (Ctrl+C) / cut (Ctrl+X) / paste (Ctrl+V)
- Save (Ctrl+S)
- Select all (Ctrl+A)
- Tab insertion with smart indentation
