# Video Playback

Play videos with custom controls and subtitle support using AromaUI.

## Features
- Playback controls
- Subtitle support
- Custom UI integration

## Architecture

```
flowchart TD
    Play[Play Video] --> Control[Control Playback]
    Control --> Subtitle[Subtitle Support]
    Control --> UI[Custom UI]
```

## Example Usage
```c
aroma_video_play(file);
aroma_video_set_subtitle(file, subtitle);
```

## API Reference
- aroma_video_play(file)
- aroma_video_set_subtitle(file, subtitle)
