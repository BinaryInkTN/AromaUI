
Every UI element in AromaUI is an `AromaNode`. Nodes form a tree, and the framework traverses this tree for layout, hit-testing, and rendering.

## Node Structure

Each node holds its type (root, container, or widget), a unique id,
geometry, layout hints, visibility, Z-index, and opacity. You work with
these through functions, never by touching fields directly.

**Key limits:**
- **128 children per node** (`AROMA_MAX_CHILD_NODES`)
- **64 properties per widget** in Incense markup
- **1024 dirty nodes per frame** (`AROMA_MAX_DIRTY_NODES`)

Read geometry with `aroma_node_get_rect(node)` and toggle visibility with
`aroma_node_set_hidden()`.

## Node Lifecycle

### Creation

```c
AromaNode *container = aroma_container_create(root, 0, 0, 800, 480);
```

Build nodes with the widget factory functions in `include/aroma_ui.h`
(containers, buttons, labels, …). The framework manages node memory for
you, including on small embedded targets.

### Parenting

Factories attach the new node to its parent automatically.

- Child limit is enforced at 128 (`AROMA_MAX_CHILD_NODES`).
- Invalidating a node refreshes its whole branch up to the root.

### Destruction

```c
aroma_ui_destroy_window(window);
```

Destroying a window recursively destroys its subtree.

## Dirty Region Tracking

Only dirty nodes are processed during layout and rendering.

```c
void aroma_node_invalidate(AromaNode *node);
```

Marks the node and refreshes its ancestors. The renderer skips branches that need no work.

## Memory Management

Node and widget memory is managed for you in fixed pools, sized to run
well on small embedded targets as well as desktops. There is nothing to
configure; the limits listed above are the only constraints to design
around.

## Node APIs

Use these functions to arrange and control nodes directly:

| Function | Role |
|---|---|
| `aroma_node_invalidate()` | Marks a node dirty and propagates `subtree_dirty` upward |
| `aroma_node_set_layout_mode()` | Selects NONE, FLEX, or GRID child arrangement |
| `aroma_node_set_layout_fill()` | Match parent bounds |
| `aroma_node_set_layout_center()` | Center within parent |
| `aroma_node_set_layout_anchor()` | Pin to parent edges |
| `aroma_node_set_z_index()` | Controls draw order |
| `aroma_node_set_hidden()` | Toggles visibility without destruction |

Most applications should use the higher-level factory functions in `include/aroma_ui.h` instead of calling these directly.

## Next Steps

- Learn how [Events](Event-System.md) are dispatched to nodes.
- Understand [Layout](Layout-Engine.md) calculation.
- See how nodes become pixels in the [Rendering Pipeline](Rendering-Pipeline-and-DrawList.md).
