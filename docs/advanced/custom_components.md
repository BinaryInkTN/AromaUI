# Custom Components

Create reusable custom UI components and widgets in AromaUI.

## Features
- Component inheritance
- Custom rendering
- Event handling

## Architecture

```
flowchart TD
    Base[Base Component] --> Inherit[Inherit]
    Inherit --> Render[Custom Render]
    Inherit --> Event[Event Handling]
```

## Example Usage
```c
aroma_component_create(type, props);
```

## API Reference
- aroma_component_create(type, props)
