#ifndef MEDIA_BT_SERVICE_H
#define MEDIA_BT_SERVICE_H












#include <stdbool.h>

#include "bt_speaker_api.h"

typedef struct
{




    int (*set_enabled)(bool on, const char *device_name);
    bool (*is_enabled)(void);

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


const MediaBtService *media_bt_service(void);

#endif
