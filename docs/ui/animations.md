AromaUI provides a built-in, lightweight animation engine to create smooth and fluid user experiences. Animations can be applied to any `AromaNode` to transition its properties (like position, size, or opacity) over a specified duration.

## Animation Types

AromaUI supports several built-in animation types out of the box, defined in the `AromaAnimationType` enum:

- `AROMA_ANIM_SLIDE_X`: Animates the `x` coordinate of the node.
- `AROMA_ANIM_SLIDE_Y`: Animates the `y` coordinate of the node.
- `AROMA_ANIM_SCALE_X`: Animates the `width` of the node.
- `AROMA_ANIM_SCALE_Y`: Animates the `height` of the node.
- `AROMA_ANIM_FADE`: Animates the `opacity` of the node.
- `AROMA_ANIM_CUSTOM`: Allows you to animate an arbitrary value and apply it manually via a callback.
- `AROMA_ANIM_NONE`: No animation.

## Easing Functions

Easings define the rate of change of a parameter over time. AromaUI provides the following easing functions (`AromaEasingType`):

- `AROMA_EASE_LINEAR`: Linear constant speed.
- `AROMA_EASE_IN_QUAD`: Starts slow, accelerates.
- `AROMA_EASE_OUT_QUAD`: Starts fast, decelerates.
- `AROMA_EASE_IN_OUT_QUAD`: Starts slow, accelerates, then decelerates.
- `AROMA_EASE_OUT_CUBIC`: Fluid deceleration (default for many UI elements).
- `AROMA_EASE_OUT_BACK`: Overshoots slightly and then settles back.
- `AROMA_EASE_OUT_ELASTIC`: Elastic bounce effect.

## Starting an Animation

You can start a basic animation using `aroma_animation_start`. 

```c
// Example: Slide a card in from the left side of the screen
AromaAnimation* anim = aroma_animation_start(
    card_node,              // Target AromaNode
    AROMA_ANIM_SLIDE_X,     // Type of animation
    -350.0f,                // Start value
    20.0f,                  // End value
    300                     // Duration in milliseconds
);

// Optional: Change the easing from the default
aroma_animation_set_easing(anim, AROMA_EASE_OUT_BACK);
```

## Stopping an Animation

If you need to interrupt an animation before it naturally finishes, you can call:

```c
aroma_animation_stop(card_node);
```

## Custom Animations

If you need to animate a property that isn't covered by the default types (e.g. rotating an element, blending colors, or animating custom widget data), you can use custom animations.

```c
void my_custom_anim_cb(AromaNode* target, float current_val, void* user_data) {
    // Apply `current_val` to your data structure
    AromaRect* my_widget_data = (AromaRect*)target->node_widget_ptr;
    // e.g. update a custom offset, angle or size
}

// Start custom animation
AromaAnimation* custom_anim = aroma_animation_start_custom(
    target_node,      // Custom AromaNode
    0.0f,             // Start value
    100.0f,           // End value
    500,              // 500ms duration
    my_custom_anim_cb,// The callback that applies the value
    NULL              // User context data
);
```

## Internal workings

The animation engine is driven by the main UI thread. During each frame layout and render pass, the animation manager updates all active animations, computes the eased values, writes them to the node struct or calls the custom callbacks, and automatically invalidates the dirty rects to ensure the screen redraws cleanly.

