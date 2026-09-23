/* Media Bluetooth service implementation: owns the A2DP/AVRCP speaker
 * stack (bt_speaker_api.c, same .so) and exposes it to the host through
 * MediaBtService. No callbacks are registered anywhere: every consumer
 * polls the thread-safe getters (the host mini card has its monitor
 * thread, the Music UI refreshes on its update tick).
 *
 * Lifecycle mirrors the old host toggle exactly: enable = init (once)
 * + start, disable = stop + cleanup. Destroy stops the stack so a live
 * update can dlclose this .so without stranding threads.
 */

#include "media_bt_service.h"

#include <stdio.h>
#include <string.h>

static bool s_media_enabled = false;

static void on_svc_log(const char *level, const char *message, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[BT %s] %s\n", level ? level : "INFO",
            message ? message : "");
}

static int media_set_enabled(bool on, const char *device_name)
{
    if (on)
    {
        if (!s_media_enabled)
        {
            bt_config_t config = {
                .device_name = (device_name && device_name[0])
                                   ? device_name
                                   : "Aroma Infotainment",
                .pin_code = "0000",
                .verbose = true,
                .state_cb = NULL,
                .state_cb_data = NULL,
                .device_cb = NULL,
                .device_cb_data = NULL,
                .error_cb = NULL,
                .error_cb_data = NULL,
                .audio_cb = NULL,
                .audio_cb_data = NULL,
                .log_cb = on_svc_log,
                .log_cb_data = NULL,
                .avrcp_cb = NULL,
                .avrcp_cb_data = NULL,
            };
            if (bt_speaker_init(&config) != 0)
            {
                fprintf(stderr, "[BT] init failed: %s\n",
                        bt_speaker_get_last_error_message());
                return -1;
            }
            if (bt_speaker_get_state() != BT_STATE_ADVERTISING)
            {
                fprintf(stderr,
                        "[BT] warning: state is '%s' after init, not "
                        "advertising - pairing attempts may be rejected "
                        "with no visible error until the agent registers.\n",
                        bt_speaker_get_state_string());
            }
            s_media_enabled = true;
        }
        return bt_speaker_start();
    }
    bt_speaker_stop();
    bt_speaker_cleanup();
    s_media_enabled = false;
    return 0;
}

static bool media_is_enabled(void)
{
    return s_media_enabled && bt_speaker_is_running();
}

static int media_set_device_name(const char *name)
{
    if (!name || !name[0])
        return -1;
    return bt_speaker_set_device_name(name);
}

static bt_state_t media_state(void)
{
    return bt_speaker_get_state();
}

static bool media_is_connected(void)
{
    bt_state_t s = bt_speaker_get_state();
    return s == BT_STATE_CONNECTED || s == BT_STATE_PLAYING;
}

static void media_device_info(bt_device_info_t *out)
{
    if (!out)
        return;
    *out = bt_speaker_get_device_info();
}

static void media_media_info(bt_media_info_t *out)
{
    if (!out)
        return;
    *out = bt_speaker_get_media_info();
}

static void media_stats(bt_stats_t *out)
{
    if (!out)
        return;
    *out = bt_speaker_get_stats();
}

static const MediaBtService s_media_bt_service = {
    .set_enabled = media_set_enabled,
    .is_enabled = media_is_enabled,
    .set_device_name = media_set_device_name,
    .state = media_state,
    .is_connected = media_is_connected,
    .device_info = media_device_info,
    .media_info = media_media_info,
    .stats = media_stats,
    .avrcp_play = bt_speaker_avrcp_play,
    .avrcp_pause = bt_speaker_avrcp_pause,
    .avrcp_next = bt_speaker_avrcp_next,
    .avrcp_prev = bt_speaker_avrcp_previous,
};

const MediaBtService *media_bt_service(void)
{
    return &s_media_bt_service;
}
