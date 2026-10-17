#ifndef AROMA_VIDEO_H
#define AROMA_VIDEO_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "widgets/aroma_image.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaFont AromaFont;

typedef enum AromaVideoState {
    AROMA_VIDEO_STOPPED = 0,
    AROMA_VIDEO_PLAYING = 1,
    AROMA_VIDEO_PAUSED = 2,
    AROMA_VIDEO_FINISHED = 3,
    AROMA_VIDEO_ERROR = 4
} AromaVideoState;

typedef void (*AromaVideoFinishedCb)(AromaNode *node, void *user_data);
typedef void (*AromaVideoErrorCb)(AromaNode *node, const char *message, void *user_data);
typedef void (*AromaVideoFrameCb)(AromaNode *node, double presentation_time, void *user_data);

AromaNode *aroma_video_create(AromaNode *parent, const char *video_path,
                              int x, int y, int width, int height);

AromaNode *aroma_video_create_from_memory(AromaNode *parent,
                                           unsigned char *data, size_t data_size,
                                           int x, int y, int width, int height);

void aroma_video_set_source(AromaNode *video_node, const char *video_path);

bool aroma_video_push_frame(AromaNode *video_node,
                             const unsigned char *rgba,
                             int frame_width, int frame_height);

void aroma_video_play(AromaNode *video_node);
void aroma_video_pause(AromaNode *video_node);
void aroma_video_stop(AromaNode *video_node);
bool aroma_video_seek(AromaNode *video_node, double time_seconds);

void aroma_video_set_looping(AromaNode *video_node, bool loop);
bool aroma_video_get_looping(AromaNode *video_node);

void aroma_video_set_muted(AromaNode *video_node, bool muted);
bool aroma_video_get_muted(AromaNode *video_node);
// muted=false plays the file's audio track (44.1kHz stereo PCM sink:
// ALSA on Linux, OpenSL ES on Android); muted=true drops PCM immediately.

// Tap/click behavior. By default a tap on the video only brings up the
// on-screen controls (never toggling playback, matching touch platforms);
// call with enabled=true to make a plain tap/click also toggle play/pause.
// The OSD play/pause button always works regardless of this setting.
void aroma_video_set_click_to_toggle(AromaNode *video_node, bool enabled);

void aroma_video_set_on_finished(AromaNode *video_node,
                                  AromaVideoFinishedCb cb, void *user_data);
void aroma_video_set_on_error(AromaNode *video_node,
                               AromaVideoErrorCb cb, void *user_data);
void aroma_video_set_on_frame(AromaNode *video_node,
                               AromaVideoFrameCb cb, void *user_data);

AromaVideoState aroma_video_get_state(AromaNode *video_node);
bool aroma_video_is_playing(AromaNode *video_node);
double aroma_video_get_duration(AromaNode *video_node);
double aroma_video_get_position(AromaNode *video_node);
double aroma_video_get_framerate(AromaNode *video_node);
int aroma_video_get_video_width(AromaNode *video_node);
int aroma_video_get_video_height(AromaNode *video_node);
unsigned int aroma_video_get_texture_id(AromaNode *video_node);
const char *aroma_video_get_source(AromaNode *video_node);
// Human-readable failure reason, or NULL/"" when healthy. Useful on
// Android where logcat is not always at hand (e.g. show it in a label).
const char *aroma_video_get_error(AromaNode *video_node);

void aroma_video_set_corner_radius(AromaNode *video_node, float radius);
float aroma_video_get_corner_radius(AromaNode *video_node);
void aroma_video_set_scale_mode(AromaNode *video_node, AromaImageScaleMode mode);
AromaImageScaleMode aroma_video_get_scale_mode(AromaNode *video_node);
void aroma_video_set_size(AromaNode *video_node, int width, int height);
void aroma_video_set_position(AromaNode *video_node, int x, int y);

void aroma_video_set_font(AromaNode *video_node, AromaFont *font);
void aroma_video_set_show_controls(AromaNode *video_node, bool show);
bool aroma_video_get_show_controls(AromaNode *video_node);
void aroma_video_set_controls_visible(AromaNode *video_node, bool visible);
bool aroma_video_get_controls_visible(AromaNode *video_node);
void aroma_video_set_controls_timeout(AromaNode *video_node, uint32_t timeout_ms);

void aroma_video_draw(AromaNode *video_node, size_t window_id);
void aroma_video_destroy_node(AromaNode *node);

#ifdef __cplusplus
}
#endif

#endif
