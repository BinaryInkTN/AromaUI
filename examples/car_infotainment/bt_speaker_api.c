#include "bt_speaker_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>

#include <dbus/dbus.h>
#include <pulse/pulseaudio.h>

void safe_strncpy(char *dest, const char *src, size_t n)
{
    if (!dest || n == 0) return;
    if (!src) { dest[0] = '\0'; return; }
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
}

#define APP_NAME "bt_speaker"

#define BLUEZ_BUS_NAME              "org.bluez"
#define BLUEZ_ADAPTER_IFACE         "org.bluez.Adapter1"
#define BLUEZ_DEVICE_IFACE          "org.bluez.Device1"
#define BLUEZ_MEDIA_IFACE           "org.bluez.Media1"
#define BLUEZ_MEDIA_EP_IFACE        "org.bluez.MediaEndpoint1"
#define BLUEZ_AGENT_IFACE           "org.bluez.Agent1"
#define BLUEZ_AGENT_MGR_IFACE       "org.bluez.AgentManager1"
#define BLUEZ_PROFILE_MGR_IFACE     "org.bluez.ProfileManager1"
#define DBUS_PROPS_IFACE            "org.freedesktop.DBus.Properties"
#define DBUS_OBJMGR_IFACE           "org.freedesktop.DBus.ObjectManager"
#define BLUEZ_MEDIA_TRANSPORT_IFACE "org.bluez.MediaTransport1"
#define BLUEZ_MEDIA_PLAYER_IFACE    "org.bluez.MediaPlayer1"
#define BLUEZ_MEDIA_CONTROL_IFACE   "org.bluez.MediaControl1"

#define AGENT_PATH    "/com/btspeaker/agent"
#define ENDPOINT_PATH "/com/btspeaker/endpoint/a2dp_sink"
#define PROFILE_PATH  "/com/btspeaker/profile/a2dp_sink"
#define PLAYER_PATH   "/com/btspeaker/player"

#define A2DP_CODEC_SBC    0x00
#define A2DP_SINK_UUID    "0000110B-0000-1000-8000-00805F9B34FB"
#define AVRCP_TARGET_UUID "0000110C-0000-1000-8000-00805F9B34FB"
#define AVRCP_CONTROLLER_UUID "0000110E-0000-1000-8000-00805F9B34FB"

#define BT_CLASS_AUDIO_SPEAKER 0x240404u

typedef struct
{
    uint8_t frequency;
    uint8_t channel_mode;
    uint8_t block_length;
    uint8_t subbands;
    uint8_t allocation_method;
    uint8_t min_bitpool;
    uint8_t max_bitpool;
} __attribute__((packed)) a2dp_sbc_t;

#define SBC_FREQ_44100   0x20
#define SBC_FREQ_48000   0x10
#define SBC_FREQ_32000   0x40
#define SBC_FREQ_16000   0x80
#define SBC_CHAN_MONO    0x08
#define SBC_CHAN_DUAL    0x04
#define SBC_CHAN_STEREO  0x02
#define SBC_CHAN_JOINT   0x01
#define SBC_BLK_4        0x80
#define SBC_BLK_8        0x40
#define SBC_BLK_12       0x20
#define SBC_BLK_16       0x10
#define SBC_SUBBAND_4    0x08
#define SBC_SUBBAND_8    0x04
#define SBC_ALLOC_SNR       0x02
#define SBC_ALLOC_LOUDNESS  0x01

static const a2dp_sbc_t sbc_capabilities = {
    .frequency        = SBC_FREQ_44100 | SBC_FREQ_48000 | SBC_FREQ_32000 | SBC_FREQ_16000,
    .channel_mode     = SBC_CHAN_MONO | SBC_CHAN_DUAL | SBC_CHAN_STEREO | SBC_CHAN_JOINT,
    .block_length     = SBC_BLK_4 | SBC_BLK_8 | SBC_BLK_12 | SBC_BLK_16,
    .subbands         = SBC_SUBBAND_4 | SBC_SUBBAND_8,
    .allocation_method = SBC_ALLOC_SNR | SBC_ALLOC_LOUDNESS,
    .min_bitpool      = 2,
    .max_bitpool      = 64,
};

typedef struct
{
    char device_name[64];
    char pin_code[16];
    bool verbose;

    DBusConnection *bus;
    char adapter_path[128];
    char device_path[128];

    pa_mainloop     *pa_ml;
    pa_mainloop_api *pa_api;
    pa_context      *pa_ctx;
    bool             pa_ready;
    bool             pa_bluez_module_loaded;

    bt_state_t state;
    char connected_device_path[128];
    char connected_device_name[64];
    char connected_device_address[32];

    pthread_mutex_t  lock;
    volatile bool    running;

    char transport_path[256];
    int  transport_fd;

    bool   agent_registered;
    time_t connected_time;
    time_t audio_start_time;

    bt_error_t last_error;
    char       last_error_msg[256];

    bt_state_callback_t  state_cb;
    void                *state_cb_data;
    bt_device_callback_t device_cb;
    void                *device_cb_data;
    bt_error_callback_t  error_cb;
    void                *error_cb_data;
    bt_audio_callback_t  audio_cb;
    void                *audio_cb_data;
    bt_log_callback_t    log_cb;
    void                *log_cb_data;
    bt_avrcp_callback_t  avrcp_cb;
    void                *avrcp_cb_data;

    pthread_t main_thread;
    bool      initialized;

    bt_media_info_t current_media;
    char            player_path[256];
    bool            player_verified;

    volatile bool pending_player_find;
    volatile bool pending_avrcp_monitor;
} internal_app_t;

static internal_app_t g_app = {
    .device_name           = "Aroma Speaker",
    .pin_code              = "0000",
    .state                 = BT_STATE_IDLE,
    .transport_fd          = -1,
    .last_error            = BT_ERROR_NONE,
    .initialized           = false,
    .current_media         = {0},
    .player_verified       = false,
    .pending_player_find   = false,
    .pending_avrcp_monitor = false,
    .pa_bluez_module_loaded = false,
};

static void *main_loop_thread(void *arg);
static void set_state_locked(internal_app_t *app, bt_state_t s);
static void set_state(internal_app_t *app, bt_state_t s);
static void set_error(internal_app_t *app, bt_error_t e, const char *msg);
static void notify_device_event(internal_app_t *app, bool connected);
static void notify_audio_event(internal_app_t *app, bool started);
static void notify_avrcp_event(internal_app_t *app, const bt_media_info_t *media);
static void log_msg(internal_app_t *app, const char *level, const char *fmt, ...);
static bool find_adapter(internal_app_t *app);
static bool configure_adapter(internal_app_t *app);
static bool register_endpoint(internal_app_t *app);
static bool register_agent(internal_app_t *app);
static bool register_a2dp_profile(internal_app_t *app);
static bool register_avrcp_profile(internal_app_t *app);
static void unregister_agent(internal_app_t *app);
static void handle_signal(internal_app_t *app, DBusMessage *msg);
static void cleanup(internal_app_t *app);
static void parse_avrcp_metadata(DBusMessageIter *iter, bt_media_info_t *media);
static void send_avrcp_command(const char *command);
static void check_transport_state(void);
static void query_device_name(void);
static void find_player_path(internal_app_t *app);
static void monitor_avrcp_changes(internal_app_t *app);
static void check_reconnection(internal_app_t *app);
static bool verify_player_functional(internal_app_t *app);
static void dump_all_objects(internal_app_t *app);

static void log_msg(internal_app_t *app, const char *level, const char *fmt, ...)
{
    if (!app->log_cb && !app->verbose) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (app->log_cb) {
        app->log_cb(level, buf, app->log_cb_data);
    } else {
        time_t t = time(NULL);
        struct tm *ti = localtime(&t);
        char tb[20];
        strftime(tb, sizeof(tb), "%H:%M:%S", ti);
        printf("[%s] [%s] %s\n", tb, level, buf);
        fflush(stdout);
    }
}

typedef struct {
    internal_app_t *app;
    bool found;
} bluez_check_t;

static void pa_module_check_cb(pa_context *c, const pa_module_info *info,
                                int eol, void *ud)
{
    (void)c;
    bluez_check_t *chk = (bluez_check_t *)ud;
    if (eol) return;
    if (info && strstr(info->name, "module-bluez5-discover"))
        chk->found = true;
}

static void pa_state_cb(pa_context *ctx, void *ud)
{
    internal_app_t *app = (internal_app_t *)ud;
    switch (pa_context_get_state(ctx))
    {
    case PA_CONTEXT_READY:
        app->pa_ready = true;
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        app->pa_ready = false;
        break;
    default:
        break;
    }
}

static void load_pa_bt_modules(internal_app_t *app)
{
    if (!app->pa_ready) return;

    if (!app->pa_bluez_module_loaded)
    {
        bluez_check_t chk = {.app = app, .found = false};
        pa_operation *op = pa_context_get_module_info_list(
            app->pa_ctx, pa_module_check_cb, &chk);
        if (op) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                pa_mainloop_iterate(app->pa_ml, 1, NULL);
            pa_operation_unref(op);
        }

        if (chk.found) {
            log_msg(app, "INFO", "module-bluez5-discover already loaded by PA");
            app->pa_bluez_module_loaded = true;
        } else {
            op = pa_context_load_module(app->pa_ctx,
                                        "module-bluez5-discover",
                                        NULL, NULL, NULL);
            if (op) {
                pa_operation_unref(op);
                app->pa_bluez_module_loaded = true;
                log_msg(app, "INFO", "Loaded module-bluez5-discover");
            } else {
                log_msg(app, "ERROR",
                        "Failed to load module-bluez5-discover. "
                        "Is pulseaudio-module-bluetooth installed?");
            }
        }
    }
}

static bool init_pulseaudio(internal_app_t *app)
{
    app->pa_ml = pa_mainloop_new();
    if (!app->pa_ml) return false;

    app->pa_api = pa_mainloop_get_api(app->pa_ml);
    app->pa_ctx = pa_context_new(app->pa_api, APP_NAME);
    if (!app->pa_ctx) return false;

    pa_context_set_state_callback(app->pa_ctx, pa_state_cb, app);

    if (pa_context_connect(app->pa_ctx, NULL, PA_CONTEXT_NOFAIL, NULL) < 0) {
        log_msg(app, "ERROR", "pa_context_connect failed: %s",
                pa_strerror(pa_context_errno(app->pa_ctx)));
        return false;
    }

    int retries = 0;
    while (!app->pa_ready && retries++ < 50) {
        pa_mainloop_iterate(app->pa_ml, 0, NULL);
        usleep(20000);
    }

    if (!app->pa_ready)
        log_msg(app, "WARN", "PulseAudio not READY after 1s");

    return app->pa_ready;
}

static bool get_prop_string(DBusConnection *bus, const char *dest,
                            const char *path, const char *iface,
                            const char *prop, char *out, size_t out_len)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Get");
    if (!msg) return false;
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop,
                             DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return false;
    }
    DBusMessageIter iter, var;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&iter, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
            const char *val = NULL;
            dbus_message_iter_get_basic(&var, &val);
            if (val) safe_strncpy(out, val, out_len);
        }
    }
    dbus_message_unref(reply);
    return true;
}

static bool get_prop_bool(DBusConnection *bus, const char *dest,
                          const char *path, const char *iface,
                          const char *prop, dbus_bool_t *out)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Get");
    if (!msg) return false;
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop,
                             DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return false;
    }
    DBusMessageIter iter, var;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&iter, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN)
            dbus_message_iter_get_basic(&var, out);
    }
    dbus_message_unref(reply);
    return true;
}

static bool set_prop_bool(DBusConnection *bus, const char *dest,
                          const char *path, const char *iface,
                          const char *prop, dbus_bool_t val)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Set");
    if (!msg) return false;
    DBusMessageIter it, var;
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&it, &var);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); return false; }
    if (r) dbus_message_unref(r);
    return true;
}

static bool set_prop_str(DBusConnection *bus, const char *dest,
                         const char *path, const char *iface,
                         const char *prop, const char *val)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Set");
    if (!msg) return false;
    DBusMessageIter it, var;
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&it, &var);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); return false; }
    if (r) dbus_message_unref(r);
    return true;
}

static bool set_prop_uint32(DBusConnection *bus, const char *dest,
                            const char *path, const char *iface,
                            const char *prop, dbus_uint32_t val)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Set");
    if (!msg) return false;
    DBusMessageIter it, var;
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &val);
    dbus_message_iter_close_container(&it, &var);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); return false; }
    if (r) dbus_message_unref(r);
    return true;
}

static void set_state_locked(internal_app_t *app, bt_state_t new_state)
{
    bt_state_t old = app->state;
    app->state = new_state;
    pthread_mutex_unlock(&app->lock);
    if (app->state_cb && old != new_state)
        app->state_cb(old, new_state, app->state_cb_data);
    pthread_mutex_lock(&app->lock);
}

static void set_state(internal_app_t *app, bt_state_t new_state)
{
    pthread_mutex_lock(&app->lock);
    set_state_locked(app, new_state);
    pthread_mutex_unlock(&app->lock);
}

static void set_error(internal_app_t *app, bt_error_t e, const char *msg)
{
    app->last_error = e;
    safe_strncpy(app->last_error_msg, msg, sizeof(app->last_error_msg));
    if (app->error_cb) app->error_cb(e, msg, app->error_cb_data);
}

static void notify_device_event(internal_app_t *app, bool connected)
{
    if (!app->device_cb) return;
    bt_device_info_t info;
    pthread_mutex_lock(&app->lock);
    safe_strncpy(info.name,    app->connected_device_name,    sizeof(info.name));
    safe_strncpy(info.path,    app->connected_device_path,    sizeof(info.path));
    safe_strncpy(info.address, app->connected_device_address, sizeof(info.address));
    info.connected = connected;
    pthread_mutex_unlock(&app->lock);
    app->device_cb(&info, connected, app->device_cb_data);
}

static void notify_audio_event(internal_app_t *app, bool started)
{
    if (app->audio_cb) app->audio_cb(started, app->audio_cb_data);
}

static void notify_avrcp_event(internal_app_t *app, const bt_media_info_t *media)
{
    if (app->avrcp_cb) app->avrcp_cb(media, app->avrcp_cb_data);
}

static bool find_adapter(internal_app_t *app)
{
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/",
                                                    DBUS_OBJMGR_IFACE,
                                                    "GetManagedObjects");
    if (!msg) return false;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) { dbus_error_free(&err); return false; }

    DBusMessageIter iter, dict;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return false;
    }
    dbus_message_iter_recurse(&iter, &dict);
    bool found = false;
    while (!found && dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, ifaces;
        dbus_message_iter_recurse(&dict, &entry);
        const char *path = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&entry, &path);
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_ARRAY) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_recurse(&entry, &ifaces);
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter ie;
            dbus_message_iter_recurse(&ifaces, &ie);
            const char *iface = NULL;
            if (dbus_message_iter_get_arg_type(&ie) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&ie, &iface);
            if (iface && strcmp(iface, BLUEZ_ADAPTER_IFACE) == 0) {
                safe_strncpy(app->adapter_path, path, sizeof(app->adapter_path));
                found = true;
                break;
            }
            dbus_message_iter_next(&ifaces);
        }
        dbus_message_iter_next(&dict);
    }
    dbus_message_unref(reply);
    return found;
}

static bool configure_adapter(internal_app_t *app)
{
    log_msg(app, "INFO", "Configuring adapter: %s", app->adapter_path);
    set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                  BLUEZ_ADAPTER_IFACE, "Powered", TRUE);
    sleep(2);
    set_prop_str(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                 BLUEZ_ADAPTER_IFACE, "Alias", app->device_name);
    set_prop_uint32(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                    BLUEZ_ADAPTER_IFACE, "Class", (dbus_uint32_t)BT_CLASS_AUDIO_SPEAKER);
    set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                  BLUEZ_ADAPTER_IFACE, "Pairable", TRUE);
    set_prop_uint32(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                    BLUEZ_ADAPTER_IFACE, "PairableTimeout", 0);
    set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                  BLUEZ_ADAPTER_IFACE, "Discoverable", TRUE);
    set_prop_uint32(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                    BLUEZ_ADAPTER_IFACE, "DiscoverableTimeout", 0);
    dbus_connection_flush(app->bus);
    log_msg(app, "INFO", "Adapter ready – look for '%s' on your phone (PIN: %s)",
            app->device_name, app->pin_code);
    return true;
}

static void endpoint_append_props(DBusMessage *reply)
{
    DBusMessageIter iter, dict, entry, var, arr;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING
                                     DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &dict);

    const char *k = "UUID", *uuid = A2DP_SINK_UUID;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &uuid);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);

    k = "Codec";
    uint8_t codec = A2DP_CODEC_SBC;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BYTE, &codec);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);

    k = "Capabilities";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                     DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &arr);
    const uint8_t *cap = (const uint8_t *)&sbc_capabilities;
    for (size_t i = 0; i < sizeof(sbc_capabilities); i++)
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &cap[i]);
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);

    dbus_message_iter_close_container(&iter, &dict);
}

static DBusHandlerResult endpoint_handler(DBusConnection *conn,
                                          DBusMessage *msg, void *data)
{
    internal_app_t *app = (internal_app_t *)data;

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_EP_IFACE, "Release")) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_EP_IFACE, "GetProperties") ||
        dbus_message_is_method_call(msg, DBUS_PROPS_IFACE, "GetAll")) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        endpoint_append_props(r);
        dbus_connection_send(conn, r, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_EP_IFACE, "SetConfiguration")) {
        const char *transport = NULL;
        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&iter, &transport);
        if (transport) {
            pthread_mutex_lock(&app->lock);
            safe_strncpy(app->transport_path, transport, sizeof(app->transport_path));
            app->audio_start_time = time(NULL);
            app->pending_player_find = true;
            app->pending_avrcp_monitor = true;
            set_state_locked(app, BT_STATE_PLAYING);
            pthread_mutex_unlock(&app->lock);
            notify_audio_event(app, true);
        }
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_EP_IFACE, "SelectConfiguration")) {
        a2dp_sbc_t cfg = {0};
        cfg.frequency        = SBC_FREQ_44100;
        cfg.channel_mode     = SBC_CHAN_JOINT;
        cfg.block_length     = SBC_BLK_16;
        cfg.subbands         = SBC_SUBBAND_8;
        cfg.allocation_method = SBC_ALLOC_LOUDNESS;
        cfg.min_bitpool      = 2;
        cfg.max_bitpool      = 53;
        DBusMessage *r = dbus_message_new_method_return(msg);
        DBusMessageIter ri, ra;
        dbus_message_iter_init_append(r, &ri);
        dbus_message_iter_open_container(&ri, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &ra);
        const uint8_t *b = (const uint8_t *)&cfg;
        for (size_t i = 0; i < sizeof(cfg); i++)
            dbus_message_iter_append_basic(&ra, DBUS_TYPE_BYTE, &b[i]);
        dbus_message_iter_close_container(&ri, &ra);
        dbus_connection_send(conn, r, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_EP_IFACE, "ClearConfiguration")) {
        pthread_mutex_lock(&app->lock);
        app->transport_path[0]        = '\0';
        app->audio_start_time         = 0;
        app->player_path[0]           = '\0';
        app->player_verified          = false;
        app->pending_player_find      = false;
        app->pending_avrcp_monitor    = false;
        if (app->state == BT_STATE_PLAYING)
            set_state_locked(app, BT_STATE_CONNECTED);
        pthread_mutex_unlock(&app->lock);
        notify_audio_event(app, false);
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_connection_flush(conn);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable endpoint_vtable = {
    .message_function = endpoint_handler
};

static bool register_endpoint(internal_app_t *app)
{
    if (!dbus_connection_register_object_path(app->bus, ENDPOINT_PATH,
                                              &endpoint_vtable, app))
        return false;

    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, app->adapter_path,
                                                    BLUEZ_MEDIA_IFACE, "RegisterEndpoint");
    if (!msg) {
        dbus_connection_unregister_object_path(app->bus, ENDPOINT_PATH);
        return false;
    }

    DBusMessageIter iter, props, entry, var, arr;
    dbus_message_iter_init_append(msg, &iter);
    const char *ep = ENDPOINT_PATH;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &ep);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING
                                     DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &props);

    const char *k = "UUID", *uuid = A2DP_SINK_UUID;
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &uuid);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    k = "Codec";
    uint8_t codec = A2DP_CODEC_SBC;
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BYTE, &codec);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);

    k = "Capabilities";
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                     DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING, &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &arr);
    const uint8_t *cap = (const uint8_t *)&sbc_capabilities;
    for (size_t i = 0; i < sizeof(sbc_capabilities); i++)
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &cap[i]);
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&props, &entry);
    dbus_message_iter_close_container(&iter, &props);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        log_msg(app, "ERROR", "RegisterEndpoint failed: %s", err.message);
        dbus_error_free(&err);
        dbus_connection_unregister_object_path(app->bus, ENDPOINT_PATH);
        return false;
    }
    if (reply) dbus_message_unref(reply);
    log_msg(app, "INFO", "A2DP endpoint registered");
    return true;
}

static bool register_a2dp_profile(internal_app_t *app)
{
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                                    BLUEZ_PROFILE_MGR_IFACE,
                                                    "RegisterProfile");
    if (!msg) return false;

    DBusMessageIter iter, opts, entry, var;
    dbus_message_iter_init_append(msg, &iter);
    const char *profile_path = PROFILE_PATH;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &profile_path);
    const char *uuid = A2DP_SINK_UUID;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &uuid);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING
                                     DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &opts);

    const char *k = "Name", *v_name = "A2DP Sink";
    dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_name);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&opts, &entry);

    k = "Role";
    const char *v_role = "sink";
    dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_role);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&opts, &entry);

    k = "Channel";
    dbus_uint16_t v_ch = 0;
    dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "q", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT16, &v_ch);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&opts, &entry);

    dbus_message_iter_close_container(&iter, &opts);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        log_msg(app, "WARN", "RegisterProfile A2DP: %s (continuing)", err.message);
        dbus_error_free(&err);
        return true;
    }
    if (reply) dbus_message_unref(reply);
    log_msg(app, "INFO", "A2DP Sink profile registered");
    return true;
}

static bool register_avrcp_profile(internal_app_t *app)
{
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                                    BLUEZ_PROFILE_MGR_IFACE,
                                                    "RegisterProfile");
    if (!msg) return false;

    DBusMessageIter iter, opts, entry, var;
    dbus_message_iter_init_append(msg, &iter);
    const char *profile_path = PLAYER_PATH;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &profile_path);
    const char *uuid = AVRCP_TARGET_UUID;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &uuid);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING
                                     DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &opts);

    const char *k = "Name", *v_name = "AVRCP Target";
    dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_name);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&opts, &entry);

    k = "Role";
    const char *v_role = "target";
    dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v_role);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&opts, &entry);

    dbus_message_iter_close_container(&iter, &opts);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        log_msg(app, "WARN", "RegisterAVRCP Target: %s (continuing)", err.message);
        dbus_error_free(&err);
        return true;
    }
    if (reply) dbus_message_unref(reply);

    msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                       BLUEZ_PROFILE_MGR_IFACE, "RegisterProfile");
    if (msg) {
        DBusMessageIter iter2, opts2, entry2, var2;
        dbus_message_iter_init_append(msg, &iter2);
        dbus_message_iter_append_basic(&iter2, DBUS_TYPE_OBJECT_PATH, &profile_path);
        const char *uuid_ct = AVRCP_CONTROLLER_UUID;
        dbus_message_iter_append_basic(&iter2, DBUS_TYPE_STRING, &uuid_ct);
        dbus_message_iter_open_container(&iter2, DBUS_TYPE_ARRAY,
                                         DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                         DBUS_TYPE_STRING_AS_STRING
                                         DBUS_TYPE_VARIANT_AS_STRING
                                         DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                         &opts2);

        k = "Name";
        const char *v_name_ct = "AVRCP Controller";
        dbus_message_iter_open_container(&opts2, DBUS_TYPE_DICT_ENTRY, NULL, &entry2);
        dbus_message_iter_append_basic(&entry2, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry2, DBUS_TYPE_VARIANT, "s", &var2);
        dbus_message_iter_append_basic(&var2, DBUS_TYPE_STRING, &v_name_ct);
        dbus_message_iter_close_container(&entry2, &var2);
        dbus_message_iter_close_container(&opts2, &entry2);

        k = "Role";
        const char *v_role_ct = "controller";
        dbus_message_iter_open_container(&opts2, DBUS_TYPE_DICT_ENTRY, NULL, &entry2);
        dbus_message_iter_append_basic(&entry2, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry2, DBUS_TYPE_VARIANT, "s", &var2);
        dbus_message_iter_append_basic(&var2, DBUS_TYPE_STRING, &v_role_ct);
        dbus_message_iter_close_container(&entry2, &var2);
        dbus_message_iter_close_container(&opts2, &entry2);

        dbus_message_iter_close_container(&iter2, &opts2);

        dbus_error_init(&err);
        reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 5000, &err);
        dbus_message_unref(msg);
        if (dbus_error_is_set(&err)) {
            log_msg(app, "WARN", "RegisterAVRCP Controller: %s", err.message);
            dbus_error_free(&err);
        }
        if (reply) dbus_message_unref(reply);
    }

    log_msg(app, "INFO", "AVRCP profiles registered");
    return true;
}

static bool verify_player_functional(internal_app_t *app)
{
    if (!app->bus || !app->player_path[0]) return false;

    char status[64] = {0};
    if (get_prop_string(app->bus, BLUEZ_BUS_NAME, app->player_path,
                        BLUEZ_MEDIA_PLAYER_IFACE, "Status",
                        status, sizeof(status)) && status[0]) {
        log_msg(app, "INFO", "Player verified: %s status=%s", app->player_path, status);
        pthread_mutex_lock(&app->lock);
        safe_strncpy(app->current_media.status, status, sizeof(app->current_media.status));
        pthread_mutex_unlock(&app->lock);
        notify_avrcp_event(app, &app->current_media);
        return true;
    }
    dbus_bool_t can_ctrl = FALSE;
    if (get_prop_bool(app->bus, BLUEZ_BUS_NAME, app->player_path,
                      BLUEZ_MEDIA_PLAYER_IFACE, "CanControl", &can_ctrl) && can_ctrl) {
        log_msg(app, "INFO", "Player verified via CanControl: %s", app->player_path);
        return true;
    }
    log_msg(app, "WARN", "Player at %s not responsive", app->player_path);
    pthread_mutex_lock(&app->lock);
    app->player_path[0]  = '\0';
    app->player_verified = false;
    pthread_mutex_unlock(&app->lock);
    return false;
}

static void find_player_path(internal_app_t *app)
{
    if (!app->bus || !app->connected_device_path[0]) return;

    char player_prop[256] = {0};
    if (get_prop_string(app->bus, BLUEZ_BUS_NAME, app->connected_device_path,
                        BLUEZ_MEDIA_CONTROL_IFACE, "Player",
                        player_prop, sizeof(player_prop)) &&
        player_prop[0] && strcmp(player_prop, "/") != 0) {
        pthread_mutex_lock(&app->lock);
        safe_strncpy(app->player_path, player_prop, sizeof(app->player_path));
        app->player_verified = false;
        pthread_mutex_unlock(&app->lock);
        log_msg(app, "INFO", "Player via MediaControl.Player: %s", player_prop);
        if (verify_player_functional(app)) {
            pthread_mutex_lock(&app->lock);
            app->player_verified = true;
            pthread_mutex_unlock(&app->lock);
            return;
        }
    }

    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/",
                                                    DBUS_OBJMGR_IFACE,
                                                    "GetManagedObjects");
    if (!msg) return;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return;
    }
    DBusMessageIter iter, dict;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return;
    }
    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, ifaces;
        dbus_message_iter_recurse(&dict, &entry);
        const char *obj_path = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&entry, &obj_path);
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_ARRAY ||
            !obj_path ||
            !strstr(obj_path, app->connected_device_path) ||
            !strstr(obj_path, "/player")) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_recurse(&entry, &ifaces);
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter ie;
            dbus_message_iter_recurse(&ifaces, &ie);
            const char *iface = NULL;
            if (dbus_message_iter_get_arg_type(&ie) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&ie, &iface);
            if (iface && strcmp(iface, BLUEZ_MEDIA_PLAYER_IFACE) == 0) {
                pthread_mutex_lock(&app->lock);
                safe_strncpy(app->player_path, obj_path, sizeof(app->player_path));
                app->player_verified = false;
                pthread_mutex_unlock(&app->lock);
                log_msg(app, "INFO", "Found player: %s", obj_path);
                if (verify_player_functional(app)) {
                    pthread_mutex_lock(&app->lock);
                    app->player_verified = true;
                    pthread_mutex_unlock(&app->lock);
                    dbus_message_unref(reply);
                    return;
                }
            }
            dbus_message_iter_next(&ifaces);
        }
        dbus_message_iter_next(&dict);
    }
    dbus_message_unref(reply);

    const char *patterns[] = {"/player0", "/player1", NULL};
    char base_path[256];
    for (int i = 0; patterns[i]; i++) {
        snprintf(base_path, sizeof(base_path), "%s%s",
                 app->connected_device_path, patterns[i]);
        char status[32] = {0};
        if (get_prop_string(app->bus, BLUEZ_BUS_NAME, base_path,
                            BLUEZ_MEDIA_PLAYER_IFACE, "Status",
                            status, sizeof(status)) && status[0]) {
            pthread_mutex_lock(&app->lock);
            safe_strncpy(app->player_path, base_path, sizeof(app->player_path));
            app->player_verified = true;
            pthread_mutex_unlock(&app->lock);
            log_msg(app, "INFO", "Player via pattern: %s", base_path);
            return;
        }
    }
    log_msg(app, "WARN", "No AVRCP player found for device: %s",
            app->connected_device_path);
}

static void parse_avrcp_metadata(DBusMessageIter *iter, bt_media_info_t *media)
{
    if (!iter || !media) return;
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) return;
    DBusMessageIter dict;
    dbus_message_iter_recurse(iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&entry, &key);
        if (!key || !dbus_message_iter_has_next(&entry)) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(&dict);
            continue;
        }
        DBusMessageIter var;
        dbus_message_iter_recurse(&entry, &var);
        int vt = dbus_message_iter_get_arg_type(&var);

        if (strcmp(key, "Title") == 0 && vt == DBUS_TYPE_STRING) {
            const char *v = NULL;
            dbus_message_iter_get_basic(&var, &v);
            if (v) safe_strncpy(media->title, v, sizeof(media->title));
        } else if (strcmp(key, "Artist") == 0 && vt == DBUS_TYPE_STRING) {
            const char *v = NULL;
            dbus_message_iter_get_basic(&var, &v);
            if (v) safe_strncpy(media->artist, v, sizeof(media->artist));
        } else if (strcmp(key, "Album") == 0 && vt == DBUS_TYPE_STRING) {
            const char *v = NULL;
            dbus_message_iter_get_basic(&var, &v);
            if (v) safe_strncpy(media->album, v, sizeof(media->album));
        } else if (strcmp(key, "Genre") == 0 && vt == DBUS_TYPE_STRING) {
            const char *v = NULL;
            dbus_message_iter_get_basic(&var, &v);
            if (v) safe_strncpy(media->genre, v, sizeof(media->genre));
        } else if (strcmp(key, "Duration") == 0 && vt == DBUS_TYPE_UINT32) {
            dbus_uint32_t v = 0;
            dbus_message_iter_get_basic(&var, &v);
            media->duration = v;
        } else if (strcmp(key, "TrackNumber") == 0 && vt == DBUS_TYPE_UINT32) {
            dbus_uint32_t v = 0;
            dbus_message_iter_get_basic(&var, &v);
            media->track_number = v;
        } else if (strcmp(key, "NumberOfTracks") == 0 && vt == DBUS_TYPE_UINT32) {
            dbus_uint32_t v = 0;
            dbus_message_iter_get_basic(&var, &v);
            media->total_tracks = v;
        } else if (strcmp(key, "Position") == 0 && vt == DBUS_TYPE_UINT32) {
            dbus_uint32_t v = 0;
            dbus_message_iter_get_basic(&var, &v);
            media->position = v;
        } else if (strcmp(key, "Metadata") == 0) {
            DBusMessageIter inner;
            dbus_message_iter_recurse(&var, &inner);
            if (dbus_message_iter_get_arg_type(&inner) == DBUS_TYPE_ARRAY)
                parse_avrcp_metadata(&inner, media);
        }
        dbus_message_iter_next(&dict);
    }
}

static void monitor_avrcp_changes(internal_app_t *app)
{
    if (!app->bus || !app->connected_device_path[0]) return;
    if (!app->player_path[0] || !app->player_verified)
        find_player_path(app);
    if (!app->player_path[0] || !app->player_verified) return;

    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, app->player_path,
                                                    DBUS_PROPS_IFACE, "GetAll");
    if (!msg) return;
    const char *iface = BLUEZ_MEDIA_PLAYER_IFACE;
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 2000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return;
    }
    DBusMessageIter reply_iter;
    if (!dbus_message_iter_init(reply, &reply_iter) ||
        dbus_message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return;
    }
    DBusMessageIter dict;
    dbus_message_iter_recurse(&reply_iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&entry, &key);
        if (!key || !dbus_message_iter_has_next(&entry)) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
            dbus_message_iter_next(&dict);
            continue;
        }
        DBusMessageIter var;
        dbus_message_iter_recurse(&entry, &var);

        if (strcmp(key, "Track") == 0) {
            pthread_mutex_lock(&app->lock);
            memset(&app->current_media, 0, sizeof(app->current_media));
            parse_avrcp_metadata(&var, &app->current_media);
            if (app->current_media.title[0] || app->current_media.artist[0])
                safe_strncpy(app->current_media.status, "playing",
                             sizeof(app->current_media.status));
            pthread_mutex_unlock(&app->lock);
            notify_avrcp_event(app, &app->current_media);
        } else if (strcmp(key, "Status") == 0 &&
                   dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
            const char *status = NULL;
            dbus_message_iter_get_basic(&var, &status);
            if (status) {
                pthread_mutex_lock(&app->lock);
                safe_strncpy(app->current_media.status, status,
                             sizeof(app->current_media.status));
                pthread_mutex_unlock(&app->lock);
                notify_avrcp_event(app, &app->current_media);
            }
        } else if (strcmp(key, "Position") == 0 &&
                   dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_UINT32) {
            dbus_uint32_t pos = 0;
            dbus_message_iter_get_basic(&var, &pos);
            pthread_mutex_lock(&app->lock);
            app->current_media.position = pos;
            pthread_mutex_unlock(&app->lock);
            notify_avrcp_event(app, &app->current_media);
        }
        dbus_message_iter_next(&dict);
    }
    dbus_message_unref(reply);
}

static void send_avrcp_command(const char *command)
{
    if (!g_app.bus) {
        log_msg(&g_app, "ERROR", "No D-Bus connection");
        return;
    }
    if (!g_app.player_path[0] || !g_app.player_verified) {
        find_player_path(&g_app);
        if (g_app.player_path[0])
            g_app.player_verified = verify_player_functional(&g_app);
    }

    typedef struct { const char *iface; const char *method; } method_try_t;
    method_try_t tries[2] = {
        {BLUEZ_MEDIA_PLAYER_IFACE,  command},
        {BLUEZ_MEDIA_CONTROL_IFACE, command},
    };
    bool success = false;

    if (g_app.player_path[0] && g_app.player_verified) {
        for (int i = 0; i < 2 && !success; i++) {
            DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME,
                                                            g_app.player_path,
                                                            tries[i].iface,
                                                            tries[i].method);
            if (!msg) continue;
            DBusError err;
            dbus_error_init(&err);
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                g_app.bus, msg, 5000, &err);
            dbus_message_unref(msg);
            if (!dbus_error_is_set(&err)) {
                log_msg(&g_app, "INFO", "AVRCP %s OK via %s (player)",
                        command, tries[i].iface);
                success = true;
            } else {
                log_msg(&g_app, "DEBUG", "  %s.%s on player: %s",
                        tries[i].iface, tries[i].method, err.message);
                dbus_error_free(&err);
            }
            if (reply) dbus_message_unref(reply);
        }
    }

    if (!success && g_app.connected_device_path[0]) {
        DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME,
                                                        g_app.connected_device_path,
                                                        BLUEZ_MEDIA_CONTROL_IFACE,
                                                        command);
        if (msg) {
            DBusError err;
            dbus_error_init(&err);
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                g_app.bus, msg, 3000, &err);
            dbus_message_unref(msg);
            if (!dbus_error_is_set(&err)) {
                log_msg(&g_app, "INFO", "AVRCP %s OK via device MediaControl1", command);
                success = true;
            } else {
                log_msg(&g_app, "ERROR", "All AVRCP attempts for %s failed: %s",
                        command, err.message);
                dbus_error_free(&err);
            }
            if (reply) dbus_message_unref(reply);
        }
    }

    if (success) {
        pthread_mutex_lock(&g_app.lock);
        if (strcmp(command, "Play") == 0)
            safe_strncpy(g_app.current_media.status, "playing",
                         sizeof(g_app.current_media.status));
        else if (strcmp(command, "Pause") == 0)
            safe_strncpy(g_app.current_media.status, "paused",
                         sizeof(g_app.current_media.status));
        pthread_mutex_unlock(&g_app.lock);
    }
}

static void check_transport_state(void)
{
    if (!g_app.bus || !g_app.connected_device_path[0]) return;

    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/",
                                                    DBUS_OBJMGR_IFACE,
                                                    "GetManagedObjects");
    if (!msg) return;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_app.bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return;
    }
    DBusMessageIter iter, dict;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return;
    }
    dbus_message_iter_recurse(&iter, &dict);

    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, ifaces;
        dbus_message_iter_recurse(&dict, &entry);
        const char *obj_path = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&entry, &obj_path);
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_ARRAY) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_recurse(&entry, &ifaces);

        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter ie;
            dbus_message_iter_recurse(&ifaces, &ie);
            const char *iface = NULL;
            if (dbus_message_iter_get_arg_type(&ie) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&ie, &iface);
            if (iface &&
                strcmp(iface, BLUEZ_MEDIA_TRANSPORT_IFACE) == 0 &&
                obj_path && strstr(obj_path, g_app.connected_device_path)) {
                dbus_bool_t connected = FALSE;
                if (get_prop_bool(g_app.bus, BLUEZ_BUS_NAME, obj_path,
                                  BLUEZ_MEDIA_TRANSPORT_IFACE, "Connected",
                                  &connected)) {
                    if (connected && g_app.state != BT_STATE_PLAYING) {
                        pthread_mutex_lock(&g_app.lock);
                        set_state_locked(&g_app, BT_STATE_PLAYING);
                        g_app.audio_start_time      = time(NULL);
                        g_app.pending_player_find   = true;
                        g_app.pending_avrcp_monitor = true;
                        pthread_mutex_unlock(&g_app.lock);
                        notify_audio_event(&g_app, true);
                    } else if (!connected && g_app.state == BT_STATE_PLAYING) {
                        pthread_mutex_lock(&g_app.lock);
                        set_state_locked(&g_app, BT_STATE_CONNECTED);
                        pthread_mutex_unlock(&g_app.lock);
                        notify_audio_event(&g_app, false);
                    }
                }
            }
            dbus_message_iter_next(&ifaces);
        }
        dbus_message_iter_next(&dict);
    }
    dbus_message_unref(reply);
}

static void query_device_name(void)
{
    if (!g_app.bus || !g_app.connected_device_path[0]) return;
    char name[128] = {0};
    if (get_prop_string(g_app.bus, BLUEZ_BUS_NAME,
                        g_app.connected_device_path,
                        BLUEZ_DEVICE_IFACE, "Name",
                        name, sizeof(name)) && name[0]) {
        pthread_mutex_lock(&g_app.lock);
        safe_strncpy(g_app.connected_device_name, name,
                     sizeof(g_app.connected_device_name));
        pthread_mutex_unlock(&g_app.lock);
        notify_device_event(&g_app, true);
    }
}

static void check_reconnection(internal_app_t *app)
{
    if (!app->bus || !app->adapter_path[0]) return;
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/",
                                                    DBUS_OBJMGR_IFACE,
                                                    "GetManagedObjects");
    if (!msg) return;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        app->bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return;
    }
    DBusMessageIter iter, dict;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        dbus_message_unref(reply);
        return;
    }
    dbus_message_iter_recurse(&iter, &dict);

    bool found_reconnect = false;
    while (!found_reconnect &&
           dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, ifaces;
        dbus_message_iter_recurse(&dict, &entry);
        const char *obj_path = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&entry, &obj_path);
        dbus_message_iter_next(&entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_ARRAY || !obj_path) {
            dbus_message_iter_next(&dict);
            continue;
        }
        dbus_message_iter_recurse(&entry, &ifaces);

        bool is_device = false;
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter ie;
            dbus_message_iter_recurse(&ifaces, &ie);
            const char *iface = NULL;
            if (dbus_message_iter_get_arg_type(&ie) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&ie, &iface);
            if (iface && strcmp(iface, BLUEZ_DEVICE_IFACE) == 0)
                is_device = true;
            dbus_message_iter_next(&ifaces);
        }

        if (is_device) {
            dbus_bool_t connected = FALSE;
            if (get_prop_bool(app->bus, BLUEZ_BUS_NAME, obj_path,
                              BLUEZ_DEVICE_IFACE, "Connected", &connected) &&
                connected && app->connected_device_path[0] == '\0') {
                dbus_bool_t trusted = FALSE;
                get_prop_bool(app->bus, BLUEZ_BUS_NAME, obj_path,
                              BLUEZ_DEVICE_IFACE, "Trusted", &trusted);
                if (trusted) {
                    log_msg(app, "INFO", "Found reconnected trusted device: %s", obj_path);
                    get_prop_string(app->bus, BLUEZ_BUS_NAME, obj_path,
                                    BLUEZ_DEVICE_IFACE, "Address",
                                    app->connected_device_address,
                                    sizeof(app->connected_device_address));
                    if (!get_prop_string(app->bus, BLUEZ_BUS_NAME, obj_path,
                                         BLUEZ_DEVICE_IFACE, "Name",
                                         app->connected_device_name,
                                         sizeof(app->connected_device_name)))
                        get_prop_string(app->bus, BLUEZ_BUS_NAME, obj_path,
                                        BLUEZ_DEVICE_IFACE, "Alias",
                                        app->connected_device_name,
                                        sizeof(app->connected_device_name));
                    if (!app->connected_device_name[0] && app->connected_device_address[0])
                        snprintf(app->connected_device_name,
                                 sizeof(app->connected_device_name),
                                 "Device (%s)", app->connected_device_address);

                    pthread_mutex_lock(&app->lock);
                    safe_strncpy(app->connected_device_path, obj_path,
                                 sizeof(app->connected_device_path));
                    safe_strncpy(app->device_path, obj_path, sizeof(app->device_path));
                    app->connected_time         = time(NULL);
                    app->pending_player_find    = true;
                    app->pending_avrcp_monitor  = true;
                    set_state_locked(app, BT_STATE_CONNECTED);
                    pthread_mutex_unlock(&app->lock);

                    found_reconnect = true;
                    notify_device_event(app, true);
                    check_transport_state();
                    query_device_name();
                }
            }
        }
        dbus_message_iter_next(&dict);
    }
    dbus_message_unref(reply);
}

static void dump_all_objects(internal_app_t *app)
{
    if (!app->bus || !app->verbose) return;
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/",
                                                    DBUS_OBJMGR_IFACE,
                                                    "GetManagedObjects");
    if (!msg) return;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        app->bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (reply) dbus_message_unref(reply);
        return;
    }
    log_msg(app, "DEBUG", "=== BLUEZ OBJECTS ===");
    DBusMessageIter iter, dict;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, ifaces;
        dbus_message_iter_recurse(&dict, &entry);
        const char *obj_path = NULL;
        dbus_message_iter_get_basic(&entry, &obj_path);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &ifaces);
        while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter ie;
            dbus_message_iter_recurse(&ifaces, &ie);
            const char *iface = NULL;
            dbus_message_iter_get_basic(&ie, &iface);
            if (iface && strcmp(iface, BLUEZ_MEDIA_PLAYER_IFACE) == 0)
                log_msg(app, "DEBUG", "Player candidate: %s", obj_path);
            dbus_message_iter_next(&ifaces);
        }
        dbus_message_iter_next(&dict);
    }
    log_msg(app, "DEBUG", "=== END BLUEZ OBJECTS ===");
    dbus_message_unref(reply);
}

static DBusHandlerResult avrcp_handler(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
{
    internal_app_t *app = (internal_app_t *)data;
    const char *member = dbus_message_get_member(msg);
    if (!member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    bool handled = false;

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "Play")     ||
        dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "Pause")    ||
        dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "Next")     ||
        dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "Previous") ||
        dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "VolumeUp") ||
        dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "VolumeDown")) {
        log_msg(app, "INFO", "Remote AVRCP command: %s", member);
        send_avrcp_command(member);
        handled = true;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "GetProperties")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, dict, entry, var;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                         DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                         DBUS_TYPE_STRING_AS_STRING
                                         DBUS_TYPE_VARIANT_AS_STRING
                                         DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                         &dict);
        const char *k = "Status";
        pthread_mutex_lock(&app->lock);
        const char *v = app->current_media.status[0]
                            ? app->current_media.status : "playing";
        pthread_mutex_unlock(&app->lock);
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &v);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&dict, &entry);
        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        handled = true;
    }

    if (dbus_message_is_method_call(msg, BLUEZ_MEDIA_CONTROL_IFACE, "GetTrackInfo")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, dict, entry, var;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                         DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                         DBUS_TYPE_STRING_AS_STRING
                                         DBUS_TYPE_VARIANT_AS_STRING
                                         DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                         &dict);
        pthread_mutex_lock(&app->lock);
        struct { const char *key; const char *val; } str_fields[] = {
            {"Title",  app->current_media.title},
            {"Artist", app->current_media.artist},
            {"Album",  app->current_media.album},
        };
        for (size_t fi = 0; fi < 3; fi++) {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &str_fields[fi].key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
            dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &str_fields[fi].val);
            dbus_message_iter_close_container(&entry, &var);
            dbus_message_iter_close_container(&dict, &entry);
        }
        const char *dk = "Duration";
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &dk);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &app->current_media.duration);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&dict, &entry);
        pthread_mutex_unlock(&app->lock);
        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        handled = true;
    }

    if (handled) return DBUS_HANDLER_RESULT_HANDLED;
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable avrcp_vtable = {
    .message_function = avrcp_handler
};

static void unregister_agent(internal_app_t *app)
{
    if (!app->agent_registered || !app->bus) return;
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                                    BLUEZ_AGENT_MGR_IFACE,
                                                    "UnregisterAgent");
    if (msg) {
        const char *p = AGENT_PATH;
        dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
        DBusError err;
        dbus_error_init(&err);
        DBusMessage *r = dbus_connection_send_with_reply_and_block(
            app->bus, msg, 3000, &err);
        dbus_message_unref(msg);
        if (r) dbus_message_unref(r);
        dbus_error_free(&err);
    }
    dbus_connection_unregister_object_path(app->bus, AGENT_PATH);
    app->agent_registered = false;
}

static DBusHandlerResult agent_handler(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
{
    internal_app_t *app = (internal_app_t *)data;

    if (dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "Release")) {
        app->agent_registered = false;
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "RequestPinCode")) {
        const char *device_path = NULL;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &device_path,
                              DBUS_TYPE_INVALID);
        log_msg(app, "INFO", "Device %s requesting PIN",
                device_path ? device_path : "unknown");
        if (device_path)
            set_prop_bool(app->bus, BLUEZ_BUS_NAME, device_path,
                          BLUEZ_DEVICE_IFACE, "Trusted", TRUE);
        const char *pin = app->pin_code;
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_message_append_args(r, DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "DisplayPinCode")) {
        const char *device_path = NULL, *pin = NULL;
        dbus_message_get_args(msg, NULL,
                              DBUS_TYPE_OBJECT_PATH, &device_path,
                              DBUS_TYPE_STRING, &pin,
                              DBUS_TYPE_INVALID);
        log_msg(app, "INFO", "PIN for %s: %s",
                device_path ? device_path : "unknown", pin ? pin : "?");
        if (device_path) {
            set_prop_bool(app->bus, BLUEZ_BUS_NAME, device_path,
                          BLUEZ_DEVICE_IFACE, "Trusted", TRUE);
            pthread_mutex_lock(&app->lock);
            safe_strncpy(app->connected_device_path, device_path,
                         sizeof(app->connected_device_path));
            app->connected_time = time(NULL);
            set_state_locked(app, BT_STATE_CONNECTED);
            pthread_mutex_unlock(&app->lock);
        }
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "RequestConfirmation")) {
        const char *device_path = NULL;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &device_path,
                              DBUS_TYPE_INVALID);
        log_msg(app, "INFO", "Confirming connection from %s",
                device_path ? device_path : "unknown");
        if (device_path) {
            set_prop_bool(app->bus, BLUEZ_BUS_NAME, device_path,
                          BLUEZ_DEVICE_IFACE, "Trusted", TRUE);
            pthread_mutex_lock(&app->lock);
            safe_strncpy(app->connected_device_path, device_path,
                         sizeof(app->connected_device_path));
            app->connected_time = time(NULL);
            set_state_locked(app, BT_STATE_CONNECTED);
            pthread_mutex_unlock(&app->lock);
        }
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "RequestAuthorization") ||
        dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "AuthorizeService")     ||
        dbus_message_is_method_call(msg, BLUEZ_AGENT_IFACE, "Cancel")) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, r, NULL);
        dbus_message_unref(r);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable agent_vtable = {
    .message_function = agent_handler
};

static bool register_agent(internal_app_t *app)
{
    unregister_agent(app);
    if (!dbus_connection_register_object_path(app->bus, AGENT_PATH,
                                              &agent_vtable, app)) {
        log_msg(app, "ERROR", "Failed to register agent object path");
        return false;
    }
    DBusMessage *msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                                    BLUEZ_AGENT_MGR_IFACE,
                                                    "RegisterAgent");
    if (!msg) {
        dbus_connection_unregister_object_path(app->bus, AGENT_PATH);
        return false;
    }
    const char *path = AGENT_PATH;
    const char *cap  = "KeyboardDisplay";
    dbus_message_append_args(msg,
                             DBUS_TYPE_OBJECT_PATH, &path,
                             DBUS_TYPE_STRING, &cap,
                             DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        app->bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        log_msg(app, "ERROR", "RegisterAgent failed: %s", err.message);
        dbus_error_free(&err);
        dbus_connection_unregister_object_path(app->bus, AGENT_PATH);
        return false;
    }
    if (reply) dbus_message_unref(reply);

    msg = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                       BLUEZ_AGENT_MGR_IFACE,
                                       "RequestDefaultAgent");
    if (msg) {
        dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        dbus_error_init(&err);
        reply = dbus_connection_send_with_reply_and_block(app->bus, msg, 3000, &err);
        dbus_message_unref(msg);
        if (reply) dbus_message_unref(reply);
        if (dbus_error_is_set(&err)) {
            log_msg(app, "WARN", "RequestDefaultAgent: %s", err.message);
            dbus_error_free(&err);
        }
    }
    dbus_connection_flush(app->bus);
    app->agent_registered = true;
    log_msg(app, "INFO", "Agent ready (PIN: %s)", app->pin_code);
    return true;
}

static void handle_signal(internal_app_t *app, DBusMessage *msg)
{
    const char *iface  = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path   = dbus_message_get_path(msg);
    if (!iface || !member) return;
    if (strcmp(member, "PropertiesChanged") != 0 ||
        strcmp(iface, DBUS_PROPS_IFACE) != 0) return;

    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    const char *changed_iface = NULL;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) return;
    dbus_message_iter_get_basic(&iter, &changed_iface);
    if (!changed_iface) return;

    if (strcmp(changed_iface, BLUEZ_DEVICE_IFACE) == 0) {
        dbus_message_iter_next(&iter);
        DBusMessageIter dict;
        dbus_message_iter_recurse(&iter, &dict);
        while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter e, v;
            dbus_message_iter_recurse(&dict, &e);
            const char *prop = NULL;
            dbus_message_iter_get_basic(&e, &prop);
            dbus_message_iter_next(&e);
            dbus_message_iter_recurse(&e, &v);

            if (prop && strcmp(prop, "Connected") == 0) {
                dbus_bool_t connected = FALSE;
                dbus_message_iter_get_basic(&v, &connected);
                if (connected) {
                    get_prop_string(app->bus, BLUEZ_BUS_NAME, path,
                                    BLUEZ_DEVICE_IFACE, "Address",
                                    app->connected_device_address,
                                    sizeof(app->connected_device_address));
                    if (!get_prop_string(app->bus, BLUEZ_BUS_NAME, path,
                                         BLUEZ_DEVICE_IFACE, "Name",
                                         app->connected_device_name,
                                         sizeof(app->connected_device_name)))
                        get_prop_string(app->bus, BLUEZ_BUS_NAME, path,
                                        BLUEZ_DEVICE_IFACE, "Alias",
                                        app->connected_device_name,
                                        sizeof(app->connected_device_name));
                    if (!app->connected_device_name[0] && app->connected_device_address[0])
                        snprintf(app->connected_device_name,
                                 sizeof(app->connected_device_name),
                                 "Device (%s)", app->connected_device_address);
                    pthread_mutex_lock(&app->lock);
                    safe_strncpy(app->connected_device_path, path,
                                 sizeof(app->connected_device_path));
                    safe_strncpy(app->device_path, path, sizeof(app->device_path));
                    app->connected_time         = time(NULL);
                    if (app->state != BT_STATE_PLAYING)
                        set_state_locked(app, BT_STATE_CONNECTED);
                    app->pending_player_find    = true;
                    app->pending_avrcp_monitor  = true;
                    pthread_mutex_unlock(&app->lock);
                    notify_device_event(app, true);
                    query_device_name();
                } else {
                    pthread_mutex_lock(&app->lock);
                    app->connected_device_path[0]    = '\0';
                    app->connected_device_name[0]    = '\0';
                    app->connected_device_address[0] = '\0';
                    app->connected_time              = 0;
                    app->audio_start_time            = 0;
                    app->player_path[0]              = '\0';
                    app->player_verified             = false;
                    app->pending_player_find         = false;
                    app->pending_avrcp_monitor       = false;
                    memset(&app->current_media, 0, sizeof(app->current_media));
                    set_state_locked(app, BT_STATE_ADVERTISING);
                    pthread_mutex_unlock(&app->lock);
                    notify_device_event(app, false);
                    notify_audio_event(app, false);
                }
            }
            if (prop && (strcmp(prop, "Name") == 0 || strcmp(prop, "Alias") == 0)) {
                DBusMessageIter var2;
                dbus_message_iter_recurse(&v, &var2);
                if (dbus_message_iter_get_arg_type(&var2) == DBUS_TYPE_STRING) {
                    const char *val = NULL;
                    dbus_message_iter_get_basic(&var2, &val);
                    if (val && val[0]) {
                        pthread_mutex_lock(&app->lock);
                        safe_strncpy(app->connected_device_name, val,
                                     sizeof(app->connected_device_name));
                        pthread_mutex_unlock(&app->lock);
                        notify_device_event(app, true);
                    }
                }
            }
            dbus_message_iter_next(&dict);
        }
        return;
    }

    if (strcmp(changed_iface, BLUEZ_MEDIA_TRANSPORT_IFACE) == 0) {
        dbus_message_iter_next(&iter);
        DBusMessageIter dict;
        dbus_message_iter_recurse(&iter, &dict);
        while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter e, v;
            dbus_message_iter_recurse(&dict, &e);
            const char *prop = NULL;
            dbus_message_iter_get_basic(&e, &prop);
            dbus_message_iter_next(&e);
            dbus_message_iter_recurse(&e, &v);

            if (prop && strcmp(prop, "Connected") == 0) {
                dbus_bool_t connected = FALSE;
                DBusMessageIter var2;
                dbus_message_iter_recurse(&v, &var2);
                if (dbus_message_iter_get_arg_type(&var2) == DBUS_TYPE_BOOLEAN) {
                    dbus_message_iter_get_basic(&var2, &connected);
                    pthread_mutex_lock(&app->lock);
                    if (connected && app->state != BT_STATE_PLAYING) {
                        set_state_locked(app, BT_STATE_PLAYING);
                        app->audio_start_time      = time(NULL);
                        app->pending_player_find   = true;
                        app->pending_avrcp_monitor = true;
                        pthread_mutex_unlock(&app->lock);
                        notify_audio_event(app, true);
                    } else if (!connected && app->state == BT_STATE_PLAYING) {
                        set_state_locked(app, BT_STATE_CONNECTED);
                        pthread_mutex_unlock(&app->lock);
                        notify_audio_event(app, false);
                    } else {
                        pthread_mutex_unlock(&app->lock);
                    }
                }
            }
            dbus_message_iter_next(&dict);
        }
        return;
    }

    if (strcmp(changed_iface, BLUEZ_MEDIA_PLAYER_IFACE) == 0) {
        if (!path || !app->player_path[0] || strcmp(path, app->player_path) != 0)
            return;

        dbus_message_iter_next(&iter);
        DBusMessageIter dict;
        dbus_message_iter_recurse(&iter, &dict);
        while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter e;
            dbus_message_iter_recurse(&dict, &e);
            const char *prop = NULL;
            if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&e, &prop);
            if (!prop || !dbus_message_iter_has_next(&e)) {
                dbus_message_iter_next(&dict);
                continue;
            }
            dbus_message_iter_next(&e);

            if (strcmp(prop, "Track") == 0) {
                pthread_mutex_lock(&app->lock);
                memset(&app->current_media, 0, sizeof(app->current_media));
                parse_avrcp_metadata(&e, &app->current_media);
                if (app->current_media.title[0] || app->current_media.artist[0])
                    safe_strncpy(app->current_media.status, "playing",
                                 sizeof(app->current_media.status));
                pthread_mutex_unlock(&app->lock);
                notify_avrcp_event(app, &app->current_media);
            } else if (strcmp(prop, "Status") == 0) {
                DBusMessageIter var2;
                dbus_message_iter_recurse(&e, &var2);
                if (dbus_message_iter_get_arg_type(&var2) == DBUS_TYPE_STRING) {
                    const char *status = NULL;
                    dbus_message_iter_get_basic(&var2, &status);
                    if (status) {
                        pthread_mutex_lock(&app->lock);
                        safe_strncpy(app->current_media.status, status,
                                     sizeof(app->current_media.status));
                        pthread_mutex_unlock(&app->lock);
                        notify_avrcp_event(app, &app->current_media);
                    }
                }
            } else if (strcmp(prop, "Position") == 0) {
                DBusMessageIter var2;
                dbus_message_iter_recurse(&e, &var2);
                if (dbus_message_iter_get_arg_type(&var2) == DBUS_TYPE_UINT32) {
                    dbus_uint32_t pos = 0;
                    dbus_message_iter_get_basic(&var2, &pos);
                    pthread_mutex_lock(&app->lock);
                    app->current_media.position = pos;
                    pthread_mutex_unlock(&app->lock);
                    notify_avrcp_event(app, &app->current_media);
                }
            }
            dbus_message_iter_next(&dict);
        }
    }
}

static void *main_loop_thread(void *arg)
{
    internal_app_t *app = (internal_app_t *)arg;
    DBusError err;

    const char *matches[] = {
        "type='signal',interface='" DBUS_PROPS_IFACE "',member='PropertiesChanged'",
        "type='signal',interface='" DBUS_PROPS_IFACE "',member='PropertiesChanged'"
        ",arg0='" BLUEZ_MEDIA_TRANSPORT_IFACE "'",
        "type='signal',interface='" DBUS_PROPS_IFACE "',member='PropertiesChanged'"
        ",arg0='" BLUEZ_MEDIA_PLAYER_IFACE "'",
        NULL
    };
    for (int i = 0; matches[i]; i++) {
        dbus_error_init(&err);
        dbus_bus_add_match(app->bus, matches[i], &err);
        dbus_error_free(&err);
    }

    time_t last_refresh         = 0;
    time_t last_transport_check = 0;
    time_t last_avrcp_poll      = 0;
    time_t last_reconnect_check = 0;
    time_t last_agent_retry     = 0;
    time_t player_find_requested = 0;

    while (app->running) {
        dbus_connection_read_write(app->bus, 10);
        DBusDispatchStatus ds;
        do { ds = dbus_connection_dispatch(app->bus); }
        while (ds == DBUS_DISPATCH_DATA_REMAINS);

        DBusMessage *m;
        while ((m = dbus_connection_pop_message(app->bus)) != NULL) {
            if (dbus_message_get_type(m) == DBUS_MESSAGE_TYPE_SIGNAL)
                handle_signal(app, m);
            dbus_message_unref(m);
        }

        if (app->pa_ml) pa_mainloop_iterate(app->pa_ml, 0, NULL);

        time_t now = time(NULL);

        if (!app->agent_registered && now - last_agent_retry > 10) {
            log_msg(app, "WARN", "Agent not registered, retrying...");
            register_agent(app);
            last_agent_retry = now;
        }

        if (app->pending_player_find) {
            if (player_find_requested == 0) player_find_requested = now;
            if (now - player_find_requested >= 1) {
                app->pending_player_find = false;
                player_find_requested   = 0;
                find_player_path(app);
            }
        }

        if (app->pending_avrcp_monitor && app->player_verified) {
            app->pending_avrcp_monitor = false;
            monitor_avrcp_changes(app);
        }

        if (now - last_transport_check > 2 &&
            (app->state == BT_STATE_CONNECTED || app->state == BT_STATE_PLAYING)) {
            check_transport_state();
            last_transport_check = now;
        }

        if (now - last_avrcp_poll > 5 &&
            (app->state == BT_STATE_CONNECTED || app->state == BT_STATE_PLAYING)) {
            find_player_path(app);
            if (app->player_verified) monitor_avrcp_changes(app);
            last_avrcp_poll = now;
        }

        if (now - last_reconnect_check > 3 &&
            app->connected_device_path[0] == '\0' &&
            app->state == BT_STATE_ADVERTISING) {
            check_reconnection(app);
            last_reconnect_check = now;
        }

        if (now - last_refresh > 30) {
            set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                          BLUEZ_ADAPTER_IFACE, "Discoverable", TRUE);
            set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                          BLUEZ_ADAPTER_IFACE, "Pairable", TRUE);
            last_refresh = now;
        }
    }
    return NULL;
}

static void cleanup(internal_app_t *app)
{
    if (app->bus && app->adapter_path[0]) {
        set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                      BLUEZ_ADAPTER_IFACE, "Discoverable", FALSE);
        set_prop_bool(app->bus, BLUEZ_BUS_NAME, app->adapter_path,
                      BLUEZ_ADAPTER_IFACE, "Pairable", FALSE);
    }
    unregister_agent(app);
    if (app->bus)
        dbus_connection_unregister_object_path(app->bus, PLAYER_PATH);

    if (app->pa_ctx) {
        pa_context_disconnect(app->pa_ctx);
        pa_context_unref(app->pa_ctx);
        app->pa_ctx = NULL;
    }
    if (app->pa_ml) {
        pa_mainloop_free(app->pa_ml);
        app->pa_ml = NULL;
    }
    app->pa_ready = false;

    if (app->bus) {
        dbus_connection_unref(app->bus);
        app->bus = NULL;
    }
    app->initialized = false;
    set_state(app, BT_STATE_IDLE);
}

int bt_speaker_init(const bt_config_t *config)
{
    if (g_app.running) {
        set_error(&g_app, BT_ERROR_ALREADY_RUNNING, "Already running");
        return -1;
    }

    bool     saved_bluez_loaded = g_app.pa_bluez_module_loaded;

    if (g_app.initialized) pthread_mutex_destroy(&g_app.lock);

    memset(&g_app, 0, sizeof(g_app));
    g_app.transport_fd           = -1;
    g_app.pa_bluez_module_loaded = saved_bluez_loaded;

    pthread_mutex_init(&g_app.lock, NULL);

    safe_strncpy(g_app.device_name, "Aroma Speaker", sizeof(g_app.device_name));
    safe_strncpy(g_app.pin_code,    "0000",          sizeof(g_app.pin_code));

    if (config) {
        if (config->device_name)
            safe_strncpy(g_app.device_name, config->device_name, sizeof(g_app.device_name));
        if (config->pin_code)
            safe_strncpy(g_app.pin_code, config->pin_code, sizeof(g_app.pin_code));
        g_app.verbose        = config->verbose;
        g_app.state_cb       = config->state_cb;
        g_app.state_cb_data  = config->state_cb_data;
        g_app.device_cb      = config->device_cb;
        g_app.device_cb_data = config->device_cb_data;
        g_app.error_cb       = config->error_cb;
        g_app.error_cb_data  = config->error_cb_data;
        g_app.audio_cb       = config->audio_cb;
        g_app.audio_cb_data  = config->audio_cb_data;
        g_app.log_cb         = config->log_cb;
        g_app.log_cb_data    = config->log_cb_data;
        g_app.avrcp_cb       = config->avrcp_cb;
        g_app.avrcp_cb_data  = config->avrcp_cb_data;
    }

    set_state(&g_app, BT_STATE_INITIALIZING);

    DBusError err;
    dbus_error_init(&err);
    g_app.bus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!g_app.bus || dbus_error_is_set(&err)) {
        set_error(&g_app, BT_ERROR_DBUS,
                  err.message ? err.message : "D-Bus connection failed");
        dbus_error_free(&err);
        return -1;
    }

    static const char *stale_agents[] = {
        "/org/bluez/agent/gnome",
        "/org/bluez/agent",
        "/org/bluez/agent/obex",
        NULL
    };
    for (int i = 0; stale_agents[i]; i++) {
        DBusMessage *m = dbus_message_new_method_call(BLUEZ_BUS_NAME, "/org/bluez",
                                                      BLUEZ_AGENT_MGR_IFACE,
                                                      "UnregisterAgent");
        if (m) {
            const char *p = stale_agents[i];
            dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
            DBusError de;
            dbus_error_init(&de);
            DBusMessage *r = dbus_connection_send_with_reply_and_block(
                g_app.bus, m, 2000, &de);
            dbus_message_unref(m);
            if (r) dbus_message_unref(r);
            dbus_error_free(&de);
        }
    }

    dbus_bus_request_name(g_app.bus, "com.btspeaker.app",
                          DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    dbus_error_free(&err);

    if (!find_adapter(&g_app)) {
        set_error(&g_app, BT_ERROR_ADAPTER_NOT_FOUND, "No Bluetooth adapter found");
        return -1;
    }
    if (!configure_adapter(&g_app)) {
        set_error(&g_app, BT_ERROR_CONFIG_FAILED, "Failed to configure adapter");
        return -1;
    }
    if (!register_endpoint(&g_app)) {
        set_error(&g_app, BT_ERROR_ENDPOINT_FAILED, "Failed to register A2DP endpoint");
        return -1;
    }

    dbus_connection_register_object_path(g_app.bus, PLAYER_PATH, &avrcp_vtable, &g_app);

    if (!register_a2dp_profile(&g_app))
        log_msg(&g_app, "WARN", "A2DP profile registration failed");
    if (!register_avrcp_profile(&g_app))
        log_msg(&g_app, "WARN", "AVRCP profile registration failed");
    if (!register_agent(&g_app))
        log_msg(&g_app, "WARN",
                "Initial agent registration failed, will retry in main loop");

    if (init_pulseaudio(&g_app))
        load_pa_bt_modules(&g_app);
    else
        log_msg(&g_app, "WARN",
                "PulseAudio init failed – ensure pulseaudio is running.");

    g_app.initialized = true;
    set_state(&g_app, BT_STATE_ADVERTISING);
    return 0;
}

int bt_speaker_start(void)
{
    if (!g_app.initialized) {
        set_error(&g_app, BT_ERROR_CONFIG_FAILED, "Not initialized");
        return -1;
    }
    if (g_app.running) return 0;
    g_app.running = true;
    if (pthread_create(&g_app.main_thread, NULL, main_loop_thread, &g_app) != 0) {
        g_app.running = false;
        set_error(&g_app, BT_ERROR_CONFIG_FAILED, "Failed to create main thread");
        return -1;
    }
    return 0;
}

int bt_speaker_stop(void)
{
    if (!g_app.running) return 0;
    g_app.running = false;
    pthread_join(g_app.main_thread, NULL);
    cleanup(&g_app);
    return 0;
}

void bt_speaker_cleanup(void)
{
    if (g_app.running) bt_speaker_stop();
    else if (g_app.initialized) cleanup(&g_app);
    pthread_mutex_destroy(&g_app.lock);

    bool saved_bluez = g_app.pa_bluez_module_loaded;
    memset(&g_app, 0, sizeof(g_app));
    g_app.transport_fd           = -1;
    g_app.pa_bluez_module_loaded = saved_bluez;
}

bt_state_t bt_speaker_get_state(void)
{
    pthread_mutex_lock(&g_app.lock);
    bt_state_t s = g_app.state;
    pthread_mutex_unlock(&g_app.lock);
    return s;
}

const char *bt_speaker_get_state_string(void)
{
    switch (bt_speaker_get_state()) {
    case BT_STATE_IDLE:         return "Idle";
    case BT_STATE_INITIALIZING: return "Initializing";
    case BT_STATE_ADVERTISING:  return "Advertising";
    case BT_STATE_PAIRED:       return "Paired";
    case BT_STATE_CONNECTED:    return "Connected";
    case BT_STATE_PLAYING:      return "Playing";
    case BT_STATE_ERROR:        return "Error";
    default:                    return "Unknown";
    }
}

bt_device_info_t bt_speaker_get_device_info(void)
{
    bt_device_info_t info = {0};
    pthread_mutex_lock(&g_app.lock);
    if (g_app.connected_device_path[0] && !g_app.connected_device_name[0]) {
        if (!get_prop_string(g_app.bus, BLUEZ_BUS_NAME,
                             g_app.connected_device_path,
                             BLUEZ_DEVICE_IFACE, "Name",
                             g_app.connected_device_name,
                             sizeof(g_app.connected_device_name)))
            get_prop_string(g_app.bus, BLUEZ_BUS_NAME,
                            g_app.connected_device_path,
                            BLUEZ_DEVICE_IFACE, "Alias",
                            g_app.connected_device_name,
                            sizeof(g_app.connected_device_name));
    }
    safe_strncpy(info.name,    g_app.connected_device_name,    sizeof(info.name));
    safe_strncpy(info.path,    g_app.connected_device_path,    sizeof(info.path));
    safe_strncpy(info.address, g_app.connected_device_address, sizeof(info.address));
    info.connected = (g_app.connected_device_path[0] != '\0');
    pthread_mutex_unlock(&g_app.lock);
    return info;
}

bt_media_info_t bt_speaker_get_media_info(void)
{
    bt_media_info_t info = {0};
    pthread_mutex_lock(&g_app.lock);
    info = g_app.current_media;
    pthread_mutex_unlock(&g_app.lock);
    return info;
}

static void ensure_player(void)
{
    if (!g_app.player_path[0] || !g_app.player_verified) {
        find_player_path(&g_app);
        if (g_app.player_path[0])
            g_app.player_verified = verify_player_functional(&g_app);
    }
}

int bt_speaker_avrcp_play(void)
{
    if (!g_app.connected_device_path[0]) return -1;
    ensure_player();
    send_avrcp_command("Play");
    return 0;
}

int bt_speaker_avrcp_pause(void)
{
    if (!g_app.connected_device_path[0]) return -1;
    ensure_player();
    send_avrcp_command("Pause");
    return 0;
}

int bt_speaker_avrcp_next(void)
{
    if (!g_app.connected_device_path[0]) return -1;
    ensure_player();
    send_avrcp_command("Next");
    return 0;
}

int bt_speaker_avrcp_previous(void)
{
    if (!g_app.connected_device_path[0]) return -1;
    ensure_player();
    send_avrcp_command("Previous");
    return 0;
}

int bt_speaker_avrcp_volume_up(void)
{
    send_avrcp_command("VolumeUp");
    return 0;
}

int bt_speaker_avrcp_volume_down(void)
{
    send_avrcp_command("VolumeDown");
    return 0;
}

bt_stats_t bt_speaker_get_stats(void)
{
    bt_stats_t stats = {0};
    pthread_mutex_lock(&g_app.lock);
    if (g_app.connected_time > 0)
        stats.connected_time_sec = (long)(time(NULL) - g_app.connected_time);
    if (g_app.audio_start_time > 0)
        stats.audio_time_sec = (long)(time(NULL) - g_app.audio_start_time);
    stats.audio_active = (g_app.state == BT_STATE_PLAYING);
    pthread_mutex_unlock(&g_app.lock);
    return stats;
}

bt_error_t  bt_speaker_get_last_error(void)         { return g_app.last_error; }
const char *bt_speaker_get_last_error_message(void) { return g_app.last_error_msg; }
bool        bt_speaker_is_running(void)              { return g_app.running; }

int bt_speaker_set_discoverable(bool d)
{
    if (!g_app.initialized || !g_app.bus || !g_app.adapter_path[0]) return -1;
    return set_prop_bool(g_app.bus, BLUEZ_BUS_NAME, g_app.adapter_path,
                         BLUEZ_ADAPTER_IFACE, "Discoverable", d) ? 0 : -1;
}

int bt_speaker_set_pairable(bool p)
{
    if (!g_app.initialized || !g_app.bus || !g_app.adapter_path[0]) return -1;
    return set_prop_bool(g_app.bus, BLUEZ_BUS_NAME, g_app.adapter_path,
                         BLUEZ_ADAPTER_IFACE, "Pairable", p) ? 0 : -1;
}

int bt_speaker_set_device_name(const char *name)
{
    if (!name) return -1;
    pthread_mutex_lock(&g_app.lock);
    safe_strncpy(g_app.device_name, name, sizeof(g_app.device_name));
    pthread_mutex_unlock(&g_app.lock);
    if (g_app.initialized && g_app.bus && g_app.adapter_path[0])
        set_prop_str(g_app.bus, BLUEZ_BUS_NAME, g_app.adapter_path,
                     BLUEZ_ADAPTER_IFACE, "Alias", g_app.device_name);
    return 0;
}

void bt_speaker_set_state_callback(bt_state_callback_t cb, void *ud)
{
    g_app.state_cb      = cb;
    g_app.state_cb_data = ud;
}

void bt_speaker_set_device_callback(bt_device_callback_t cb, void *ud)
{
    g_app.device_cb      = cb;
    g_app.device_cb_data = ud;
}

void bt_speaker_set_error_callback(bt_error_callback_t cb, void *ud)
{
    g_app.error_cb      = cb;
    g_app.error_cb_data = ud;
}

void bt_speaker_set_audio_callback(bt_audio_callback_t cb, void *ud)
{
    g_app.audio_cb      = cb;
    g_app.audio_cb_data = ud;
}

void bt_speaker_set_log_callback(bt_log_callback_t cb, void *ud)
{
    g_app.log_cb      = cb;
    g_app.log_cb_data = ud;
}

void bt_speaker_set_avrcp_callback(bt_avrcp_callback_t cb, void *ud)
{
    g_app.avrcp_cb      = cb;
    g_app.avrcp_cb_data = ud;
}