#ifndef BT_SPEAKER_API_H
#define BT_SPEAKER_API_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    BT_STATE_IDLE = 0,
    BT_STATE_INITIALIZING,
    BT_STATE_ADVERTISING,
    BT_STATE_PAIRED,
    BT_STATE_CONNECTED,
    BT_STATE_PLAYING,
    BT_STATE_ERROR
} bt_state_t;

typedef enum {
    BT_ERROR_NONE = 0,
    BT_ERROR_DBUS,
    BT_ERROR_ADAPTER_NOT_FOUND,
    BT_ERROR_CONFIG_FAILED,
    BT_ERROR_ENDPOINT_FAILED,
    BT_ERROR_AGENT_FAILED,
    BT_ERROR_ALREADY_RUNNING,
    BT_ERROR_NOT_INITIALIZED
} bt_error_t;

typedef struct {
    char name[128];
    char path[256];
    char address[32];
    bool connected;
} bt_device_info_t;

typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char genre[64];
    char status[32];
    uint32_t duration;
    uint32_t position;
    uint32_t track_number;
    uint32_t total_tracks;
} bt_media_info_t;

typedef struct {
    long connected_time_sec;
    long audio_time_sec;
    bool audio_active;
} bt_stats_t;

typedef void (*bt_state_callback_t)(bt_state_t old_state, bt_state_t new_state, void *user_data);
typedef void (*bt_device_callback_t)(const bt_device_info_t *device, bool connected, void *user_data);
typedef void (*bt_error_callback_t)(bt_error_t error, const char *message, void *user_data);
typedef void (*bt_audio_callback_t)(bool started, void *user_data);
typedef void (*bt_log_callback_t)(const char *level, const char *message, void *user_data);
typedef void (*bt_avrcp_callback_t)(const bt_media_info_t *media, void *user_data);

typedef struct {
    const char *device_name;
    const char *pin_code;
    bool verbose;
    bt_state_callback_t state_cb;
    void *state_cb_data;
    bt_device_callback_t device_cb;
    void *device_cb_data;
    bt_error_callback_t error_cb;
    void *error_cb_data;
    bt_audio_callback_t audio_cb;
    void *audio_cb_data;
    bt_log_callback_t log_cb;
    void *log_cb_data;
    bt_avrcp_callback_t avrcp_cb;
    void *avrcp_cb_data;
} bt_config_t;

int bt_speaker_init(const bt_config_t *config);
int bt_speaker_start(void);
int bt_speaker_stop(void);
void bt_speaker_cleanup(void);

bt_state_t bt_speaker_get_state(void);
const char *bt_speaker_get_state_string(void);
bt_device_info_t bt_speaker_get_device_info(void);
bt_media_info_t bt_speaker_get_media_info(void);
bt_stats_t bt_speaker_get_stats(void);
bt_error_t bt_speaker_get_last_error(void);
const char *bt_speaker_get_last_error_message(void);
bool bt_speaker_is_running(void);

int bt_speaker_set_discoverable(bool d);
int bt_speaker_set_pairable(bool p);
int bt_speaker_set_device_name(const char *name);

int bt_speaker_avrcp_play(void);
int bt_speaker_avrcp_pause(void);
int bt_speaker_avrcp_next(void);
int bt_speaker_avrcp_previous(void);
int bt_speaker_avrcp_volume_up(void);
int bt_speaker_avrcp_volume_down(void);

void bt_speaker_set_state_callback(bt_state_callback_t cb, void *ud);
void bt_speaker_set_device_callback(bt_device_callback_t cb, void *ud);
void bt_speaker_set_error_callback(bt_error_callback_t cb, void *ud);
void bt_speaker_set_audio_callback(bt_audio_callback_t cb, void *ud);
void bt_speaker_set_log_callback(bt_log_callback_t cb, void *ud);
void bt_speaker_set_avrcp_callback(bt_avrcp_callback_t cb, void *ud);

#endif