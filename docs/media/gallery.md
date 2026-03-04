# Gallery Access

Browse, select, and manage media from the device gallery using AromaUI APIs.

## Features
- Media browsing
- Selection and filtering
- Thumbnail generation

## Architecture

```
flowchart TD
    Gallery[Gallery] --> Browse[Browse Media]
    Browse --> Select[Select Item]
    Select --> Manage[Manage Media]
```

## Example Usage
```c
aroma_gallery_browse();
aroma_gallery_select(item_id);
```

## API Reference
- aroma_gallery_browse()
- aroma_gallery_select(item_id)
