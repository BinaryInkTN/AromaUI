#ifndef GLPS_AUDIO_STREAM_H
#define GLPS_AUDIO_STREAM_H

#include "glps_common.h"


/**
 * Initializes a new audio manager instance
 * @param device_name The ALSA device name (NULL for default)
 * @return Pointer to initialized audio manager or NULL on failure
 */
glps_audio_stream *glps_audio_stream_init(const char *device_name,
                                          int buffer_frames,
                                          int sample_rate,
                                          int channels,
                                          int bits_per_sample,
                                          int buffer_size);

/**
 * Plays an audio file
 * @param am Audio manager instance
 * @param audio_file_path Path to audio file
 * @param sample_rate Sample rate (0 to use file's native rate)
 * @param channels Channel count (0 to use file's native channels)
 * @param bits_per_sample Bits per sample
 * @param buffer_size Audio buffer size in bytes
 * @return 0 on success, negative error code on failure
 */
int glps_audio_stream_play(glps_audio_stream *am, const char *audio_file_path,
                           int sample_rate, int channels, int bits_per_sample,
                           int buffer_size);
/**
 * Stops current playback
 * @param am Audio manager instance
 */
void glps_audio_stream_stop(glps_audio_stream *am);

/**
 * Pauses current playback
 * @param am Audio manager instance
 */
void glps_audio_stream_pause(glps_audio_stream *am);

/**
 * Resumes paused playback
 * @param am Audio manager instance
 */
void glps_audio_stream_resume(glps_audio_stream *am);

/**
 * Sets playback volume
 * @param am Audio manager instance
 * @param volume Volume level (0.0 to 1.0)
 */
void glps_audio_stream_set_volume(glps_audio_stream *am, float volume);

/**
 * Sets playback position
 * @param am Audio manager instance
 * @param position Position in samples
 */
void glps_audio_stream_set_position(glps_audio_stream *am, unsigned int position);

/**
 * Destroys audio manager and releases all resources
 * @param am Audio manager instance
 */
void glps_audio_stream_destroy(glps_audio_stream *am);

#endif