#ifndef BLUETOOTH_PHONE_H
#define BLUETOOTH_PHONE_H

#include "aroma.h"
#include "bt_speaker_api.h"
#include "bt_speaker_hfp.h"

#define CONTACTS_RETRY_INTERVAL_SEC 5
#define MAX_CONTACTS_RETRIES 20
#define MIN_EMPTY_RESULT_RETRIES 3
#define CONTACTS_PER_PAGE 7
#define MAX_DIALER_DIGITS 32

void show_incoming_call_screen(const char *name, const char *number, const char *call_path);
void populate_contact_listview(AromaNode *listview);
void attempt_contact_fetch(void);
bool open_phone(AromaNode *node, void *user_data);
void close_phone(void *user_data);
void update_bt_info_card(void);

extern bt_state_t g_bt_state;
extern bt_device_info_t g_bt_device_info;
extern bt_media_info_t g_bt_media_info;
extern bt_stats_t g_bt_stats;
extern bool g_bt_initialized;
extern bool g_bt_connected;
extern pthread_mutex_t g_bt_mutex;
extern int contact_fetch_retries;
extern bool contact_fetch_in_progress;
extern pthread_mutex_t contact_fetch_mutex;
extern char dialer_number[MAX_DIALER_DIGITS];
extern AromaNode *dialer_display_label;
extern AromaNode *dialer_card;
extern int sorted_to_original[100];
extern AromaNode *incoming_call_overlay;
extern AromaNode *incoming_call_name_label;
extern AromaNode *incoming_call_number_label;
extern AromaNode *incoming_call_accept_btn;
extern AromaNode *incoming_call_reject_btn;
extern AromaNode *incoming_call_end_btn;
extern bool call_overlay_visible;
extern char current_call_name[128];
extern char current_call_number[64];
extern char current_call_path[256];
extern pthread_mutex_t call_state_lock;
extern int contact_page;
extern int total_pages;
extern AromaNode *prev_page_btn;
extern AromaNode *next_page_btn;
extern AromaNode *page_label;
extern AromaNode *pagination_card;
extern pthread_mutex_t contact_list_lock;

#endif
