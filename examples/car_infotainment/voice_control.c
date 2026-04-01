#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <vosk_api.h>
#include "cJSON.h"
#include "voice_control.h"
#include <stdbool.h>

extern void queue_voice_action(int tab_index, bool call, bool end_call, const char* status);
extern void queue_voice_partial(const char* partial_text);
extern void queue_voice_theme(int dark_mode);

static bool manual_wake_active = false;

static void speak(const char *message) {
    if (!message || strlen(message) == 0) return;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pico2wave -w /tmp/aroma_tts.wav \"%s\" && aplay -q /tmp/aroma_tts.wav &", message);
    int ret = system(cmd);
    (void)ret;
}

void trigger_manual_wake(void) {
    manual_wake_active = true;
}

static void process_intent(const char *text) {
    bool is_awake = manual_wake_active || strstr(text, "hey aroma") || strstr(text, "aroma");

    if (is_awake) {
        manual_wake_active = false;
        
        if (strstr(text, "music")) {
            printf("Voice Intent: OPEN MUSIC\n");
            speak("Opening Music");
            queue_voice_action(1, false, false, "Opened Music");
        } else if (strstr(text, "light mode") || strstr(text, "light theme")) {
            printf("Voice Intent: LIGHT MODE\n");
            speak("Switching to light mode");
            queue_voice_theme(0);
            queue_voice_action(-1, false, false, "Light Mode Set");
        } else if (strstr(text, "dark mode") || strstr(text, "dark theme")) {
            printf("Voice Intent: DARK MODE\n");
            speak("Switching to dark mode");
            queue_voice_theme(1);
            queue_voice_action(-1, false, false, "Dark Mode Set");
        } else if (strstr(text, "phone") || strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: OPEN PHONE\n");
            speak("Opening Phone");
            queue_voice_action(2, false, false, "Opened Phone");
        } else if (strstr(text, "settings")) {
            printf("Voice Intent: OPEN SETTINGS\n");
            speak("Opening Settings");
            queue_voice_action(3, false, false, "Opened Settings");
        } else if (strstr(text, "main") || strstr(text, "home")) {
            printf("Voice Intent: OPEN MAIN\n");
            speak("Opening Home screen");
            queue_voice_action(0, false, false, "Opened Home");
        } else if (strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: CALL\n");
            speak("Starting call");
            queue_voice_action(-1, true, false, "");
        } else if (strstr(text, "end") || strstr(text, "hang up")) {
            printf("Voice Intent: END CALL\n");
            speak("Ending call");
            queue_voice_action(-1, false, true, "");
        } else {
            printf("Voice Intent: UNKNOWN -> %s\n", text);
            speak("Sorry, I didn't catch that.");
            queue_voice_action(-1, false, false, "Unknown Command");
        }
    } else {
        if (strstr(text, "call") || strstr(text, "dial")) {
            printf("Voice Intent: CALL\n");
            speak("Starting call");
            queue_voice_action(-1, true, false, "");
        } else if (strstr(text, "end") || strstr(text, "hang up")) {
            printf("Voice Intent: END CALL\n");
            speak("Ending call");
            queue_voice_action(-1, false, true, "");
        } else {
            queue_voice_action(-1, false, false, ""); // Clear status
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
                    queue_voice_partial(text->valuestring);
                    process_intent(text->valuestring);
                } else {
                    queue_voice_partial(""); 
                }
                cJSON_Delete(json);
            }
        } else {
            const char *partial_res = vosk_recognizer_partial_result(recognizer);
            cJSON *json = cJSON_Parse(partial_res);
            if (json) {
                cJSON *partial = cJSON_GetObjectItem(json, "partial");
                if (partial && partial->valuestring && strlen(partial->valuestring) > 0) {
                    queue_voice_partial(partial->valuestring);
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
