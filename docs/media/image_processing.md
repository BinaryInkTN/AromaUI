# Image Processing

Resize, crop, filter, and manipulate images efficiently with AromaUI.

## Features
- Resize and crop
- Filters and effects
- Format conversion

## Architecture

```
flowchart TD
    Load[Load Image] --> Process[Process Image]
    Process --> Filter[Apply Filter]
    Process --> Resize[Resize/Crop]
    Process --> Convert[Convert Format]
```

## Example Usage
```c
aroma_image_load(file);
aroma_image_resize(image, width, height);
```

## API Reference
- aroma_image_load(file)
- aroma_image_resize(image, width, height)
- aroma_image_filter(image, filter)
- aroma_image_convert(image, format)
