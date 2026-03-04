# Animations

AromaUI provides smooth animations and transitions for enhanced user experience.

## Features
- Property animations
- Transition effects
- Custom animation curves

## Architecture

```
flowchart TD
    Trigger[Trigger Animation] --> Animate[Animate Properties]
    Animate --> Render[Render Frame]
```

## Example Usage
```c
aroma_ui_animate(widget, ANIM_FADE_IN, duration);
```

## API Reference
- aroma_ui_animate(widget, type, duration)
