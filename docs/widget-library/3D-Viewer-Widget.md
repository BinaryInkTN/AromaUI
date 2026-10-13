The `Aroma3DViewer` widget provides an interactive viewport for rendering 3D models using AromaUI's built-in software rasterizer. It supports camera orbit, zoom, pan, and auto-rotation, making it suitable for product visualization, automotive configurators, and diagnostic overlays.

## Live Demonstration

A small 3D studio below: full-bleed viewport with an `Object Mode`
chip and `Reset` in a top bar, Fox/Cube object tabs, then one panel
with plain-word sliders. `Orbit`, `Tilt` and `Zoom` drive the visible
model only, so Fox and Cube keep independent cameras. `Light X/Y/Z`
move the scene-wide light, and every row shows a live readout.
Touching a camera control stops the turntable so it never fights you;
`Reset` restores the spin:

```aroma
ThreeDViewer {
    id: "model_fox"
    x: 0
    y: 48
    width: 320
    height: 200
    model: fox
    auto_rotate: true
    interactive: true
}
Slider {
    id: "s_orbit"
    x: 88
    y: 312
    width: 150
    height: 20
    min: 0
    max: 628
    value: 0
    on_change: "studio_orbit"
}
```

```incense-demo viewer3d
```

The `studio_*` callbacks live in `examples/incense_sandbox/sandbox.c`:
sliders report int values, so radians and light positions ride scaled
(x100). Each camera `on_change` reads its slider, applies the field to
the visible viewer only via `aroma_3d_viewer_get_camera` / `set_camera`,
and refreshes its readout with `aroma_label_set_text`. View presets are
`IconButton`s, which deliver no node handle, so the axis rides in the
callback userdata (`INCENSE_CALLBACK_VOID_PTR`). Copy the pattern into
your own app to get the same bench natively.

The embedded bytes live in `src/core/aroma_fox_model.h` (regenerate with
`python3 tools/glb_to_h.py Fox.glb aroma_fox_glb src/core/aroma_fox_model.h`).
Fox mesh CC0 by PixelMannen; rigging/animation CC-BY 4.0 by tomkranis.

## Incense: Local Files and Camera

`ThreeDViewer` accepts a local filesystem path via `model` (or its
`src` alias). Anything that is not `cube` / `fox` is passed to
`aroma_3d_load_model()`:

```aroma
ThreeDViewer {
    x: 20
    y: 76
    width: 280
    height: 280
    model: "./assets/car.glb"
    auto_rotate: false
    interactive: true
}
```

Camera props override the auto-fit that runs on every `set_model`.
Angles are radians, matching `Aroma3DCamera` (`theta: 0` faces +Z,
`phi: 1.0` is slightly above the horizon). Both plain and `cam_*`
spellings work:

```aroma
ThreeDViewer {
    x: 20
    y: 76
    width: 280
    height: 280
    model: cube
    theta: 0.6
    phi: 1.1
    radius: 4.0
    target_x: 0.0
    target_y: 0.0
    target_z: 0.0
    fov: 45.0
    light_x: 1.0
    light_y: 1.0
    light_z: 1.0
    auto_rotate: false
    interactive: true
}
```

| Prop | Alias | Default | Description |
| --- | --- | --- | --- |
| `model` | `src` | - | `cube`, `fox`, or a local `.glb` path. |
| `theta` | `cam_theta` | auto-fit | Horizontal orbit angle (radians). |
| `phi` | `cam_phi` | auto-fit | Vertical orbit angle (radians). |
| `radius` | `cam_radius` | auto-fit | Distance from target. |
| `target_x/y/z` | `cam_target_*` | bounds center | Look-at point. |
| `fov` | `cam_fov` | `45.0` | Field of view (degrees). |
| `near` / `near_plane` | `cam_near*` | `0.1` | Near clip plane. |
| `far` / `far_plane` | `cam_far*` | `1000.0` | Far clip plane. |
| `light_x/y/z` | - | `1.0` | Point light position in model units. |

The light is a point in model units, so keep it outside the model
bounds: a light buried inside a large mesh lights almost nothing (every
surface faces away from it). As a rule of thumb, park it a few
model-sizes out. The live demo lights the 155-unit fox from
`(150, 200, 150)`.

In the browser sandbox the filesystem is in-memory, so local paths
resolve to whatever the host preloaded there - use native builds for
real `.glb` files on disk.

## Component Overview

The viewer pairs a widget (input, layout, and display) with a 3D core
(models, cameras, and rendering). You create a viewer, assign a model,
and control the camera; loading, interaction, and drawing are handled
for you.

---

## Camera System

The `Aroma3DCamera` structure defines the viewpoint using spherical coordinates and supports orbit, zoom, and pan operations.

### Camera Structure

| Field | Type | Description |
| --- | --- | --- |
| `theta` | `float` | Horizontal angle in radians. |
| `phi` | `float` | Vertical angle in radians. |
| `radius` | `float` | Distance from the target point. |
| `target[3]` | `float[3]` | The (x, y, z) point the camera looks at. |
| `fov` | `float` | Field of view in degrees. |
| `near_plane` | `float` | Near clipping plane distance. |
| `far_plane` | `float` | Far clipping plane distance. |

### Camera Operations

| Function | Description |
| --- | --- |
| `aroma_3d_camera_init` | Resets camera to default position looking at origin. |
| `aroma_3d_camera_orbit` | Adjusts theta/phi by delta values (used for drag rotation). |
| `aroma_3d_camera_zoom` | Changes radius by delta (used for scroll wheel). |
| `aroma_3d_camera_pan` | Moves the target point in screen space. |
| `aroma_3d_camera_update_view` | Computes the 4x4 view matrix. |
| `aroma_3d_camera_update_proj` | Computes the 4x4 projection matrix. |

**Sources:**[include/aroma_3d.h16-25](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_3d.h#L16-L25)[src/core/aroma_3d.c1-80](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_3d.c#L1-L80)

---

## Model Management

Models are loaded from disk or memory and contain one or more meshes. Each mesh can have material properties such as metallic and clearcoat.

### Loading Models

```c
// Synchronous loading
Aroma3DModel *model = aroma_3d_load_model("/path/to/car.glb");
// Asynchronous loading (non-blocking)
Aroma3DLoadJob *job = aroma_3d_load_model_async("/path/to/car.glb");
while (!aroma_3d_load_model_poll(job)) {
    // Process other UI frames while loading
    aroma_ui_process_events();
    aroma_ui_render(window);
}
Aroma3DModel *model = aroma_3d_load_model_finish(job);
```

No model file handy? `aroma_3d_create_cube()` builds a simple colored
cube for testing, `aroma_3d_create_fox()` loads the embedded Khronos Fox
sample, and Incense accepts `model: cube` / `model: fox` / `src: cube`
on `ThreeDViewer` to use them (see the live demo above). Any other
`model:` value is a local path loaded with `aroma_3d_load_model()`.

Both indexed and non-indexed triangle meshes are supported: primitives
without an `indices` array (like the Fox) render with `glDrawArrays`.

### Model API

| Function | Description |
| --- | --- |
| `aroma_3d_load_model` | Loads a model from a filesystem path. |
| `aroma_3d_load_model_from_memory` | Loads a model from a memory buffer. |
| `aroma_3d_create_cube` | Creates a simple colored cube for testing. |
| `aroma_3d_destroy_model` | Frees all meshes and model data. |
| `aroma_3d_get_mesh_count` | Returns the number of meshes in the model. |
| `aroma_3d_get_mesh` | Returns a specific mesh by index. |
| `aroma_3d_set_mesh_metallic` | Sets the metallic factor of a mesh. |
| `aroma_3d_set_mesh_clearcoat` | Sets the clearcoat factor of a mesh. |
| `aroma_3d_get_model_bounds` | Retrieves the bounding box (min/max) of the model. |

**Sources:**[include/aroma_3d.h13-39](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_3d.h#L13-L39)[src/core/aroma_3d.c200-260](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_3d.c#L200-L260)

---

## Viewer Widget API

The `Aroma3DViewer` widget wraps the 3D core into an `AromaNode` with built-in event handling and rendering.

### Creation and Destruction

| Function | Description |
| --- | --- |
| `aroma_3d_viewer_create` | Creates a new viewer node as a child of `parent`. |
| `aroma_3d_viewer_set_model` | Assigns a model to the viewer; auto-fits camera to bounds. |
| `aroma_3d_viewer_get_model` | Retrieves the currently assigned model. |

### Camera Control

| Function | Description |
| --- | --- |
| `aroma_3d_viewer_set_camera` | Overrides the camera with a specific configuration. |
| `aroma_3d_viewer_get_camera` | Reads the current camera state into `out_camera`. |
| `aroma_3d_viewer_reset_camera` | Resets camera to default orbit position and radius. |
| `aroma_3d_viewer_set_auto_rotate` | Enables/disables continuous horizontal rotation. |
| `aroma_3d_viewer_set_light_position` | Sets the directional light position for shading. |

### Interaction Control

| Function | Description |
| --- | --- |
| `aroma_3d_viewer_set_interactive` | Enables/disables user input (drag, scroll). |
| `aroma_3d_viewer_get_interactive` | Returns whether the viewer accepts input. |
| `aroma_3d_viewer_update` | Requests a redraw of the viewer node. |

### Input Handling

The viewer subscribes to the following events with high priority to ensure smooth interaction:

- `EVENT_TYPE_MOUSE_MOVE` / `EVENT_TYPE_TOUCH_MOVE` - Orbit camera when dragging.
- `EVENT_TYPE_MOUSE_CLICK` / `EVENT_TYPE_TOUCH_DOWN` - Start drag.
- `EVENT_TYPE_MOUSE_RELEASE` / `EVENT_TYPE_TOUCH_UP` - End drag.
- `EVENT_TYPE_MOUSE_SCROLL` - Zoom camera.
- `EVENT_TYPE_MOUSE_EXIT` - Cancel drag if cursor leaves window.

**Sources:**[src/widgets/aroma_3d_viewer.c31-79](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/widgets/aroma_3d_viewer.c#L31-L79)[include/widgets/aroma_3d_viewer.h14-25](https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/widgets/aroma_3d_viewer.h#L14-L25)

---

## Rendering Approach

3D content renders straight into the widget rectangle with a built-in
software rasterizer: no GPU-specific setup on your side. Depth testing,
shading, and texturing are included.

---

## Usage Example: Automotive Camera
The Car Infotainment example uses the 3D viewer for the vehicle exterior and interior views. The camera is animated between predefined states to simulate orbit, pan, and zoom transitions.

```c
// Exterior camera state
state.camera.theta = VEHICLE_CAM_EXTERIOR_THETA;
state.camera.phi   = VEHICLE_CAM_EXTERIOR_PHI;
state.camera.radius = VEHICLE_CAM_EXTERIOR_RADIUS;
state.camera.target[0] = VEHICLE_CAM_EXTERIOR_TARGET_X;
state.camera.target[1] = VEHICLE_CAM_EXTERIOR_TARGET_Y;
state.camera.target[2] = VEHICLE_CAM_EXTERIOR_TARGET_Z;

// Animate to interior view
state.anim_target_theta = VEHICLE_CAM_INTERIOR_THETA;
state.anim_target_phi   = VEHICLE_CAM_INTERIOR_PHI;
state.anim_target_radius = VEHICLE_CAM_INTERIOR_RADIUS;
state.camera_animating = true;

// Apply camera each frame
aroma_3d_viewer_set_camera(state.viewer_3d, &state.camera);
```

**Sources:**[examples/car_infotainment/vehicle/vehicle_camera.c15-60](https://github.com/BinaryInkTN/AromaUI/blob/main/examples/car_infotainment/vehicle/vehicle_camera.c#L15-L60)[examples/car_infotainment/vehicle/vehicle_camera.c164-168](https://github.com/BinaryInkTN/AromaUI/blob/main/examples/car_infotainment/vehicle/vehicle_camera.c#L164-L168)

---

## Automatic Camera Fitting

Assigning a model frames it for you automatically:

1. The previous model is released.
2. The camera takes a default orbit position.
3. The target moves to the center of the model.
4. The distance fits the largest model dimension.
