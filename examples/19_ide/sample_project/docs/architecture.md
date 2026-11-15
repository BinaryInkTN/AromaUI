# Aroma Notes architecture

The sample deliberately keeps the model tiny so the IDE can exercise real
language-server behaviour without a build step getting in the way.

## Layers

1. **Markup** - `ui/app.aroma` owns the window, cards, labels and buttons.
2. **Bootstrap** - `src/main.c` registers callbacks and loads the markup.
3. **Model** - `src/notes.c` stores up to 16 notes in a fixed-size array.
4. **Theme** - `src/theme.c` exposes palette helpers shared by the views.

## Language server checks

- Incense diagnostics verify balanced braces and rounded radii.
- C diagnostics flag unresolved includes and TODO/FIXME markers.
- Completion offers widgets in `.aroma` files and AromaUI calls in C.

Try opening `src/notes.c`, placing the caret after `notes_`, and pressing
`Ctrl+Space` to see project-aware suggestions.
