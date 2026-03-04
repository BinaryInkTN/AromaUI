# Audio Playback

Play audio files and streams, and manage audio sessions with AromaUI.

## Features
- Playback control
- Stream support
- Session management

## Architecture

```
flowchart TD
    Play[Play Audio] --> Control[Control Playback]
    Control --> Stream[Stream Audio]
    Control --> Session[Manage Session]
```

## Example Usage
```c
aroma_audio_play(file);
aroma_audio_pause();
```

## API Reference
- aroma_audio_play(file)
- aroma_audio_pause()
- aroma_audio_stop()
