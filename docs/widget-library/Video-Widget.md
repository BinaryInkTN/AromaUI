# Video Widget

Plays video files and live frame streams inside your UI, with playback controls, seeking, looping, and declarative Incense support.

## Getting Started

```c
AromaNode *video = aroma_video_create(root, "assets/clip.mp4", 20, 60, 320, 180);
aroma_video_set_looping(video, true);
aroma_video_play(video);
```

Or one line with autoplay:

```c
AromaNode *video = aroma_ui_video(root, "assets/clip.mp4", 20, 60, 320, 180,
                                  true /* autoplay */, true /* loop */);
```

## Supported Formats

File decoding is handled by a **vendored FFmpeg build** (`vendors/ffmpeg`, built from source as a static library — no system FFmpeg required):

| Container | Examples |
|---|---|
| MP4 / MOV / M4V / 3GP | `.mp4`, `.mov`, `.m4a` audio ignored |
| Matroska / WebM | `.mkv`, `.webm` |
| AVI / FLV / WMV | `.avi`, `.flv`, `.wmv`, `.asf` |
| MPEG-PS / MPEG-TS | `.mpg`, `.mpeg`, `.ts`, `.m2ts` |
| Ogg / WAV | `.ogv`, `.wav` (video streams) |

| Video codec | Notes |
|---|---|
| H.264 / AVC | most common, incl. MP4 and MKV |
| H.265 / HEVC | 4K content |
| VP8 / VP9, AV1 | WebM and modern MKV/MP4 |
| MPEG-1 / MPEG-2 / MPEG-4 | incl. `.mpg` files |
| Theora, MJPEG, WMV7-9, VC-1, VP6, H.263, GIF | legacy and web formats |

Audio tracks are skipped (the widget is muted by default and always video-only in this release). If FFmpeg is disabled (`AROMA_ENABLE_FFMPEG=OFF`, or on platforms without it such as ESP32), the widget falls back to the built-in MPEG-1 decoder (`.mpg` with MPEG-1 video), and finally to frame-push mode.

Convert anything to a guaranteed format with:

```bash
ffmpeg -i input.mov -c:v mpeg1video output.mpg   # smallest, plays everywhere
ffmpeg -i input.mov -c:v libx264 out.mp4         # needs the FFmpeg build
```

## Live Frame Streams

No file? Push your own RGBA frames (camera, screen share, custom decoder, remote stream):

```c
AromaNode *video = aroma_video_create(root, NULL, 20, 60, 320, 180);
/* ... every frame ... */
aroma_video_push_frame(video, rgba_pixels, frame_w, frame_h);
```

Frames upload through an in-place texture update (`update_image_rgba`), so same-size pushes never reallocate the GPU texture.

## Playback Controls (built in)

Every player ships a modern overlay: a round play/pause button, a
scrubbable progress bar, and current-time / duration labels. Tapping the
video toggles play/pause and brings the controls up; they auto-hide after
3 seconds while playing (paused and finished states keep them visible).
Dragging the bar seeks with pause-while-scrubbing.

```c
aroma_video_set_font(video, font);          // enables the time labels
aroma_video_set_show_controls(video, false); // chromeless mode
aroma_video_set_controls_timeout(video, 5000);
```

| Function | Purpose |
|---|---|
| `aroma_video_set_font(node, font)` | Font for the time labels (optional) |
| `aroma_video_set_show_controls(node, show)` | Master switch (also an Incense `controls:` prop) |
| `aroma_video_set_controls_visible(node, visible)` | Show/hide programmatically |
| `aroma_video_set_controls_timeout(node, ms)` | Auto-hide delay (`0` = never) |

## End Of Video

A finished non-looping video holds its last frame and reports
`AROMA_VIDEO_FINISHED` (plus the `on_finished` callback). Calling play
again replays from the start. Looping videos wrap seamlessly with the
clock reset, so position and progress continue without jumps or stalls.

## API Reference

| Function | Purpose |
|---|---|
| `aroma_video_create(parent, path, x, y, w, h)` | Open a file, or pass `NULL` for push mode |
| `aroma_video_create_from_memory(parent, data, len, ...)` | Open from a memory buffer |
| `aroma_video_set_source(node, path)` | Switch to another file |
| `aroma_video_play / pause / stop` | Transport controls |
| `aroma_video_seek(node, seconds)` | Jump to a timestamp |
| `aroma_video_set_looping / get_looping` | Loop playback |
| `aroma_video_push_frame(node, rgba, w, h)` | Feed an external RGBA frame |
| `aroma_video_get_duration / get_position / get_framerate` | Stream info |
| `aroma_video_get_video_width / height` | Decoded frame size |
| `aroma_video_set_scale_mode` (`fill`, `fit`, `cover`) | Aspect handling |
| `aroma_video_set_corner_radius` | Rounded corners |
| `aroma_video_set_click_to_toggle` | Tap toggles play/pause (default on) |
| `aroma_video_set_on_finished / on_error / on_frame` | Lifecycle callbacks |

## Incense (declarative UI)

```aroma
Video {
    id: "player"
    src: "assets/clip.mp4"
    x: 20 y: 84 width: 320 height: 180
    scale: "fit" corner_radius: 12
    autoplay: true loop: true muted: true
}
```

Supported properties: `src`, `x`, `y`, `width`, `height`, `scale` (`fill`/`fit`/`cover`), `corner_radius` (or `radius`), `autoplay`, `loop`, `muted`, `click_to_toggle`, plus the standard `z_index` / `visible` / animation props. Omit `src` for a push-mode stream driven from C.

## How It Works

Decoding runs on the UI thread, driven by a 33 ms timer that decodes forward by the elapsed wall-clock time — video stays in sync without worker threads or locking. Decoded frames convert to RGBA (libswscale for FFmpeg, `plm_frame_to_rgba` for MPEG-1) and stream into a single reused GPU texture. Clicking the widget toggles play/pause; seeking flushes the decoder and lands on the first frame at or after the target.

## Example

See `examples/14_video` — a synthetic live stream with Play/Pause/Stop buttons and a status label, plus notes on loading `.mpg`/`.mp4` files.
