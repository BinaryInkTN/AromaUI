













#include "contacts_bt_service.h"
#include "media_bt_service.h"
#include "package_manager.h"

#include <string.h>

static bool s_hfp_enabled = false;




static const MediaBtService *media_svc(void)
{
    typedef const MediaBtService *(*svc_fn)(void);
    svc_fn fn = (svc_fn)package_manager_symbol("com.aroma.media",
                                               "media_bt_service");
    return fn ? fn() : NULL;
}

static bool contacts_connected(void)
{
    const MediaBtService *m = media_svc();
    return m && m->is_connected();
}

static void contacts_device_info(bt_device_info_t *out)
{
    const MediaBtService *m = media_svc();
    if (!out)
        return;
    if (m)
        m->device_info(out);
    else
        memset(out, 0, sizeof(*out));
}

static int contacts_set_enabled(bool on)
{
    if (on)
    {
        if (s_hfp_enabled)
            return 0;
        if (bt_hfp_init() != 0)
            return -1;
        s_hfp_enabled = true;
        return 0;
    }
    if (s_hfp_enabled)
    {
        bt_hfp_cleanup();
        s_hfp_enabled = false;
    }
    return 0;
}

static bool contacts_is_enabled(void)
{
    return s_hfp_enabled;
}

static const ContactsBtService s_contacts_bt_service = {
    .set_enabled = contacts_set_enabled,
    .is_enabled = contacts_is_enabled,
    .connected = contacts_connected,
    .device_info = contacts_device_info,
    .dial = bt_hfp_dial,
    .answer = bt_hfp_answer,
    .hangup = bt_hfp_hangup,
    .hangup_all = bt_hfp_hangup_all,
    .active_calls = bt_hfp_get_active_calls,
};

const ContactsBtService *contacts_bt_service(void)
{
    return &s_contacts_bt_service;
}
