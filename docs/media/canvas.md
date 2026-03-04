# Canvas Drawing

AromaUI provides a 2D drawing API for custom graphics and visualizations.

## Features
- Draw shapes and paths
- Custom rendering
- Layer management

## Architecture

```
flowchart TD
    Canvas[Canvas] --> Draw[Draw Shape]
    Draw --> Render[Render]
    Canvas --> Layer[Manage Layers]
```

## Example Usage
```c
aroma_canvas_draw_line(canvas, x1, y1, x2, y2);
aroma_canvas_draw_rect(canvas, x, y, w, h);
```

## API Reference
- aroma_canvas_draw_line(canvas, x1, y1, x2, y2)
- aroma_canvas_draw_rect(canvas, x, y, w, h)
- aroma_canvas_draw_circle(canvas, x, y, r)
