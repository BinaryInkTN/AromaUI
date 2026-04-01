#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <vosk_api.h>
#include "cJSON.h"
#include "voice_control.h"

extern void dial_call_callback(void* node, void *user_data);
extern void dial_end_callback(void* node, void *user_data);
extern void navigate_to_tab(int index);
extern void set_voice_status(const char* status);

static void process_intent(const char *text) {
    if (strstr(text, "hey aroma") || strstr(text, "aroma")) {
        if (strstr(text, "music")) {
            printf("Voice Intent: OPEN MUSIC\n");
            navigate_to_tab(1);
            set_voice_status("Opened Music");
        } else if (strstr(text, "phone") || strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: OPEN PHONE\n");
            navigate_to_tab(2);
            set_voice_status("Opened Phone");
        } else if (strstr(text, "settings")) {
            printf("Voice Intent: OPEN SETTINGS\n");
            navigate_to_tab(3);
            set_voice_status("Opened Settings");
        } else if (strstr(text, "main") || strstr(text, "home")) {
            printf("Voice Intent: OPEN MAIN\n");
            navigate_to_tab(0);
            set_voice_status("Opened Home");
        } else if (strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: CALL\n");
            dial_call_callback(NULL, NULL);
        } else if (strstr(text, "end") || strstr(text, "hang up")) {
            printf("Voice Intent: END CALL\n");
            dial_end_callback(NULL, NULL);
        } else {
            set_voice_status("Command not recognized");
            printf("Voice Intent: UNKNOWN -> %s\n", text);
        }
    } else {
        // If not containing "aroma" or "hey aroma"
        if (strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: CALL\n");
            dial_call_callback(NULL, NULL);
        } else if (strstr(text, "end") || strstr(text, "hang up")) {
            printf("Voice Intent: END CALL\n");
            dial_end_callback(NULL, NULL);
        } else {
            // Optional: print unhandled partial speech
            // printf("Background chatter: %s\n", text);
            set_voice_status("");
        }
    }
}

static void *voice_thread_func(void *arg) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int sample_rate = 16000;
    int dir;
    int rc;

    VoskModel *model = vosk_model_new("../model");
    if (!model) {
        fprintf(stderr, "Failed to load Vosk model\n");
        return NULL;
    }
    VoskRecognizer *recognizer = vosk_recognizer_new(model, sample_rate);

    printf("Opening ALSA capture device...\n");
    rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "Unable to open PCM device: %s\n", snd_strerror(rc));
        return NULL;
    }
    
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 1);
    snd_pcm_hw_params_set_rate_near(handle, params, &sample_rate, &dir);
    snd_pcm_hw_params_set_period_size(handle, params, 1024, dir);
    
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        fprintf(stderr, "Unable to set HW parameters: %s\n", snd_strerror(rc));
        return NULL;
    }

    printf("ALSA capture device ready, listening for commands...\n");

    short buffer[4096];
    int frames_read = 0;
    while (1) {
        rc = snd_pcm_readi(handle, buffer, 1024);
        if (rc == -EPIPE) {
            fprintf(stderr, "ALSA Overrun occurred\n");
            snd_pcm_prepare(handle);
            continue;
        } else if (rc < 0) {
            fprintf(stderr, "ALSA Read error: %s\n", snd_strerror(rc));
            continue;
        }

        frames_read++;
        
        long long sum = 0;
        for (int i = 0; i < rc; i++) {
            sum += abs(buffer[i]);
        }
        long average_level = sum / rc;

        if (frames_read % 100 == 0) {
            printf("ALSA: Read 100 frames (~6.4 seconds of audio), Average volume level: %ld\n", average_level);
        }

        if (vosk_recognizer_accept_waveform(recognizer, (const char *)buffer, rc * 2)) {
            const char *result = vosk_recognizer_result(recognizer);
            cJSON *json = cJSON_Parse(result);
            if (json) {
                cJSON *text = cJSON_GetObjectItem(json, "text");
                if (text && text->valuestring && strlen(text->valuestring) > 0) {
                    process_intent(text->valuestring);
                }
                cJSON_Delete(json);
            }
        } else {
            // Partial results can be printed for debugging
            const char *partial_res = vosk_recognizer_partial_result(recognizer);
            cJSON *json = cJSON_Parse(partial_res);
            if (json) {
                cJSON *partial = cJSON_GetObjectItem(json, "partial");
                if (partial && partial->valuestring && strlen(partial->valuestring) > 0) {
                    printf("Partial heard: %s\n", partial->valuestring);
                }
                cJSON_Delete(json);
            }
        }
    }

    vosk_recognizer_free(recognizer);
    vosk_model_free(model);
    snd_pcm_close(handle);
    return NULL;
}

void start_voice_control_thread(void) {
    pthread_t thread;
    pthread_create(&thread, NULL, voice_thread_func, NULL);
    pthread_detach(thread);
}
