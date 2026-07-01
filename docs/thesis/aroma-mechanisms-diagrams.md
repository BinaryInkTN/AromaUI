---
marp: true
mermaid: true
paginate: true
---

# AromaUI Thesis Diagrams

This document collects Mermaid diagrams for the main AromaUI framework mechanisms.

## 1) Four-Layer Architecture

```mermaid
flowchart TD
    A[Application Layer\nWidget APIs and app logic]
    B[Core Framework\nScene graph, layout, events, animation]
    C[Backend ABI\nStable rendering and platform contracts]
    D[Platform and Graphics Backends\nGLFW or Android or TFT + GLES or Vulkan or TFT]

    A --> B
    B --> C
    C --> D
```

## 2) Frame Lifecycle (Retained Mode)

```mermaid
flowchart LR
    I[Input Collected] --> E[Events Queued]
    E --> P[Process Events and Update State]
    P --> L[Layout Pass]
    L --> R[Record Draw Commands]
    R --> S[Sort and Batch]
    S --> F[Flush to Backend]
    F --> V[Present Frame]
```

## 3) Scene Graph + Dirty Propagation

```mermaid
flowchart TD
    C[Node Property Change] --> D[Mark Node Dirty]
    D --> U[Propagate subtree_dirty to ancestors]
    U --> Q[Render phase checks dirty flags]
    Q --> H{Dirty branch?}
    H -- Yes --> T[Traverse and draw branch]
    H -- No --> K[Skip branch]
```

## 4) Event Dispatch with Bubbling

```mermaid
sequenceDiagram
    participant P as Platform Backend
    participant Q as Event Queue
    participant H as Hit Test
    participant N as Target Node
    participant A as Ancestor Nodes

    P->>Q: Push raw input event
    Q->>H: Resolve target from x,y
    H->>N: Dispatch to target listeners
    alt Consumed
        N-->>Q: Stop propagation
    else Not consumed
        N->>A: Bubble to parent chain
    end
```

## 5) Deferred Rendering via DrawList + ABI

```mermaid
flowchart LR
    W[Widget draw callback] --> X[ABI proxy draw call]
    X --> Y{DrawList active?}
    Y -- Yes --> Z[Record command in DrawList]
    Y -- No --> G[Immediate backend draw]
    Z --> B[Flush phase]
    B --> G
    G --> O[GPU or display output]
```

## 6) Layout Engine: Self-Layout + Container Modes

```mermaid
flowchart TD
    P[Parent bounds] --> S[Resolve child self-layout]
    S --> M{Container mode}
    M -- None --> A[Keep child absolute geometry]
    M -- Flex --> F[Main axis and cross axis placement]
    M -- Grid --> G[Compute row and col cells]
    F --> R[Recurse into child subtree]
    G --> R
    A --> R
```

## 7) Animation Engine Integrated with Redraw

```mermaid
flowchart LR
    T[16ms timer tick] --> U[Update active animations]
    U --> E[Apply easing function]
    E --> N[Write interpolated property to node]
    N --> D[Invalidate node]
    D --> R[Request redraw]
```

## 8) Theme and Style Resolution

```mermaid
flowchart TD
    P[Theme preset or custom palette] --> G[Set global theme]
    G --> S[Create or update component styles]
    S --> W[Widget render reads resolved colors, spacing, typography]
    W --> F[Visual consistency across app]
```

## 9) Deterministic Memory Strategy

```mermaid
flowchart LR
    R[Allocation request] --> C{Object class?}
    C -- Node or small widget --> S[Slab allocator pool]
    C -- Large or uncommon --> H[General heap fallback]
    S --> O[Fast path from free list]
    O --> D[Predictable runtime behavior]
```

## 10) Embedded Smart Flush (Tile-Based Rendering)

```mermaid
flowchart TD
    A[Dirty region reported] --> T[Map region to tile set]
    T --> L[For each dirty tile]
    L --> C[Set clip to tile bounds]
    C --> R[Replay DrawList commands intersecting tile]
    R --> P[Push tile buffer to display]
```
