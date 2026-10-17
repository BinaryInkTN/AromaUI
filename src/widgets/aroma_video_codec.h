#ifndef AROMA_VIDEO_CODEC_H
#define AROMA_VIDEO_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaVideoCodec AromaVideoCodec;

typedef void (*AromaVideoCodecFrameCb)(const unsigned char *rgba,
                                       int width, int height,
                                       double pts_seconds,
                                       void *user_data);

AromaVideoCodec *aroma_video_codec_open_file(const char *path,
                                             AromaVideoCodecFrameCb cb,
                                             void *user_data,
                                             char *errbuf, size_t errlen);

AromaVideoCodec *aroma_video_codec_open_memory(const unsigned char *data,
                                               size_t len,
                                               AromaVideoCodecFrameCb cb,
                                               void *user_data,
                                               char *errbuf, size_t errlen);

void aroma_video_codec_close(AromaVideoCodec *c);

void aroma_video_codec_set_loop(AromaVideoCodec *c, bool loop);
void aroma_video_codec_set_muted(AromaVideoCodec *c, bool muted);

bool aroma_video_codec_step(AromaVideoCodec *c, double dt_seconds);

bool aroma_video_codec_ended(const AromaVideoCodec *c);
bool aroma_video_codec_seek(AromaVideoCodec *c, double time_seconds);
void aroma_video_codec_rewind(AromaVideoCodec *c);
double aroma_video_codec_duration(const AromaVideoCodec *c);
double aroma_video_codec_framerate(const AromaVideoCodec *c);

const char *aroma_video_codec_backend(const AromaVideoCodec *c);

bool aroma_video_codec_have_decoder(void);

#ifdef __cplusplus
}
#endif

#endif
