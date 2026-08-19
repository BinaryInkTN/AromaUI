/*
 Copyright (c) 2025 Yassine Ahmed Ali

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * @file glps_audio_stream.c
 * @brief Audio stream management for GLPS.
 * @date 2025-04-25
 */

#include "glps_audio_stream.h"
#include "glps_common.h"
#include "glps_thread.h"
#include "utils/logger/pico_logger.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define DR_MP3_IMPLEMENTATION
#include "utils/audio/dr_mp3.h"

#define BUFFER_FRAMES 4096
#define DEFAULT_DEVICE "default"
#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_CHANNELS 2
#define DEFAULT_BITS_PER_SAMPLE 16
#define DEFAULT_BUFFER_SIZE 4096
#define DEFAULT_VOLUME 1.0f
#define DEFAULT_POSITION 0

glps_audio_stream *glps_audio_stream_init(const char *device_name,
                                          int buffer_frames,
                                          int sample_rate,
                                          int channels,
                                          int bits_per_sample,
                                          int buffer_size)
{
    glps_audio_stream *audio_stream = (glps_audio_stream *)malloc(sizeof(glps_audio_stream));
    if (!audio_stream)
    {
        fprintf(stderr, "Failed to allocate memory for audio manager\n");
        return NULL;
    }

    audio_stream->device_name = device_name ? device_name : DEFAULT_DEVICE;
    audio_stream->buffer_frames = buffer_frames > 0 ? buffer_frames : BUFFER_FRAMES;
    audio_stream->sample_rate = sample_rate > 0 ? sample_rate : DEFAULT_SAMPLE_RATE;
    audio_stream->channels = channels > 0 ? channels : DEFAULT_CHANNELS;
    audio_stream->bits_per_sample = bits_per_sample > 0 ? bits_per_sample : DEFAULT_BITS_PER_SAMPLE;
    audio_stream->buffer_size = buffer_size > 0 ? buffer_size : DEFAULT_BUFFER_SIZE;
    audio_stream->volume = DEFAULT_VOLUME;
    audio_stream->position = DEFAULT_POSITION;
    audio_stream->pcm_buffer = (float *)malloc(audio_stream->buffer_frames * audio_stream->channels * sizeof(float));
    if (audio_stream->pcm_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for PCM buffer\n");
        free(audio_stream);
        return NULL;
    }
    audio_stream->state_mutex = malloc(sizeof(gthread_mutex_t));
    audio_stream->volume_mutex = malloc(sizeof(gthread_mutex_t));
    audio_stream->is_paused = false;
    audio_stream->is_stopped = false;
    audio_stream->is_playing = true;
    audio_stream->mp3 = NULL;
    audio_stream->pcm_handle = NULL;
    audio_stream->audio_file_path = NULL;
    audio_stream->thread = NULL;

    glps_thread_mutex_init(audio_stream->state_mutex, NULL);
    glps_thread_mutex_init(audio_stream->volume_mutex, NULL);

    return audio_stream;
}

void *__audio_thread_func(void *arg)
{
    glps_audio_stream *am = (glps_audio_stream *)arg;
    snd_pcm_t *handle = (snd_pcm_t *)am->pcm_handle;

    if (am->thread == NULL)
    {
        fprintf(stderr, "Failed to create audio thread\n");
        return NULL;
    }

    if (!am || !am->audio_file_path)
    {
        return NULL;
    }

    if (am->channels <= 0 || am->bits_per_sample <= 0 || am->buffer_size <= 0)
    {
        return NULL;
    }

    int err = 0;

    am->mp3 = calloc(1, sizeof(drmp3));

    drmp3 *mp3 = am->mp3;

    if (!drmp3_init_file(mp3, am->audio_file_path, NULL))
    {
        printf("Failed to open MP3 file\n");
        free(am->pcm_buffer);
        return NULL;
    }

    am->sample_rate = mp3->sampleRate;
    am->channels = mp3->channels;

    if ((err = snd_pcm_open(&handle, am->device_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        printf("Playback open error: %s\n", snd_strerror(err));
        free(am->pcm_buffer);
        drmp3_uninit(mp3);
        return NULL;
    }

    if ((err = snd_pcm_set_params(handle,
                                  SND_PCM_FORMAT_FLOAT_LE,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  am->channels,
                                  am->sample_rate,
                                  1,
                                  500000)))
    {
        printf("Playback setup error: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(am->pcm_buffer);
        drmp3_uninit(mp3);
        return NULL;
    }

    bool playing = true;
    while (playing)
    {
        if(am->is_stopped)
        {
            playing = false;
            //LOG_INFO("Audio stream is stopped");
            break;

        }

        if (am->is_paused)
        {
           // LOG_INFO("Audio stream is paused");
            continue;
        }

     
        drmp3_uint64 framesRead = drmp3_read_pcm_frames_f32(am->mp3, am->buffer_size * am->channels / sizeof(float), am->pcm_buffer);

        if (framesRead == 0)
        {
            playing = false;
            continue;
        }

        for (drmp3_uint64 i = 0; i < framesRead * am->channels; i++)
        {
            am->pcm_buffer[i] *= am->volume;
        }

        snd_pcm_sframes_t frames = snd_pcm_writei(handle, am->pcm_buffer, framesRead);
        if (frames < 0)
        {
            frames = snd_pcm_recover(handle, frames, 0);
        }
        if (frames < 0)
        {
            printf("snd_pcm_writei failed: %s\n", snd_strerror(frames));
            err = frames;
            playing = false;
        }
        else if (frames > 0 && frames < (long)framesRead)
        {
            printf("Short write (expected %li, wrote %li)\n", (long)framesRead, frames);
        }
    }

    err = snd_pcm_drain(handle);
    if (err < 0)
    {
        printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
    }

    snd_pcm_close(handle);
    drmp3_uninit(mp3);
    free(am->pcm_buffer);

    return NULL;
}

int glps_audio_stream_play(glps_audio_stream *am, const char *audio_file_path,
                           int sample_rate, int channels, int bits_per_sample,
                           int buffer_size)
{
    am->audio_file_path = audio_file_path;
    am->sample_rate = sample_rate > 0 ? sample_rate : DEFAULT_SAMPLE_RATE;
    am->channels = channels > 0 ? channels : DEFAULT_CHANNELS;
    am->bits_per_sample = bits_per_sample > 0 ? bits_per_sample : DEFAULT_BITS_PER_SAMPLE;
    am->buffer_size = buffer_size > 0 ? buffer_size : DEFAULT_BUFFER_SIZE;
    am->thread = malloc(sizeof(gthread_t));
    glps_thread_create(am->thread, NULL, __audio_thread_func, am);
    glps_thread_detach(*(gthread_t *)am->thread);
    return -1;
}

void glps_audio_stream_stop(glps_audio_stream *am)
{
    glps_thread_mutex_lock(am->state_mutex);
    am->is_stopped = 1;
    glps_thread_mutex_unlock(am->state_mutex);
}

void glps_audio_stream_pause(glps_audio_stream *am)
{
    glps_thread_mutex_lock(am->state_mutex);
    am->is_paused = 1;
    glps_thread_mutex_unlock(am->state_mutex);
}

void glps_audio_stream_resume(glps_audio_stream *am)
{
    glps_thread_mutex_lock(am->state_mutex);
    am->is_paused = 0;
    glps_thread_mutex_unlock(am->state_mutex);
}

void glps_audio_stream_set_volume(glps_audio_stream *am, float volume)
{
    glps_thread_mutex_lock(am->volume_mutex);
    if (am && volume >= 0.0f && volume <= 1.0f)
    {
        am->volume = volume;
    }
    glps_thread_mutex_unlock(am->volume_mutex);
}

void glps_audio_stream_destroy(glps_audio_stream *am)
{
    if (am)
    {
        if (am->pcm_buffer)
        {
            free(am->pcm_buffer);
        }

        if (am->mp3)
        {
            drmp3_uninit(am->mp3);
            free(am->mp3);
        }

        if (am->pcm_handle)
        {
            snd_pcm_close(am->pcm_handle);
        }

        if (am->state_mutex)
        {
            glps_thread_mutex_destroy(am->state_mutex);
            free(am->state_mutex);
        }


        if (am->volume_mutex)
        {
            glps_thread_mutex_destroy(am->volume_mutex);
            free(am->volume_mutex);
        }

        if (am->thread)
        {
            free(am->thread);
        }
        free(am);
        
    }
}