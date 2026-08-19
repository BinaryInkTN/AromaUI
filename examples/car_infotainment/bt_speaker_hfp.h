#ifndef BT_SPEAKER_HFP_H
#define BT_SPEAKER_HFP_H



#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_CALL_STATE_UNKNOWN = 0,
    BT_CALL_STATE_ACTIVE,
    BT_CALL_STATE_HELD,
    BT_CALL_STATE_DIALING,
    BT_CALL_STATE_ALERTING,
    BT_CALL_STATE_INCOMING,
    BT_CALL_STATE_WAITING,
    BT_CALL_STATE_DISCONNECTED,
} bt_call_state_t;

typedef struct {
    char             path[256];  
    char             line_id[64];
    char             name[128];  
    bt_call_state_t  state;
    bool             multiparty;
} bt_call_info_t;

typedef struct {
    char name[128];
    char number[64];
} bt_contact_t;


typedef void (*bt_call_callback_t)(const bt_call_info_t *call,
                                    bool removed,
                                    void *user_data);


int bt_hfp_init(void);
int bt_hfp_fetch_call_history(const char *device_path, bt_call_info_t *out_calls,
                               size_t max_calls);

void bt_hfp_poll(void);

void bt_hfp_cleanup(void);




int bt_hfp_dial(const char *number);


int bt_hfp_hangup(const char *call_path);


int bt_hfp_hangup_all(void);


int bt_hfp_answer(const char *call_path);


int bt_hfp_get_active_calls(bt_call_info_t *out_calls, size_t max_calls);

void bt_hfp_set_call_callback(bt_call_callback_t cb, void *user_data);


int bt_hfp_fetch_contacts(const char *device_path,
                           bt_contact_t *out_contacts,
                           size_t max_contacts);

const char *bt_hfp_get_last_error_message(void);

#ifdef __cplusplus
}
#endif

#endif