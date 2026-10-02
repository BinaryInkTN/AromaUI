#ifndef CONTACTS_BT_SERVICE_H
#define CONTACTS_BT_SERVICE_H
















#include <stdbool.h>
#include <stddef.h>

#include "bt_speaker_hfp.h"



#include "bt_speaker_api.h"

typedef struct
{



    int (*set_enabled)(bool on);
    bool (*is_enabled)(void);




    bool (*connected)(void);
    void (*device_info)(bt_device_info_t *out);

    int (*dial)(const char *number);
    int (*answer)(const char *call_path);
    int (*hangup)(const char *call_path);
    int (*hangup_all)(void);
    int (*active_calls)(bt_call_info_t *out_calls, size_t max_calls);
} ContactsBtService;


const ContactsBtService *contacts_bt_service(void);

#endif
