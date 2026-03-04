# Layout Management

AromaUI provides flexible layout management for responsive UIs. Layout containers support flexbox-like configuration and adaptive sizing.

## Features
- Flex and grid layouts
- Responsive sizing
- Alignment and justification
- Nested containers

## Architecture

```
flowchart TD
    Window[Window] --> Container[Container]
    Container --> Widget[Widget]
    Container --> Container2[Nested Container]
```

## Example Usage
```c
AromaNode* root = aroma_ui_window("App", 800, 600, false);
AromaNode* container = aroma_ui_container(root, 0, 0, 800, 600, FLEX, ROW, CENTER, START);
AromaNode* button = aroma_ui_button(container, "Click", 10, 10, 100, 40, on_click, NULL, font);
```

## API Reference
- aroma_ui_container(...)
- aroma_ui_window(...)
