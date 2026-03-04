# Gestures & Touch

Handle touch events, gestures, and multi-touch interactions in AromaUI.

## Features
- Tap, swipe, pinch, drag
- Multi-touch support
- Gesture callbacks

## Architecture

```
flowchart TD
    Touch[Touch Event] --> Recognize[Gesture Recognizer]
    Recognize --> Callback[Callback]
```

## Example Usage
```c
aroma_ui_set_gesture(widget, GESTURE_SWIPE, on_swipe);
```

## API Reference
- aroma_ui_set_gesture(widget, gesture, callback)
