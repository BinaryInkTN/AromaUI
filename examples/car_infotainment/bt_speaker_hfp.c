#include "bt_speaker_hfp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include <dbus/dbus.h>

void safe_strncpy(char *dest, const char *src, size_t n);

#define OFONO_BUS_NAME "org.ofono"
#define OFONO_MANAGER_IFACE "org.ofono.Manager"
#define OFONO_MODEM_IFACE "org.ofono.Modem"
#define OFONO_VCM_IFACE "org.ofono.VoiceCallManager"
#define OFONO_VC_IFACE "org.ofono.VoiceCall"

#define OBEX_BUS_NAME "org.bluez.obex"
#define OBEX_CLIENT_IFACE "org.bluez.obex.Client1"
#define OBEX_SESSION_IFACE "org.bluez.obex.Session1"
#define OBEX_PBAP_IFACE "org.bluez.obex.PhonebookAccess1"
#define OBEX_TRANSFER_IFACE "org.bluez.obex.Transfer1"

#define DBUS_PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_OBJMGR_IFACE "org.freedesktop.DBus.ObjectManager"

#define MAX_TRACKED_CALLS 16

typedef struct
{
    char path[256];
    char line_id[64];
    char name[128];
    bt_call_state_t state;
    bool multiparty;
    bool in_use;
} tracked_call_t;

typedef struct
{
    DBusConnection *session_bus;
    DBusConnection *obex_bus;
    char modem_path[256];
    tracked_call_t calls[MAX_TRACKED_CALLS];
    bt_call_callback_t call_cb;
    void *call_cb_data;
    char last_error[256];
} hfp_state_t;

static hfp_state_t g_hfp = {0};

static void hfp_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_hfp.last_error, sizeof(g_hfp.last_error), fmt, ap);
    va_end(ap);
}

const char *bt_hfp_get_last_error_message(void)
{
    return g_hfp.last_error;
}

static bool hfp_get_prop_string(DBusConnection *bus, const char *dest,
                                const char *path, const char *iface,
                                const char *prop, char *out, size_t out_len)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Get");
    if (!msg)
        return false;
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop,
                             DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return false;
    }
    DBusMessageIter iter, var;
    dbus_message_iter_init(reply, &iter);
    bool ok = false;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
    {
        dbus_message_iter_recurse(&iter, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING)
        {
            const char *val = NULL;
            dbus_message_iter_get_basic(&var, &val);
            if (val)
            {
                safe_strncpy(out, val, out_len);
                ok = true;
            }
        }
    }
    dbus_message_unref(reply);
    return ok;
}

static bool hfp_get_prop_bool(DBusConnection *bus, const char *dest,
                              const char *path, const char *iface,
                              const char *prop, dbus_bool_t *out)
{
    DBusMessage *msg = dbus_message_new_method_call(dest, path,
                                                    DBUS_PROPS_IFACE, "Get");
    if (!msg)
        return false;
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop,
                             DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return false;
    }
    DBusMessageIter iter, var;
    dbus_message_iter_init(reply, &iter);
    bool ok = false;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
    {
        dbus_message_iter_recurse(&iter, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN)
        {
            dbus_message_iter_get_basic(&var, out);
            ok = true;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

static bool hfp_find_modem(void)
{
    if (!g_hfp.session_bus)
        return false;

    DBusMessage *msg = dbus_message_new_method_call(OFONO_BUS_NAME, "/",
                                                    OFONO_MANAGER_IFACE,
                                                    "GetModems");
    if (!msg)
        return false;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.session_bus, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        hfp_set_error("oFono GetModems failed: %s",
                      dbus_error_is_set(&err) ? err.message : "no reply");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return false;
    }

    DBusMessageIter iter, arr;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
    {
        dbus_message_unref(reply);
        hfp_set_error("oFono GetModems: unexpected reply shape");
        return false;
    }
    dbus_message_iter_recurse(&iter, &arr);

    bool found = false;
    while (!found && dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT)
    {
        DBusMessageIter st, props;
        dbus_message_iter_recurse(&arr, &st);
        const char *path = NULL;
        if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&st, &path);
        dbus_message_iter_next(&st);
        if (!path || dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_ARRAY)
        {
            dbus_message_iter_next(&arr);
            continue;
        }
        dbus_message_iter_recurse(&st, &props);

        bool powered = false, online = false;
        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter e, v;
            dbus_message_iter_recurse(&props, &e);
            const char *key = NULL;
            if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&e, &key);
            dbus_message_iter_next(&e);
            if (key && dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_VARIANT)
            {
                dbus_message_iter_recurse(&e, &v);
                if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_BOOLEAN)
                {
                    dbus_bool_t b = FALSE;
                    dbus_message_iter_get_basic(&v, &b);
                    if (strcmp(key, "Powered") == 0)
                        powered = b;
                    else if (strcmp(key, "Online") == 0)
                        online = b;
                }
            }
            dbus_message_iter_next(&props);
        }

        if (powered && online)
        {
            safe_strncpy(g_hfp.modem_path, path, sizeof(g_hfp.modem_path));
            found = true;
        }
        dbus_message_iter_next(&arr);
    }
    dbus_message_unref(reply);

    if (!found)
    {
        g_hfp.modem_path[0] = '\0';
        hfp_set_error("No Online+Powered oFono modem found");
    }
    return found;
}

static bt_call_state_t hfp_parse_call_state(const char *s)
{
    if (!s)
        return BT_CALL_STATE_UNKNOWN;
    if (strcmp(s, "active") == 0)
        return BT_CALL_STATE_ACTIVE;
    if (strcmp(s, "held") == 0)
        return BT_CALL_STATE_HELD;
    if (strcmp(s, "dialing") == 0)
        return BT_CALL_STATE_DIALING;
    if (strcmp(s, "alerting") == 0)
        return BT_CALL_STATE_ALERTING;
    if (strcmp(s, "incoming") == 0)
        return BT_CALL_STATE_INCOMING;
    if (strcmp(s, "waiting") == 0)
        return BT_CALL_STATE_WAITING;
    if (strcmp(s, "disconnected") == 0)
        return BT_CALL_STATE_DISCONNECTED;
    return BT_CALL_STATE_UNKNOWN;
}

static tracked_call_t *hfp_find_tracked(const char *path)
{
    for (int i = 0; i < MAX_TRACKED_CALLS; i++)
        if (g_hfp.calls[i].in_use && strcmp(g_hfp.calls[i].path, path) == 0)
            return &g_hfp.calls[i];
    return NULL;
}

static tracked_call_t *hfp_alloc_tracked(const char *path)
{
    for (int i = 0; i < MAX_TRACKED_CALLS; i++)
    {
        if (!g_hfp.calls[i].in_use)
        {
            memset(&g_hfp.calls[i], 0, sizeof(g_hfp.calls[i]));
            safe_strncpy(g_hfp.calls[i].path, path, sizeof(g_hfp.calls[i].path));
            g_hfp.calls[i].in_use = true;
            return &g_hfp.calls[i];
        }
    }
    return NULL;
}

static void hfp_notify_call(const tracked_call_t *tc, bool removed)
{
    if (!g_hfp.call_cb)
        return;
    bt_call_info_t info = {0};
    safe_strncpy(info.path, tc->path, sizeof(info.path));
    safe_strncpy(info.line_id, tc->line_id, sizeof(info.line_id));
    safe_strncpy(info.name, tc->name, sizeof(info.name));
    info.state = tc->state;
    info.multiparty = tc->multiparty;
    g_hfp.call_cb(&info, removed, g_hfp.call_cb_data);
}

static void hfp_apply_call_props(DBusMessageIter *props_iter, tracked_call_t *tc)
{
    while (dbus_message_iter_get_arg_type(props_iter) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter e, v;
        dbus_message_iter_recurse(props_iter, &e);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&e) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&e, &key);
        dbus_message_iter_next(&e);
        if (!key || dbus_message_iter_get_arg_type(&e) != DBUS_TYPE_VARIANT)
        {
            dbus_message_iter_next(props_iter);
            continue;
        }
        dbus_message_iter_recurse(&e, &v);
        int vt = dbus_message_iter_get_arg_type(&v);

        if (strcmp(key, "State") == 0 && vt == DBUS_TYPE_STRING)
        {
            const char *s = NULL;
            dbus_message_iter_get_basic(&v, &s);
            tc->state = hfp_parse_call_state(s);
        }
        else if (strcmp(key, "LineIdentification") == 0 && vt == DBUS_TYPE_STRING)
        {
            const char *s = NULL;
            dbus_message_iter_get_basic(&v, &s);
            if (s)
                safe_strncpy(tc->line_id, s, sizeof(tc->line_id));
        }
        else if (strcmp(key, "Name") == 0 && vt == DBUS_TYPE_STRING)
        {
            const char *s = NULL;
            dbus_message_iter_get_basic(&v, &s);
            if (s)
                safe_strncpy(tc->name, s, sizeof(tc->name));
        }
        else if (strcmp(key, "Multiparty") == 0 && vt == DBUS_TYPE_BOOLEAN)
        {
            dbus_bool_t b = FALSE;
            dbus_message_iter_get_basic(&v, &b);
            tc->multiparty = b;
        }
        dbus_message_iter_next(props_iter);
    }
}

static void hfp_handle_call_added(DBusMessage *msg)
{
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    const char *path = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
        dbus_message_iter_get_basic(&iter, &path);
    dbus_message_iter_next(&iter);
    if (!path || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;

    tracked_call_t *tc = hfp_find_tracked(path);
    if (!tc)
        tc = hfp_alloc_tracked(path);
    if (!tc)
        return;

    hfp_apply_call_props(&iter, tc);
    hfp_notify_call(tc, false);
}

static void hfp_handle_call_removed(DBusMessage *msg)
{
    const char *path = NULL;
    dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
    if (!path)
        return;
    tracked_call_t *tc = hfp_find_tracked(path);
    if (!tc)
        return;
    tc->state = BT_CALL_STATE_DISCONNECTED;
    hfp_notify_call(tc, true);
    tc->in_use = false;
}

static void hfp_handle_call_props_changed(DBusMessage *msg)
{
    const char *path = dbus_message_get_path(msg);
    if (!path)
        return;
    tracked_call_t *tc = hfp_find_tracked(path);
    if (!tc)
        return;

    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return;
    dbus_message_iter_next(&iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;

    hfp_apply_call_props(&iter, tc);
    hfp_notify_call(tc, false);
}

static void hfp_handle_legacy_call_property_changed(DBusMessage *msg)
{
    const char *path = dbus_message_get_path(msg);
    if (!path)
        return;
    tracked_call_t *tc = hfp_find_tracked(path);
    if (!tc)
        return;

    DBusMessageIter iter, var;
    dbus_message_iter_init(msg, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return;
    const char *key = NULL;
    dbus_message_iter_get_basic(&iter, &key);
    dbus_message_iter_next(&iter);
    if (!key || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        return;
    dbus_message_iter_recurse(&iter, &var);
    int vt = dbus_message_iter_get_arg_type(&var);

    if (strcmp(key, "State") == 0 && vt == DBUS_TYPE_STRING)
    {
        const char *s = NULL;
        dbus_message_iter_get_basic(&var, &s);
        tc->state = hfp_parse_call_state(s);
    }
    else if (strcmp(key, "LineIdentification") == 0 && vt == DBUS_TYPE_STRING)
    {
        const char *s = NULL;
        dbus_message_iter_get_basic(&var, &s);
        if (s)
            safe_strncpy(tc->line_id, s, sizeof(tc->line_id));
    }
    else if (strcmp(key, "Name") == 0 && vt == DBUS_TYPE_STRING)
    {
        const char *s = NULL;
        dbus_message_iter_get_basic(&var, &s);
        if (s)
            safe_strncpy(tc->name, s, sizeof(tc->name));
    }
    else if (strcmp(key, "Multiparty") == 0 && vt == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t b = FALSE;
        dbus_message_iter_get_basic(&var, &b);
        tc->multiparty = b;
    }

    hfp_notify_call(tc, false);
}

void bt_hfp_poll(void)
{
    if (!g_hfp.session_bus)
        return;

    dbus_connection_read_write(g_hfp.session_bus, 0);
    DBusDispatchStatus ds;
    do
    {
        ds = dbus_connection_dispatch(g_hfp.session_bus);
    } while (ds == DBUS_DISPATCH_DATA_REMAINS);

    DBusMessage *m;
    while ((m = dbus_connection_pop_message(g_hfp.session_bus)) != NULL)
    {
        if (dbus_message_get_type(m) == DBUS_MESSAGE_TYPE_SIGNAL)
        {
            const char *iface = dbus_message_get_interface(m);
            const char *member = dbus_message_get_member(m);
            if (iface && member && strcmp(iface, OFONO_VCM_IFACE) == 0)
            {
                if (strcmp(member, "CallAdded") == 0)
                    hfp_handle_call_added(m);
                else if (strcmp(member, "CallRemoved") == 0)
                    hfp_handle_call_removed(m);
            }
            else if (iface && member &&
                     strcmp(iface, DBUS_PROPS_IFACE) == 0 &&
                     strcmp(member, "PropertiesChanged") == 0)
            {
                hfp_handle_call_props_changed(m);
            }
            else if (iface && member &&
                     strcmp(iface, OFONO_VC_IFACE) == 0 &&
                     strcmp(member, "PropertyChanged") == 0)
            {
                hfp_handle_legacy_call_property_changed(m);
            }
        }
        dbus_message_unref(m);
    }

    if (g_hfp.modem_path[0] == '\0')
    {
        static time_t last_retry = 0;
        time_t now = time(NULL);
        if (now - last_retry >= 5)
        {
            hfp_find_modem();
            last_retry = now;
        }
    }
}

int bt_hfp_init(void)
{
    if (g_hfp.session_bus)
        return 0;

    memset(&g_hfp, 0, sizeof(g_hfp));

    DBusError err;
    dbus_error_init(&err);
    g_hfp.session_bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (!g_hfp.session_bus || dbus_error_is_set(&err))
    {
        hfp_set_error("System bus connection failed: %s",
                      dbus_error_is_set(&err) ? err.message : "unknown");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        return -1;
    }
    dbus_connection_set_exit_on_disconnect(g_hfp.session_bus, FALSE);

    dbus_error_init(&err);
    g_hfp.obex_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!g_hfp.obex_bus || dbus_error_is_set(&err))
    {
        hfp_set_error("Session bus connection failed (OBEX unavailable): %s",
                      dbus_error_is_set(&err) ? err.message : "unknown");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
    }
    else
    {
        dbus_connection_set_exit_on_disconnect(g_hfp.obex_bus, FALSE);
    }

    const char *matches[] = {
        "type='signal',sender='" OFONO_BUS_NAME "',interface='" OFONO_VCM_IFACE "',member='CallAdded'",
        "type='signal',sender='" OFONO_BUS_NAME "',interface='" OFONO_VCM_IFACE "',member='CallRemoved'",
        "type='signal',interface='" DBUS_PROPS_IFACE "',member='PropertiesChanged'",
        "type='signal',sender='" OFONO_BUS_NAME "',interface='" OFONO_VC_IFACE "',member='PropertyChanged'",
        NULL};
    for (int i = 0; matches[i]; i++)
    {
        dbus_error_init(&err);
        dbus_bus_add_match(g_hfp.session_bus, matches[i], &err);
        dbus_error_free(&err);
    }
    dbus_connection_flush(g_hfp.session_bus);

    hfp_find_modem();

    return 0;
}

void bt_hfp_cleanup(void)
{
    if (g_hfp.session_bus)
    {
        dbus_connection_close(g_hfp.session_bus);
        dbus_connection_unref(g_hfp.session_bus);
    }
    if (g_hfp.obex_bus)
    {
        dbus_connection_close(g_hfp.obex_bus);
        dbus_connection_unref(g_hfp.obex_bus);
    }
    memset(&g_hfp, 0, sizeof(g_hfp));
}

int bt_hfp_dial(const char *number)
{
    if (!number || !number[0])
    {
        hfp_set_error("bt_hfp_dial: empty number");
        return -1;
    }

    if (g_hfp.session_bus)
    {
        if (g_hfp.modem_path[0] == '\0')
        {
            hfp_find_modem();
        }

        if (g_hfp.modem_path[0] != '\0')
        {
            DBusMessage *msg = dbus_message_new_method_call(
                OFONO_BUS_NAME, g_hfp.modem_path, OFONO_VCM_IFACE, "Dial");
            if (msg)
            {
                const char *hide_callerid = "default";
                dbus_message_append_args(msg,
                                         DBUS_TYPE_STRING, &number,
                                         DBUS_TYPE_STRING, &hide_callerid,
                                         DBUS_TYPE_INVALID);

                DBusError err;
                dbus_error_init(&err);
                DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                    g_hfp.session_bus, msg, 10000, &err);
                dbus_message_unref(msg);
                if (reply)
                {
                    dbus_message_unref(reply);
                    return 0;
                }
                if (dbus_error_is_set(&err))
                    dbus_error_free(&err);
            }
        }
    }

    DBusError err;
    dbus_error_init(&err);
    DBusConnection *sys_bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (!sys_bus)
    {
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        hfp_set_error("Cannot connect to system bus for dial");
        return -1;
    }

    char modem_path[256] = {0};
    DBusMessage *modem_msg = dbus_message_new_method_call(
        OFONO_BUS_NAME, "/", OFONO_MANAGER_IFACE, "GetModems");
    if (modem_msg)
    {
        dbus_error_init(&err);
        DBusMessage *modem_reply = dbus_connection_send_with_reply_and_block(
            sys_bus, modem_msg, 3000, &err);
        dbus_message_unref(modem_msg);

        if (modem_reply && !dbus_error_is_set(&err))
        {
            DBusMessageIter iter, arr;
            dbus_message_iter_init(modem_reply, &iter);
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY)
            {
                dbus_message_iter_recurse(&iter, &arr);
                if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter st;
                    dbus_message_iter_recurse(&arr, &st);
                    if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_OBJECT_PATH)
                    {
                        const char *path = NULL;
                        dbus_message_iter_get_basic(&st, &path);
                        if (path)
                            safe_strncpy(modem_path, path, sizeof(modem_path));
                    }
                }
            }
            dbus_message_unref(modem_reply);
        }
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
    }

    if (modem_path[0] == '\0')
    {
        dbus_connection_close(sys_bus);
        dbus_connection_unref(sys_bus);
        hfp_set_error("No modem found on system bus");
        return -1;
    }

    DBusMessage *dial_msg = dbus_message_new_method_call(
        OFONO_BUS_NAME, modem_path, OFONO_VCM_IFACE, "Dial");
    if (!dial_msg)
    {
        dbus_connection_close(sys_bus);
        dbus_connection_unref(sys_bus);
        hfp_set_error("Failed to create dial message");
        return -1;
    }

    const char *hide_callerid = "default";
    dbus_message_append_args(dial_msg,
                             DBUS_TYPE_STRING, &number,
                             DBUS_TYPE_STRING, &hide_callerid,
                             DBUS_TYPE_INVALID);

    dbus_error_init(&err);
    DBusMessage *dial_reply = dbus_connection_send_with_reply_and_block(
        sys_bus, dial_msg, 10000, &err);
    dbus_message_unref(dial_msg);

    int result = 0;
    if (!dial_reply || dbus_error_is_set(&err))
    {
        hfp_set_error("Dial failed on system bus: %s",
                      dbus_error_is_set(&err) ? err.message : "no reply");
        result = -1;
    }

    if (dial_reply)
        dbus_message_unref(dial_reply);
    if (dbus_error_is_set(&err))
        dbus_error_free(&err);

    dbus_connection_close(sys_bus);
    dbus_connection_unref(sys_bus);

    return result;
}

int bt_hfp_hangup(const char *call_path)
{
    if (!call_path || !call_path[0] || !g_hfp.session_bus)
    {
        hfp_set_error("bt_hfp_hangup: invalid arguments/state");
        return -1;
    }
    DBusMessage *msg = dbus_message_new_method_call(
        OFONO_BUS_NAME, call_path, OFONO_VC_IFACE, "Hangup");
    if (!msg)
        return -1;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.session_bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        hfp_set_error("Hangup failed: %s",
                      dbus_error_is_set(&err) ? err.message : "no reply");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return -1;
    }
    dbus_message_unref(reply);
    return 0;
}

int bt_hfp_hangup_all(void)
{
    if (!g_hfp.session_bus)
    {
        hfp_set_error("bt_hfp_hangup_all: not initialized");
        return -1;
    }
    if (g_hfp.modem_path[0] == '\0')
    {
        hfp_set_error("bt_hfp_hangup_all: no modem");
        return -1;
    }
    DBusMessage *msg = dbus_message_new_method_call(
        OFONO_BUS_NAME, g_hfp.modem_path, OFONO_VCM_IFACE, "HangupAll");
    if (!msg)
        return -1;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.session_bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        hfp_set_error("HangupAll failed: %s",
                      dbus_error_is_set(&err) ? err.message : "no reply");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return -1;
    }
    dbus_message_unref(reply);
    return 0;
}

int bt_hfp_answer(const char *call_path)
{
    if (!call_path || !call_path[0] || !g_hfp.session_bus)
    {
        hfp_set_error("bt_hfp_answer: invalid arguments/state");
        return -1;
    }
    DBusMessage *msg = dbus_message_new_method_call(
        OFONO_BUS_NAME, call_path, OFONO_VC_IFACE, "Answer");
    if (!msg)
        return -1;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.session_bus, msg, 5000, &err);
    dbus_message_unref(msg);
    if (!reply || dbus_error_is_set(&err))
    {
        hfp_set_error("Answer failed: %s",
                      dbus_error_is_set(&err) ? err.message : "no reply");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        if (reply)
            dbus_message_unref(reply);
        return -1;
    }
    dbus_message_unref(reply);
    return 0;
}

int bt_hfp_get_active_calls(bt_call_info_t *out_calls, size_t max_calls)
{
    if (!out_calls || max_calls == 0)
        return -1;
    int n = 0;
    for (int i = 0; i < MAX_TRACKED_CALLS && (size_t)n < max_calls; i++)
    {
        if (!g_hfp.calls[i].in_use)
            continue;
        bt_call_info_t *dst = &out_calls[n];
        memset(dst, 0, sizeof(*dst));
        safe_strncpy(dst->path, g_hfp.calls[i].path, sizeof(dst->path));
        safe_strncpy(dst->line_id, g_hfp.calls[i].line_id, sizeof(dst->line_id));
        safe_strncpy(dst->name, g_hfp.calls[i].name, sizeof(dst->name));
        dst->state = g_hfp.calls[i].state;
        dst->multiparty = g_hfp.calls[i].multiparty;
        n++;
    }
    return n;
}

void bt_hfp_set_call_callback(bt_call_callback_t cb, void *user_data)
{
    g_hfp.call_cb = cb;
    g_hfp.call_cb_data = user_data;
}

static const char *vcard_get_property_and_value(char *line, char *prop_name, size_t prop_size)
{
    char *separator = strpbrk(line, ":;");
    if (!separator)
        return NULL;

    size_t name_len = separator - line;
    if (name_len >= prop_size)
        name_len = prop_size - 1;

    strncpy(prop_name, line, name_len);
    prop_name[name_len] = '\0';

    for (size_t i = 0; i < name_len; i++)
        prop_name[i] = toupper(prop_name[i]);

    const char *colon = strchr(line, ':');
    if (!colon)
        return NULL;

    return colon + 1;
}

static void vcard_format_n_name(const char *n_value, char *out, size_t out_size)
{
    char parts[5][128] = {{0}};
    const char *p = n_value;

    for (int i = 0; i < 5 && p; i++)
    {
        const char *semicolon = strchr(p, ';');
        if (semicolon)
        {
            size_t len = semicolon - p;
            if (len >= sizeof(parts[i]))
                len = sizeof(parts[i]) - 1;
            strncpy(parts[i], p, len);
            parts[i][len] = '\0';
            p = semicolon + 1;
        }
        else
        {
            safe_strncpy(parts[i], p, sizeof(parts[i]));
            p = NULL;
        }
    }

    if (parts[1][0] && parts[0][0])
        snprintf(out, out_size, "%s %s", parts[1], parts[0]);
    else if (parts[0][0])
        safe_strncpy(out, parts[0], out_size);
    else if (parts[1][0])
        safe_strncpy(out, parts[1], out_size);
}

static int parse_vcard_file(const char *filepath, bt_contact_t *out,
                            size_t max_contacts)
{
    FILE *f = fopen(filepath, "r");
    if (!f)
    {
        hfp_set_error("Could not open phonebook file: %s", filepath);
        return -1;
    }

    char line[1024];
    bt_contact_t current = {0};
    bool in_card = false;
    size_t count = 0;

    while (fgets(line, sizeof(line), f) && count < max_contacts)
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, "BEGIN:VCARD", 11) == 0)
        {
            memset(&current, 0, sizeof(current));
            in_card = true;
            continue;
        }
        if (strncmp(line, "END:VCARD", 9) == 0)
        {
            if (in_card && (current.name[0] || current.number[0]))
            {
                out[count++] = current;
            }
            in_card = false;
            continue;
        }
        if (!in_card)
            continue;

        char prop_name[64] = {0};
        const char *value = vcard_get_property_and_value(line, prop_name, sizeof(prop_name));

        if (!value || !prop_name[0])
            continue;

        if (strcmp(prop_name, "FN") == 0)
        {
            safe_strncpy(current.name, value, sizeof(current.name));
        }
        else if (strcmp(prop_name, "N") == 0 && !current.name[0])
        {
            vcard_format_n_name(value, current.name, sizeof(current.name));
        }
        else if (strcmp(prop_name, "TEL") == 0 && !current.number[0])
        {
            safe_strncpy(current.number, value, sizeof(current.number));
        }
    }
    fclose(f);
    return (int)count;
}

int bt_hfp_fetch_call_history(const char *device_path, bt_call_info_t *out_calls,
                               size_t max_calls)
{
    if (!device_path || !device_path[0] || !out_calls || max_calls == 0)
    {
        hfp_set_error("bt_hfp_fetch_call_history: invalid arguments");
        return -1;
    }
    if (!g_hfp.obex_bus)
    {
        hfp_set_error("bt_hfp_fetch_call_history: OBEX not available");
        return -1;
    }

    char address[32] = {0};
    const char *dev_prefix = "/dev_";
    const char *dev_pos = strstr(device_path, dev_prefix);
    if (dev_pos)
    {
        const char *mac = dev_pos + strlen(dev_prefix);
        int j = 0;
        for (int i = 0; mac[i] && j < 17; i++)
        {
            if (mac[i] == '_') address[j++] = ':';
            else if ((mac[i] >= '0' && mac[i] <= '9') ||
                     (mac[i] >= 'a' && mac[i] <= 'f') ||
                     (mac[i] >= 'A' && mac[i] <= 'F'))
                address[j++] = mac[i];
        }
        address[j] = '\0';
    }
    if (!address[0] || strlen(address) != 17)
    {
        hfp_set_error("Could not extract address from path: %s", device_path);
        return -1;
    }

    DBusMessage *msg = dbus_message_new_method_call(
        OBEX_BUS_NAME, "/org/bluez/obex", OBEX_CLIENT_IFACE, "CreateSession");
    if (!msg) return -1;

    DBusMessageIter iter, dict, entry, var;
    dbus_message_iter_init_append(msg, &iter);
    const char *addr_ptr = address;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &addr_ptr);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
        DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING DBUS_TYPE_STRING_AS_STRING
        DBUS_TYPE_VARIANT_AS_STRING DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);
    const char *target_key = "Target";
    const char *target_val = "PBAP";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &target_key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &target_val);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&iter, &dict);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.obex_bus, msg, 15000, &err);
    dbus_message_unref(msg);
    if (!reply) { if(dbus_error_is_set(&err)) dbus_error_free(&err); return -1; }

    char session_path[256] = {0};
    {
        DBusMessageIter si;
        dbus_message_iter_init(reply, &si);
        const char *sp = NULL;
        if (dbus_message_iter_get_arg_type(&si) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&si, &sp);
        if (sp) safe_strncpy(session_path, sp, sizeof(session_path));
    }
    dbus_message_unref(reply);
    if (!session_path[0]) return -1;

    int result = -1;
    const char *phonebooks[] = {"cch", "ich", "och", "mch", NULL};

    for (int pb_idx = 0; phonebooks[pb_idx] != NULL && result < 0; pb_idx++)
    {
        printf("[BT HFP] Trying phonebook: %s\n", phonebooks[pb_idx]);
        fflush(stdout);

        {
            DBusMessage *sel = dbus_message_new_method_call(
                OBEX_BUS_NAME, session_path, OBEX_PBAP_IFACE, "Select");
            if (sel)
            {
                const char *loc = "int";
                dbus_message_append_args(sel, DBUS_TYPE_STRING, &loc,
                                         DBUS_TYPE_STRING, &phonebooks[pb_idx], DBUS_TYPE_INVALID);
                dbus_error_init(&err);
                DBusMessage *sel_reply = dbus_connection_send_with_reply_and_block(
                    g_hfp.obex_bus, sel, 10000, &err);
                dbus_message_unref(sel);
                if (sel_reply) dbus_message_unref(sel_reply);
                if (dbus_error_is_set(&err)) dbus_error_free(&err);
            }
        }

        {
            DBusMessage *pull = dbus_message_new_method_call(
                OBEX_BUS_NAME, session_path, OBEX_PBAP_IFACE, "PullAll");
            if (pull)
            {
                DBusMessageIter pi, pd;
                dbus_message_iter_init_append(pull, &pi);
                const char *tf = "";
                dbus_message_iter_append_basic(&pi, DBUS_TYPE_STRING, &tf);
                dbus_message_iter_open_container(&pi, DBUS_TYPE_ARRAY,
                    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING DBUS_TYPE_STRING_AS_STRING
                    DBUS_TYPE_VARIANT_AS_STRING DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &pd);
                dbus_message_iter_close_container(&pi, &pd);

                dbus_error_init(&err);
                DBusMessage *pull_reply = dbus_connection_send_with_reply_and_block(
                    g_hfp.obex_bus, pull, 15000, &err);
                dbus_message_unref(pull);

                char filename[512] = {0};
                if (pull_reply)
                {
                    DBusMessageIter pli;
                    dbus_message_iter_init(pull_reply, &pli);

                    const char *tp = NULL;
                    char tpb[256] = {0};
                    if (dbus_message_iter_get_arg_type(&pli) == DBUS_TYPE_OBJECT_PATH)
                    {
                        dbus_message_iter_get_basic(&pli, &tp);
                        if (tp) safe_strncpy(tpb, tp, sizeof(tpb));
                    }
                    dbus_message_iter_next(&pli);

                    if (dbus_message_iter_get_arg_type(&pli) == DBUS_TYPE_ARRAY)
                    {
                        DBusMessageIter props;
                        dbus_message_iter_recurse(&pli, &props);
                        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY)
                        {
                            DBusMessageIter ei, vi;
                            dbus_message_iter_recurse(&props, &ei);
                            const char *key = NULL;
                            if (dbus_message_iter_get_arg_type(&ei) == DBUS_TYPE_STRING)
                                dbus_message_iter_get_basic(&ei, &key);
                            dbus_message_iter_next(&ei);
                            if (key && dbus_message_iter_get_arg_type(&ei) == DBUS_TYPE_VARIANT)
                            {
                                dbus_message_iter_recurse(&ei, &vi);
                                if (strcmp(key, "Filename") == 0 &&
                                    dbus_message_iter_get_arg_type(&vi) == DBUS_TYPE_STRING)
                                {
                                    const char *fn = NULL;
                                    dbus_message_iter_get_basic(&vi, &fn);
                                    if (fn) safe_strncpy(filename, fn, sizeof(filename));
                                }
                            }
                            dbus_message_iter_next(&props);
                        }
                    }
                    dbus_message_unref(pull_reply);

                    if (tpb[0] && !filename[0])
                    {
                        for (int i = 0; i < 100; i++)
                        {
                            usleep(200000);
                            if (hfp_get_prop_string(g_hfp.obex_bus, OBEX_BUS_NAME,
                                tpb, OBEX_TRANSFER_IFACE, "Filename", filename, sizeof(filename))
                                && filename[0]) break;
                        }
                    }

                    if (filename[0])
                    {
                        printf("[BT HFP] Call history file: %s\n", filename);
                        fflush(stdout);

                        FILE *f = fopen(filename, "r");
                        if (f)
                        {
                            char line[2048];
                            bt_call_info_t current = {0};
                            bool in_card = false;
                            size_t count = 0;

                            while (fgets(line, sizeof(line), f) && count < max_calls)
                            {
                                size_t len = strlen(line);
                                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                                    line[--len] = '\0';

                                if (strncmp(line, "BEGIN:VCARD", 11) == 0)
                                {
                                    memset(&current, 0, sizeof(current));
                                    in_card = true;
                                }
                                else if (strncmp(line, "END:VCARD", 9) == 0)
                                {
                                    if (in_card && current.line_id[0])
                                    {
                                        out_calls[count++] = current;
                                    }
                                    in_card = false;
                                }
                                else if (in_card)
                                {
                                    if (strncmp(line, "TEL;", 4) == 0 || strncmp(line, "TEL:", 4) == 0)
                                    {
                                        const char *colon = strrchr(line, ':');
                                        if (colon && strlen(colon+1) > 0)
                                            safe_strncpy(current.line_id, colon+1, sizeof(current.line_id));
                                    }
                                    else if (strncmp(line, "FN:", 3) == 0)
                                    {
                                        safe_strncpy(current.name, line + 3, sizeof(current.name));
                                    }
                                    else if (strncmp(line, "N:", 2) == 0)
                                    {
                                        if (!current.name[0])
                                        {
                                            char *semi = strchr(line + 2, ';');
                                            if (semi && strlen(semi+1) > 0)
                                                safe_strncpy(current.name, semi+1, sizeof(current.name));
                                            else
                                                safe_strncpy(current.name, line + 2, sizeof(current.name));
                                        }
                                    }
                                }
                            }
                            fclose(f);
                            unlink(filename);
                            result = (int)count;
                            printf("[BT HFP] Parsed %d call history entries from %s\n", (int)count, phonebooks[pb_idx]);
                            fflush(stdout);
                        }
                    }
                }
                if (dbus_error_is_set(&err)) dbus_error_free(&err);
            }
        }
    }

    {
        DBusMessage *rm = dbus_message_new_method_call(
            OBEX_BUS_NAME, "/org/bluez/obex", OBEX_CLIENT_IFACE, "RemoveSession");
        if (rm)
        {
            DBusMessageIter ri;
            dbus_message_iter_init_append(rm, &ri);
            const char *sp = session_path;
            dbus_message_iter_append_basic(&ri, DBUS_TYPE_OBJECT_PATH, &sp);
            dbus_error_init(&err);
            DBusMessage *rmr = dbus_connection_send_with_reply_and_block(
                g_hfp.obex_bus, rm, 5000, &err);
            if (rmr) dbus_message_unref(rmr);
            if (dbus_error_is_set(&err)) dbus_error_free(&err);
            dbus_message_unref(rm);
        }
    }

    return result;
}

int bt_hfp_fetch_contacts(const char *device_path, bt_contact_t *out_contacts,
                          size_t max_contacts)
{
    printf("[BT HFP] fetch_contacts called with path: %s\n", device_path ? device_path : "NULL");
    fflush(stdout);

    if (!device_path || !device_path[0] || !out_contacts || max_contacts == 0)
    {
        printf("[BT HFP] Invalid arguments\n");
        fflush(stdout);
        hfp_set_error("bt_hfp_fetch_contacts: invalid arguments");
        return -1;
    }
    if (!g_hfp.obex_bus)
    {
        printf("[BT HFP] No OBEX bus\n");
        fflush(stdout);
        hfp_set_error("bt_hfp_fetch_contacts: OBEX not available");
        return -1;
    }

    char address[32] = {0};
    const char *dev_prefix = "/dev_";
    const char *dev_pos = strstr(device_path, dev_prefix);

    printf("[BT HFP] dev_pos: %s\n", dev_pos ? dev_pos : "NULL");
    fflush(stdout);

    if (dev_pos)
    {
        const char *mac = dev_pos + strlen(dev_prefix);
        int j = 0;
        for (int i = 0; mac[i] && j < 17; i++)
        {
            if (mac[i] == '_')
                address[j++] = ':';
            else if ((mac[i] >= '0' && mac[i] <= '9') ||
                     (mac[i] >= 'a' && mac[i] <= 'f') ||
                     (mac[i] >= 'A' && mac[i] <= 'F'))
                address[j++] = mac[i];
        }
        address[j] = '\0';
    }

    printf("[BT HFP] Address: '%s' len=%zu\n", address, strlen(address));
    fflush(stdout);

    if (!address[0] || strlen(address) != 17)
    {
        hfp_set_error("Could not extract address from path: %s", device_path);
        return -1;
    }
    printf("[BT HFP] Creating OBEX session...\n");
    fflush(stdout);

    DBusMessage *msg = dbus_message_new_method_call(
        OBEX_BUS_NAME, "/org/bluez/obex", OBEX_CLIENT_IFACE, "CreateSession");
    if (!msg)
    {
        printf("[BT HFP] Failed to create message\n");
        fflush(stdout);
        return -1;
    }

    DBusMessageIter iter, dict, entry, var;
    dbus_message_iter_init_append(msg, &iter);

    const char *addr_ptr = address;
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &addr_ptr);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                         DBUS_TYPE_STRING_AS_STRING
                                             DBUS_TYPE_VARIANT_AS_STRING
                                                 DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &dict);
    const char *target_key = "Target";
    const char *target_val = "PBAP";
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &target_key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                     DBUS_TYPE_STRING_AS_STRING, &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &target_val);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&iter, &dict);

    printf("[BT HFP] Sending CreateSession...\n");
    fflush(stdout);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        g_hfp.obex_bus, msg, 15000, &err);
    dbus_message_unref(msg);

    printf("[BT HFP] CreateSession returned: reply=%p\n", (void *)reply);
    fflush(stdout);

    if (!reply)
    {
        printf("[BT HFP] No reply from CreateSession: %s\n",
               dbus_error_is_set(&err) ? err.message : "no reply/timeout");
        fflush(stdout);
        if (dbus_error_is_set(&err))
        {
            hfp_set_error("CreateSession failed: %s", err.message);
            dbus_error_free(&err);
        }
        else
        {
            hfp_set_error("CreateSession failed: no reply");
        }
        return -1;
    }

    int msg_type = dbus_message_get_type(reply);
    printf("[BT HFP] Reply type: %d\n", msg_type);
    fflush(stdout);

    if (msg_type == DBUS_MESSAGE_TYPE_ERROR)
    {
        const char *err_name = dbus_message_get_error_name(reply);
        printf("[BT HFP] Error name: %s\n", err_name ? err_name : "NULL");
        fflush(stdout);

        if (dbus_error_is_set(&err))
        {
            printf("[BT HFP] DBus error: %s\n", err.message);
            fflush(stdout);
            hfp_set_error("CreateSession: %s", err.message);
            dbus_error_free(&err);
        }
        else
        {
            hfp_set_error("CreateSession error: %s", err_name ? err_name : "unknown");
        }
        dbus_message_unref(reply);
        return -1;
    }

    if (msg_type != DBUS_MESSAGE_TYPE_METHOD_RETURN)
    {
        printf("[BT HFP] Unexpected reply type: %d\n", msg_type);
        fflush(stdout);
        dbus_message_unref(reply);
        return -1;
    }

    char session_path[256] = {0};
    {
        DBusMessageIter session_iter;
        dbus_message_iter_init(reply, &session_iter);
        const char *sp = NULL;
        if (dbus_message_iter_get_arg_type(&session_iter) == DBUS_TYPE_OBJECT_PATH)
            dbus_message_iter_get_basic(&session_iter, &sp);
        if (sp)
            safe_strncpy(session_path, sp, sizeof(session_path));
    }
    dbus_message_unref(reply);
    reply = NULL;

    if (!session_path[0])
    {
        hfp_set_error("CreateSession: reply had no session object path");
        return -1;
    }
    printf("[BT HFP] Session path: %s\n", session_path);
    fflush(stdout);

    int result = -1;

    {
        DBusMessage *sel_msg = dbus_message_new_method_call(
            OBEX_BUS_NAME, session_path, OBEX_PBAP_IFACE, "Select");
        if (!sel_msg)
        {
            hfp_set_error("bt_hfp_fetch_contacts: failed to allocate Select message");
            goto cleanup_session;
        }
        const char *loc = "int";
        const char *phonebook = "pb";
        dbus_message_append_args(sel_msg,
                                 DBUS_TYPE_STRING, &loc,
                                 DBUS_TYPE_STRING, &phonebook,
                                 DBUS_TYPE_INVALID);

        DBusError sel_err;
        dbus_error_init(&sel_err);
        DBusMessage *sel_reply = dbus_connection_send_with_reply_and_block(
            g_hfp.obex_bus, sel_msg, 10000, &sel_err);
        dbus_message_unref(sel_msg);

        printf("[BT HFP] Select returned: reply=%p\n", (void *)sel_reply);
        fflush(stdout);

        if (!sel_reply)
        {
            hfp_set_error("Select(int,pb) failed: %s",
                          dbus_error_is_set(&sel_err) ? sel_err.message : "no reply");
            if (dbus_error_is_set(&sel_err))
                dbus_error_free(&sel_err);
            goto cleanup_session;
        }
        dbus_message_unref(sel_reply);
    }

    {
        DBusMessage *pull_msg = dbus_message_new_method_call(
            OBEX_BUS_NAME, session_path, OBEX_PBAP_IFACE, "PullAll");
        if (!pull_msg)
        {
            hfp_set_error("bt_hfp_fetch_contacts: failed to allocate PullAll message");
            goto cleanup_session;
        }

        DBusMessageIter pull_append_iter, pull_filters_dict;
        dbus_message_iter_init_append(pull_msg, &pull_append_iter);
        const char *target_file = "";
        dbus_message_iter_append_basic(&pull_append_iter, DBUS_TYPE_STRING, &target_file);
        dbus_message_iter_open_container(&pull_append_iter, DBUS_TYPE_ARRAY,
                                         DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                             DBUS_TYPE_STRING_AS_STRING
                                                 DBUS_TYPE_VARIANT_AS_STRING
                                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                         &pull_filters_dict);
        dbus_message_iter_close_container(&pull_append_iter, &pull_filters_dict);

        DBusError pull_err;
        dbus_error_init(&pull_err);
        DBusMessage *pull_reply = dbus_connection_send_with_reply_and_block(
            g_hfp.obex_bus, pull_msg, 15000, &pull_err);
        dbus_message_unref(pull_msg);

        printf("[BT HFP] PullAll returned: reply=%p\n", (void *)pull_reply);
        fflush(stdout);

        if (!pull_reply)
        {
            const char *pull_err_name = dbus_error_is_set(&pull_err) ? pull_err.name : NULL;
            const char *pull_err_msg = dbus_error_is_set(&pull_err) ? pull_err.message : "no reply/timeout";
            printf("[BT HFP] PullAll failed: name=%s msg=%s\n",
                   pull_err_name ? pull_err_name : "NULL", pull_err_msg);
            fflush(stdout);
            hfp_set_error("PullAll failed: %s", pull_err_msg);
            if (dbus_error_is_set(&pull_err))
                dbus_error_free(&pull_err);
            goto cleanup_session;
        }

        DBusMessageIter pull_iter;
        dbus_message_iter_init(pull_reply, &pull_iter);

        const char *transfer_path = NULL;
        char transfer_path_buf[256] = {0};
        char filename[512] = {0};
        bool transfer_complete = false;
        bool transfer_error = false;

        if (dbus_message_iter_get_arg_type(&pull_iter) == DBUS_TYPE_OBJECT_PATH)
        {
            dbus_message_iter_get_basic(&pull_iter, &transfer_path);
            if (transfer_path)
                safe_strncpy(transfer_path_buf, transfer_path, sizeof(transfer_path_buf));
        }
        dbus_message_iter_next(&pull_iter);

        if (dbus_message_iter_get_arg_type(&pull_iter) == DBUS_TYPE_ARRAY)
        {
            DBusMessageIter props_iter;
            dbus_message_iter_recurse(&pull_iter, &props_iter);

            while (dbus_message_iter_get_arg_type(&props_iter) == DBUS_TYPE_DICT_ENTRY)
            {
                DBusMessageIter entry_iter, var_iter;
                dbus_message_iter_recurse(&props_iter, &entry_iter);

                const char *key = NULL;
                if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&entry_iter, &key);
                dbus_message_iter_next(&entry_iter);

                if (key && dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_VARIANT)
                {
                    dbus_message_iter_recurse(&entry_iter, &var_iter);

                    if (strcmp(key, "Status") == 0 &&
                        dbus_message_iter_get_arg_type(&var_iter) == DBUS_TYPE_STRING)
                    {
                        const char *status = NULL;
                        dbus_message_iter_get_basic(&var_iter, &status);
                        if (status)
                        {
                            printf("[BT HFP] PullAll status: %s\n", status);
                            if (strcmp(status, "complete") == 0)
                                transfer_complete = true;
                            else if (strcmp(status, "error") == 0)
                                transfer_error = true;
                        }
                    }
                    else if (strcmp(key, "Filename") == 0 &&
                             dbus_message_iter_get_arg_type(&var_iter) == DBUS_TYPE_STRING)
                    {
                        const char *fn = NULL;
                        dbus_message_iter_get_basic(&var_iter, &fn);
                        if (fn)
                            safe_strncpy(filename, fn, sizeof(filename));
                    }
                }
                dbus_message_iter_next(&props_iter);
            }
        }
        dbus_message_unref(pull_reply);

        if (transfer_error)
        {
            hfp_set_error("PBAP transfer failed (Status=error)");
            goto cleanup_session;
        }

        if (!transfer_complete || !filename[0])
        {

            if (!transfer_path_buf[0])
            {
                hfp_set_error("PullAll: no transfer object or filename");
                goto cleanup_session;
            }

            char status[32] = {0};
            bool transfer_ok = false;
            bool transfer_explicit_error = false;
            const int poll_budget_ms = 20000;
            const int poll_step_ms = 100;
            int waited_ms = 0;

            while (waited_ms < poll_budget_ms)
            {
                status[0] = '\0';

                bool got_status = false;
                const char *status_err_name = NULL;
                char status_err_msg[256] = {0};
                {
                    DBusMessage *st_msg = dbus_message_new_method_call(
                        OBEX_BUS_NAME, transfer_path_buf, DBUS_PROPS_IFACE, "Get");
                    if (st_msg)
                    {
                        const char *st_iface = OBEX_TRANSFER_IFACE;
                        const char *st_prop = "Status";
                        dbus_message_append_args(st_msg,
                                                 DBUS_TYPE_STRING, &st_iface,
                                                 DBUS_TYPE_STRING, &st_prop,
                                                 DBUS_TYPE_INVALID);
                        DBusError st_err;
                        dbus_error_init(&st_err);
                        DBusMessage *st_reply = dbus_connection_send_with_reply_and_block(
                            g_hfp.obex_bus, st_msg, 3000, &st_err);
                        dbus_message_unref(st_msg);

                        if (!st_reply)
                        {
                            if (dbus_error_is_set(&st_err))
                            {
                                status_err_name = dbus_error_has_name(&st_err, DBUS_ERROR_UNKNOWN_OBJECT)
                                                      ? "UnknownObject"
                                                      : st_err.name;
                                safe_strncpy(status_err_msg, st_err.message, sizeof(status_err_msg));
                                dbus_error_free(&st_err);
                            }
                        }
                        else
                        {
                            DBusMessageIter st_iter, st_var;
                            dbus_message_iter_init(st_reply, &st_iter);
                            if (dbus_message_iter_get_arg_type(&st_iter) == DBUS_TYPE_VARIANT)
                            {
                                dbus_message_iter_recurse(&st_iter, &st_var);
                                if (dbus_message_iter_get_arg_type(&st_var) == DBUS_TYPE_STRING)
                                {
                                    const char *sv = NULL;
                                    dbus_message_iter_get_basic(&st_var, &sv);
                                    if (sv)
                                    {
                                        safe_strncpy(status, sv, sizeof(status));
                                        got_status = true;
                                    }
                                }
                            }
                            dbus_message_unref(st_reply);
                        }
                    }
                }

                if (waited_ms == 0 || (waited_ms % 1000) == 0)
                {
                    if (got_status)
                    {
                        printf("[BT HFP] Transfer poll @%dms: status='%s'\n", waited_ms, status);
                    }
                    else
                    {
                        printf("[BT HFP] Transfer poll @%dms: Get failed, name=%s msg=%s\n",
                               waited_ms,
                               status_err_name ? status_err_name : "NULL",
                               status_err_msg[0] ? status_err_msg : "(none)");
                    }
                    fflush(stdout);
                }

                if (got_status)
                {
                    if (strcmp(status, "complete") == 0)
                    {
                        transfer_ok = true;
                        break;
                    }
                    if (strcmp(status, "error") == 0)
                    {
                        transfer_explicit_error = true;
                        hfp_set_error("PBAP transfer failed (Status=error)");
                        break;
                    }
                }
                else if (status_err_name && strcmp(status_err_name, "UnknownObject") == 0)
                {

                    transfer_ok = true;
                    break;
                }

                usleep(poll_step_ms * 1000);
                waited_ms += poll_step_ms;
            }

            if (!transfer_ok)
            {
                if (!transfer_explicit_error)
                    hfp_set_error("PBAP transfer timed out after %d ms (last status: %s)",
                                  waited_ms, status[0] ? status : "unknown");
                goto cleanup_session;
            }

            if (!filename[0] && transfer_path_buf[0])
            {
                if (!hfp_get_prop_string(g_hfp.obex_bus, OBEX_BUS_NAME,
                                         transfer_path_buf, OBEX_TRANSFER_IFACE,
                                         "Filename", filename, sizeof(filename)) ||
                    !filename[0])
                {
                    hfp_set_error("PBAP transfer completed but Filename property unavailable");
                    goto cleanup_session;
                }
            }
        }

        if (!filename[0])
        {
            hfp_set_error("No filename from PBAP transfer");
            goto cleanup_session;
        }

        printf("[BT HFP] Transfer complete, file: %s\n", filename);
        fflush(stdout);

        int n = parse_vcard_file(filename, out_contacts, max_contacts);

        unlink(filename);

        if (n < 0)
        {

            goto cleanup_session;
        }

        printf("[BT HFP] Parsed %d contacts\n", n);
        fflush(stdout);
        result = n;
    }

cleanup_session:
{
    DBusMessage *rm_msg = dbus_message_new_method_call(
        OBEX_BUS_NAME, "/org/bluez/obex", OBEX_CLIENT_IFACE, "RemoveSession");
    if (rm_msg)
    {
        DBusMessageIter rm_iter;
        dbus_message_iter_init_append(rm_msg, &rm_iter);
        const char *sp_ptr = session_path;
        dbus_message_iter_append_basic(&rm_iter, DBUS_TYPE_OBJECT_PATH, &sp_ptr);

        DBusError rm_err;
        dbus_error_init(&rm_err);
        DBusMessage *rm_reply = dbus_connection_send_with_reply_and_block(
            g_hfp.obex_bus, rm_msg, 5000, &rm_err);
        dbus_message_unref(rm_msg);
        if (rm_reply)
            dbus_message_unref(rm_reply);
        else if (dbus_error_is_set(&rm_err))
        {
            printf("[BT HFP] RemoveSession warning: %s\n", rm_err.message);
            fflush(stdout);
            dbus_error_free(&rm_err);
        }
    }
}

    return result;
}