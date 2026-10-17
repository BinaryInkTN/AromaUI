#ifndef AROMA_AUDIO_OUT_H
#define AROMA_AUDIO_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal PCM sink for video audio. 16-bit signed mono/stereo at the
// stream's native sample rate. All functions are safe to call from the
// video tick thread; open() is idempotent per format.
bool aroma_audio_out_open(int sample_rate_hz, int channels);
void aroma_audio_out_close(void);
// Interleaved s16 frames. Silently dropped when muted or not open.
void aroma_audio_out_write(const int16_t *pcm, size_t frames);
void aroma_audio_out_set_muted(bool muted);
bool aroma_audio_out_muted(void);
// Discard queued-but-unplayed PCM (call on seek/rewind/stop).
void aroma_audio_out_flush(void);
void aroma_audio_out_pause(bool pause);

#ifdef __cplusplus
}
#endif

#endif
