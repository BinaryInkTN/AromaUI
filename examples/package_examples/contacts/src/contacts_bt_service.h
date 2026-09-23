#ifndef CONTACTS_BT_SERVICE_H
#define CONTACTS_BT_SERVICE_H

/* Contacts Bluetooth service (owned by the com.aroma.contacts package).
 *
 * The HFP/PBAP telephony stack (bt_speaker_hfp.c) compiles into this
 * package's plugin.so: there is exactly one HFP stack in the process,
 * owned here. The host (incoming-call overlay, call monitor) talks to
 * it through this contract only (looked up with
 * package_manager_symbol(com.aroma.contacts, "contacts_bt_service")).
 * Callbacks are synchronous BlueZ D-Bus calls returning 0 on success;
 * active_calls is polled (the host call monitor already polls it).
 *
 * Connection/device identity for PBAP fetch comes from the media
 * package's MediaBtService (first-party interop): telephony needs the
 * BlueZ device path of the connected phone, which the A2DP side tracks.
 */

#include <stdbool.h>
#include <stddef.h>

#include "bt_speaker_hfp.h"
/* bt_device_info_t lives in the A2DP header (same connected phone the
 * media side tracks); shared first-party type, not a dependency on the
 * media stack itself. */
#include "bt_speaker_api.h"

typedef struct
{
    /* Full lifecycle: bt_hfp_init / bt_hfp_cleanup. The plugin's own
     * init does NOT start the stack; the host toggle drives it, exactly
     * like the old host-owned code. Safe to call repeatedly. */
    int (*set_enabled)(bool on);
    bool (*is_enabled)(void);

    /* Connection + BlueZ device identity, forwarded from the media
     * package's service (first-party interop): telephony needs the same
     * connected phone the A2DP side tracks. */
    bool (*connected)(void);
    void (*device_info)(bt_device_info_t *out);

    int (*dial)(const char *number);
    int (*answer)(const char *call_path);
    int (*hangup)(const char *call_path);
    int (*hangup_all)(void);
    int (*active_calls)(bt_call_info_t *out_calls, size_t max_calls);
} ContactsBtService;

/* Exported from the contacts plugin.so. NULL when the package is missing. */
const ContactsBtService *contacts_bt_service(void);

#endif
