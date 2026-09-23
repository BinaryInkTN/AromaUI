#ifndef MEDIA_BT_SERVICE_H
#define MEDIA_BT_SERVICE_H

/* Media Bluetooth service (owned by the com.aroma.media package).
 *
 * The A2DP/AVRCP speaker stack (bt_speaker_api.c) compiles into this
 * package's plugin.so: there is exactly one stack in the process, owned
 * here. The host and other first-party code talk to it through this
 * contract only (looked up with package_manager_symbol(com.aroma.media,
 * "media_bt_service")) - never through globals or callbacks. All
 * getters are thread-safe snapshots; transport calls are synchronous
 * BlueZ D-Bus calls that return 0 on success.
 */

#include <stdbool.h>

#include "bt_speaker_api.h"

typedef struct
{
    /* Full lifecycle (mirrors the old host toggle): init + start, or
     * stop + cleanup. Safe to call repeatedly; second enables are no-ops
     * unless the stack was stopped. device_name may be NULL to keep the
     * current name. Returns 0 on success. */
    int (*set_enabled)(bool on, const char *device_name);
    bool (*is_enabled)(void);
    /* Live rename without restart. Returns 0 on success. */
    int (*set_device_name)(const char *name);

    bt_state_t (*state)(void);
    bool (*is_connected)(void);
    void (*device_info)(bt_device_info_t *out);
    void (*media_info)(bt_media_info_t *out);
    void (*stats)(bt_stats_t *out);

    int (*avrcp_play)(void);
    int (*avrcp_pause)(void);
    int (*avrcp_next)(void);
    int (*avrcp_prev)(void);
} MediaBtService;

/* Exported from the media plugin.so. NULL when the package is missing. */
const MediaBtService *media_bt_service(void);

#endif
