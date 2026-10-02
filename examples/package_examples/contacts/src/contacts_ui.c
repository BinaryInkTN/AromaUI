













#include "app_state.h"
#include "app_registry.h"
#include "vehicle_view.h"
#include "aroma_animation.h"
#include "apps/media/media_controls.h"
#include "bt_speaker_hfp.h"
#include "contacts_bt_service.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define CONTACTS_PER_PAGE 7
#define MAX_DIALER_DIGITS 32
#define CONTACTS_RETRY_INTERVAL_SEC 5
#define MIN_EMPTY_RESULT_RETRIES 3

void populate_contact_listview(AromaNode *listview);


#define CONTACTS_MAX_STORE 100
typedef struct
{
    char name[128];
    char number[64];
} ContactInfo;

static ContactInfo s_contacts[CONTACTS_MAX_STORE];
static int s_contact_count = 0;
static bool s_contacts_fetched = false;
static pthread_mutex_t s_fetch_mutex = PTHREAD_MUTEX_INITIALIZER;


static const ContactsBtService *bt_svc(void)
{
    return contacts_bt_service();
}


int contact_fetch_retries = 0;
bool contact_fetch_in_progress = false;


pthread_mutex_t contact_list_lock = PTHREAD_MUTEX_INITIALIZER;


char dialer_number[MAX_DIALER_DIGITS] = "";
AromaNode *dialer_display_label = NULL;
AromaNode *dialer_card = NULL;
int sorted_to_original[100];


int contact_page = 0;
int total_pages = 0;
AromaNode *prev_page_btn = NULL;
AromaNode *next_page_btn = NULL;
AromaNode *page_label = NULL;
AromaNode *pagination_card = NULL;


static bool on_dialer_delete_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    size_t len = strlen(dialer_number);
    if (len > 0)
    {
        dialer_number[len - 1] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, dialer_number[0] ? dialer_number : "Enter number");
        }
    }
    return true;
}

static bool on_dialer_call_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (dialer_number[0] != '\0')
    {
        const ContactsBtService *svc = bt_svc();
        if (svc && svc->connected())
        {
            bt_hfp_dial(dialer_number);
        }
        dialer_number[0] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, "Enter number");
        }
    }
    return true;
}

static bool on_dialer_button_click(AromaNode *node, void *user_data)
{
    (void)node;
    const char *digit = (const char *)user_data;
    if (!digit || strlen(dialer_number) >= MAX_DIALER_DIGITS - 1)
        return true;
    strcat(dialer_number, digit);
    if (dialer_display_label)
    {
        aroma_label_set_text(dialer_display_label, dialer_number);
    }
    return true;
}



static void on_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, tab_index != 0);
    }
    if (dialer_card)
    {
        aroma_node_set_hidden(dialer_card, tab_index != 1);
    }
    if (state.contact_listview)
    {
        aroma_node_set_hidden(state.contact_listview, tab_index != 0);
    }
    if (tab_index == 0)
    {
        contact_page = 0;
        populate_contact_listview(state.contact_listview);
    }
}



static int compare_contacts(const void *a, const void *b)
{
    const ContactInfo *ca = (const ContactInfo *)a;
    const ContactInfo *cb = (const ContactInfo *)b;
    char name_a[128], name_b[128];
    if (ca->name[0] == '\0')
    {
        strncpy(name_a, ca->number, sizeof(name_a) - 1);
        name_a[sizeof(name_a) - 1] = '\0';
    }
    else
    {
        strncpy(name_a, ca->name, sizeof(name_a) - 1);
        name_a[sizeof(name_a) - 1] = '\0';
    }
    if (cb->name[0] == '\0')
    {
        strncpy(name_b, cb->number, sizeof(name_b) - 1);
        name_b[sizeof(name_b) - 1] = '\0';
    }
    else
    {
        strncpy(name_b, cb->name, sizeof(name_b) - 1);
        name_b[sizeof(name_b) - 1] = '\0';
    }
    return strcasecmp(name_a, name_b);
}

static char get_first_letter(const char *str)
{
    if (!str || !str[0])
        return '#';
    char c = toupper(str[0]);
    if (c >= 'A' && c <= 'Z')
        return c;
    return '#';
}

void populate_contact_listview(AromaNode *listview)
{
    if (!listview)
        return;
    pthread_mutex_lock(&contact_list_lock);
    aroma_listview_clear(listview);
    if (!s_contacts_fetched)
    {
        const ContactsBtService *svc = bt_svc();
        if (svc && svc->connected())
        {
            aroma_listview_add_item_with_icon(listview, "Loading contacts...", "Please wait", AROMA_ICON_PERSON, NULL);
        }
        else
        {
            aroma_listview_add_item_with_icon(listview, "No phone connected", "Enable Bluetooth and connect a phone", AROMA_ICON_BLUETOOTH_DISABLED, NULL);
        }
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    if (s_contact_count == 0)
    {
        aroma_listview_add_item_with_icon(listview, "No contacts found", "Connect a phone with PBAP or sync contacts", AROMA_ICON_PERSON, NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    ContactInfo *sorted_contacts = malloc(sizeof(ContactInfo) * s_contact_count);
    if (!sorted_contacts)
    {
        aroma_listview_add_item_with_icon(listview, "Memory error", "", AROMA_ICON_PERSON, NULL);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    int *orig_indices = malloc(sizeof(int) * s_contact_count);
    if (!orig_indices)
    {
        free(sorted_contacts);
        aroma_listview_add_item_with_icon(listview, "Memory error", "", AROMA_ICON_PERSON, NULL);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    for (int i = 0; i < s_contact_count; i++)
    {
        orig_indices[i] = i;
    }
    memcpy(sorted_contacts, s_contacts, sizeof(ContactInfo) * s_contact_count);
    qsort(sorted_contacts, s_contact_count, sizeof(ContactInfo), compare_contacts);
    for (int i = 0; i < s_contact_count; i++)
    {
        for (int j = 0; j < s_contact_count; j++)
        {
            if (compare_contacts(&sorted_contacts[i], &s_contacts[j]) == 0 &&
                strcmp(sorted_contacts[i].number, s_contacts[j].number) == 0)
            {
                orig_indices[i] = j;
                break;
            }
        }
    }
    total_pages = (s_contact_count + CONTACTS_PER_PAGE - 1) / CONTACTS_PER_PAGE;
    if (contact_page >= total_pages)
        contact_page = total_pages - 1;
    if (contact_page < 0)
        contact_page = 0;
    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int end_idx = start_idx + CONTACTS_PER_PAGE;
    if (end_idx > s_contact_count)
        end_idx = s_contact_count;
    char current_header = 0;
    for (int i = start_idx; i < end_idx; i++)
    {
        char display_name[256];
        char display_number[64];
        sorted_to_original[i] = orig_indices[i];
        const char *name = sorted_contacts[i].name;
        const char *number = sorted_contacts[i].number;
        char letter = get_first_letter(name[0] ? name : number);
        if (letter != current_header)
        {
            current_header = letter;
            char header_text[4] = {letter, '\0'};
            aroma_listview_add_header(listview, header_text);
        }
        if (name[0] == '\0')
        {
            snprintf(display_name, sizeof(display_name), "%s", number);
            display_number[0] = '\0';
        }
        else
        {
            snprintf(display_name, sizeof(display_name), "%s", name);
            snprintf(display_number, sizeof(display_number), "%s", number);
        }
        aroma_listview_add_item_with_icon(listview, display_name, display_number, AROMA_ICON_PERSON, NULL);
    }
    free(sorted_contacts);
    free(orig_indices);
    if (prev_page_btn)
    {
        aroma_node_set_hidden(prev_page_btn, contact_page == 0);
    }
    if (next_page_btn)
    {
        aroma_node_set_hidden(next_page_btn, contact_page >= total_pages - 1);
    }
    if (page_label)
    {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "%d/%d", contact_page + 1, total_pages);
        aroma_label_set_text(page_label, label_text);
    }
    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, total_pages <= 1);
    }
    pthread_mutex_unlock(&contact_list_lock);
}

static void on_prev_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page > 0)
    {
        contact_page--;
        populate_contact_listview(state.contact_listview);
    }
}

static void on_next_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page < total_pages - 1)
    {
        contact_page++;
        populate_contact_listview(state.contact_listview);
    }
}



static void on_contact_click(int index, void *user_data)
{
    (void)user_data;
    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int actual_index = start_idx + index;
    if (actual_index >= 0 && actual_index < s_contact_count)
    {
        int orig_idx = sorted_to_original[actual_index];
        if (orig_idx >= 0 && orig_idx < s_contact_count)
        {
            char number[64];
            strncpy(number, s_contacts[orig_idx].number, sizeof(number) - 1);
            number[sizeof(number) - 1] = '\0';
            if (number[0] != '\0')
            {
                const ContactsBtService *svc = bt_svc();
                if (svc && svc->connected())
                {
                    bt_hfp_dial(number);
                }
            }
        }
    }
}






void phone_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;




    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;

    int start_y = WIN_H;
    int end_y = 0;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    aroma_node_invalidate(target);
}

bool open_phone(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, phone_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
    aroma_node_set_hidden(state.phone_node, false);
    aroma_node_set_hidden(state.phone_close_btn, false);
    aroma_node_set_hidden(state.phone_app_tabs, false);
    aroma_node_set_hidden(state.contact_listview, false);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, false);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    contact_page = 0;

    populate_contact_listview(state.contact_listview);

    return true;
}

void phone_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;

    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;

    int start_y = 0;
    int end_y = WIN_H;

    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;

    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.phone_close_btn, true);
        aroma_node_set_hidden(state.phone_node, true);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(state.phone_node);
    aroma_node_invalidate(target);
}

void close_phone(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, phone_closing_anim, NULL);
    aroma_node_set_hidden(state.phone_app_tabs, true);
    aroma_node_set_hidden(state.contact_listview, true);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, true);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}



static void contacts_fetch_data_only(void);



void attempt_contact_fetch(void)
{
    contacts_fetch_data_only();
}



void build_phone_app_ui(AromaNode *parent)
{
    if (!parent || state.phone_node) return;

    state.phone_node = aroma_ui_container(
        parent, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.phone_node, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(state.phone_node, true);

    state.phone_close_btn = aroma_ui_iconbutton(
        state.phone_node, AROMA_ICON_CLOSE,
        WIN_W - 60, 20, 40, ICON_BUTTON_FILLED,
        close_phone, parent, state.icon_font);
    aroma_node_set_z_index(state.phone_close_btn, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(state.phone_close_btn, true);

    const char *labels[] = {"Contacts", "Dialer"};


    state.phone_app_tabs = aroma_tabs_create(
        state.phone_node, 0, 60, WIN_W, 50,
        labels, 2);
    aroma_node_set_z_index(state.phone_app_tabs, Z_LAYER_STATUS_BAR + 12);
    aroma_tabs_set_font(state.phone_app_tabs, state.ui_font);
    aroma_tabs_set_on_change(state.phone_app_tabs, on_tab_changed, NULL);
    aroma_tabs_setup_events(state.phone_app_tabs, aroma_ui_request_redraw, NULL);


    state.contact_listview = aroma_listview_create(
        state.phone_app_tabs, 20, 90, WIN_W - 40, WIN_H - 200);
    aroma_node_set_z_index(state.contact_listview, Z_LAYER_STATUS_BAR + 13);
    aroma_listview_set_font(state.contact_listview, state.ui_font);
    aroma_listview_set_icon_font(state.contact_listview, state.icon_font);
    aroma_listview_set_callback(state.contact_listview, on_contact_click, NULL);

    pagination_card = aroma_ui_card(
        state.phone_app_tabs, 20, WIN_H - 120, WIN_W - 40, 60, CARD_TYPE_GLASS);
    aroma_node_set_z_index(pagination_card, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(pagination_card, true);

    prev_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_BACK,
        16, 8, 34, ICON_BUTTON_OUTLINED,
        on_prev_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(prev_page_btn, Z_LAYER_STATUS_BAR + 14);

    page_label = aroma_ui_label(
        pagination_card, "1/1",
        70, 12, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(page_label, Z_LAYER_STATUS_BAR + 14);

    next_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_FORWARD,
        130, 8, 34, ICON_BUTTON_OUTLINED,
        on_next_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(next_page_btn, Z_LAYER_STATUS_BAR + 14);


    dialer_card = aroma_ui_card(
        state.phone_app_tabs, 20, 90, WIN_W - 40, WIN_H - 160, CARD_TYPE_GLASS);
    aroma_node_set_z_index(dialer_card, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(dialer_card, true);

    dialer_display_label = aroma_ui_label(
        dialer_card, "Enter number", 430, 20,
        LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(dialer_display_label, Z_LAYER_STATUS_BAR + 14);



    AromaNode *dialer_grid = aroma_ui_container(
        dialer_card, (984 - 280) / 2, 70, 280, 330,
        AROMA_LAYOUT_MODE_GRID, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(dialer_grid, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_grid_cols(dialer_grid, 3);
    aroma_node_set_grid_rows(dialer_grid, 5);
    aroma_node_set_gap(dialer_grid, 12);

    {
        const int btn_size = 72;
        const char *digits[] = {"1", "2", "3", "4", "5", "6",
                                "7", "8", "9", "*", "0", "#"};
        for (int i = 0; i < 12; i++) {
            AromaNode *btn = aroma_ui_button(
                dialer_grid, digits[i],
                0, 0, btn_size, btn_size,
                on_dialer_button_click, (void *)digits[i], state.settings_font);
            aroma_node_set_z_index(btn, Z_LAYER_STATUS_BAR + 15);
        }
        AromaNode *del_btn = aroma_ui_button(
            dialer_grid, AROMA_ICON_BACKSPACE,
            0, 0, btn_size, btn_size,
            on_dialer_delete_click_icon, NULL, state.icon_font);
        aroma_node_set_z_index(del_btn, Z_LAYER_STATUS_BAR + 15);
        AromaNode *call_btn = aroma_ui_button(
            dialer_grid, AROMA_ICON_CALL,
            0, 0, btn_size, btn_size,
            on_dialer_call_click_icon, NULL, state.icon_font);
        aroma_node_set_z_index(call_btn, Z_LAYER_STATUS_BAR + 15);
    }


    AromaNode *contacts_content[] = {state.contact_listview, pagination_card};
    AromaNode *dialer_content[] = {dialer_card};
    aroma_tabs_set_content(state.phone_app_tabs, 0, contacts_content, 2);
    aroma_tabs_set_content(state.phone_app_tabs, 1, dialer_content, 1);
}














static pthread_t s_fetch_thread;
static volatile bool s_fetch_stop = false;
static bool s_fetch_started = false;
static volatile bool s_contacts_ui_dirty = false;

static void contacts_fetch_data_only(void)
{
    pthread_mutex_lock(&s_fetch_mutex);

    if (s_fetch_stop || contact_fetch_in_progress)
    {
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }

    if (s_contacts_fetched)
    {
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }



    const ContactsBtService *svc = bt_svc();
    bool connected = svc && svc->connected();
    bt_device_info_t device = {{0}};
    if (svc)
        svc->device_info(&device);

    if (s_fetch_stop || !connected || !device.name[0])
    {
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }

    contact_fetch_in_progress = true;
    pthread_mutex_unlock(&s_fetch_mutex);

    bt_contact_t bt_contacts[CONTACTS_MAX_STORE];
    int count = bt_hfp_fetch_contacts(device.path, bt_contacts, CONTACTS_MAX_STORE);

    pthread_mutex_lock(&s_fetch_mutex);
    contact_fetch_in_progress = false;
    if (s_fetch_stop)
    {
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }

    if (count > 0)
    {
        s_contacts_fetched = true;
        s_contact_count = count;
        for (int i = 0; i < count && i < CONTACTS_MAX_STORE; i++)
        {
            strncpy(s_contacts[i].name, bt_contacts[i].name, sizeof(s_contacts[i].name) - 1);
            s_contacts[i].name[sizeof(s_contacts[i].name) - 1] = '\0';
            strncpy(s_contacts[i].number, bt_contacts[i].number, sizeof(s_contacts[i].number) - 1);
            s_contacts[i].number[sizeof(s_contacts[i].number) - 1] = '\0';
        }
        contact_fetch_retries = 0;
        s_contacts_ui_dirty = true;
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }

    if (count == 0)
    {
        contact_fetch_retries++;
        if (contact_fetch_retries >= MIN_EMPTY_RESULT_RETRIES)
        {
            s_contacts_fetched = true;
            s_contact_count = 0;
            contact_fetch_retries = 0;
            s_contacts_ui_dirty = true;
        }
        pthread_mutex_unlock(&s_fetch_mutex);
        return;
    }

    contact_fetch_retries++;
    pthread_mutex_unlock(&s_fetch_mutex);
}

static void *contacts_fetch_thread_func(void *arg)
{
    (void)arg;
    bool last_connected = false;
    while (!s_fetch_stop)
    {

        for (int i = 0; i < CONTACTS_RETRY_INTERVAL_SEC * 10 && !s_fetch_stop; i++)
            usleep(100000);
        if (s_fetch_stop)
            break;
        const ContactsBtService *svc = bt_svc();
        bool connected = svc && svc->connected();
        if (s_fetch_stop)
            break;
        if (connected && !last_connected)
        {

            contact_fetch_retries = 0;
        }
        last_connected = connected;
        if (!connected)
            continue;
        bool fetched = s_contacts_fetched;
        if (!fetched)
            contacts_fetch_data_only();
    }
    return NULL;
}


#include "aroma_package.h"

static AromaAppPlugin s_contacts_mirror;

static bool contacts_hook_init(const AromaPackageManifest *manifest,
                               const char *install_dir,
                               const AromaPackageHost *host,
                               struct AromaNode *app_root)
{
    (void)manifest;
    (void)install_dir;
    (void)host;
    (void)app_root;
    memset(&s_contacts_mirror, 0, sizeof(s_contacts_mirror));
    s_contacts_mirror.id = "com.aroma.contacts";
    s_contacts_mirror.name = "Contacts";





    contact_fetch_retries = 0;
    contact_fetch_in_progress = false;
    contact_page = 0;
    total_pages = 0;
    dialer_number[0] = '\0';
    s_contacts_ui_dirty = false;
    s_fetch_stop = false;
    s_contact_count = 0;
    s_contacts_fetched = false;
    memset(s_contacts, 0, sizeof(s_contacts));

    if (!s_fetch_started)
    {
        if (pthread_create(&s_fetch_thread, NULL,
                           contacts_fetch_thread_func, NULL) == 0)
        {
            s_fetch_started = true;
        }
        else
        {
            fprintf(stderr, "[contacts] warning: fetch thread did not start\n");
        }
    }
    return true;
}

static bool contacts_hook_build_ui(struct AromaNode *app_root)
{
    s_contacts_mirror.app_root = app_root;
    build_phone_app_ui(app_root);
    return state.phone_node != NULL;
}

static bool contacts_hook_show(struct AromaNode *app_root)
{
    return open_phone(NULL, app_root);
}

static void contacts_hook_hide(struct AromaNode *app_root)
{
    close_phone(app_root);
}

static void contacts_hook_update(struct AromaNode *app_root)
{
    (void)app_root;


    bt_hfp_poll();


    if (s_contacts_ui_dirty && !s_fetch_stop && state.contact_listview)
    {
        s_contacts_ui_dirty = false;
        populate_contact_listview(state.contact_listview);
    }
}

static void contacts_hook_destroy(void)
{




    s_fetch_stop = true;
    if (s_fetch_started)
    {
        pthread_join(s_fetch_thread, NULL);
        s_fetch_started = false;
    }
    s_contacts_ui_dirty = false;


    contacts_bt_service()->set_enabled(false);




    state.phone_node = NULL;
    state.phone_close_btn = NULL;
    state.phone_app_tabs = NULL;
    state.contact_listview = NULL;
    pagination_card = NULL;
    prev_page_btn = NULL;
    next_page_btn = NULL;
    page_label = NULL;
    dialer_card = NULL;
    dialer_display_label = NULL;
    dialer_number[0] = '\0';
    contact_page = 0;
    total_pages = 0;
    contact_fetch_retries = 0;
    contact_fetch_in_progress = false;
    memset(&s_contacts_mirror, 0, sizeof(s_contacts_mirror));
}

static const AromaPackageHooks s_contacts_hooks = {
    .init = contacts_hook_init,
    .build_ui = contacts_hook_build_ui,
    .show = contacts_hook_show,
    .hide = contacts_hook_hide,
    .update = contacts_hook_update,
    .destroy = contacts_hook_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_contacts_hooks;
}
