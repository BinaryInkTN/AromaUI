/* Aroma Play - simple package store for the car host.
 *
 * The app list is a real aroma table (headers hidden): icon, name, info
 * and action columns, with Download buttons and a live progress bar
 * embedded as cell widgets. All metadata shown is real: whatever the
 * package manifest / store server declares (missing ratings and counts
 * are hidden, never invented).
 *
 * Threading: network runs on worker threads, which only stash data and
 * raise flags. store_app_update() runs every frame on the UI thread and
 * applies everything (status text, table rebuilds, installs, progress).
 * Workers never touch nodes.
 */

#include "app_registry.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "package_manager.h"
#include "store_client.h"
#include "setup_store.h"
#include "vehicle_view.h"
#include "apps/media/media_controls.h"
#include "theme_manager.h"
#include "widgets/aroma_table.h"
#include "widgets/aroma_dialog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>

static void close_store(void *user_data);

/* --- tabs --------------------------------------------------------------- */
enum
{
    TAB_GAMES = 0,
    TAB_APPS,
    TAB_INSTALLED,
    TAB_COUNT
};
static const char *s_tab_names[TAB_COUNT] = {"Games", "Apps", "Installed"};
static int s_tab = TAB_GAMES;
static char s_search[256] = "";

#define STORE_DEFAULT_URL "http://127.0.0.1:8080"
#define STORE_MAX_ROWS 6

/* Table columns: icon | name | info | action | more. */
enum
{
    COL_ICON = 0,
    COL_NAME,
    COL_INFO,
    COL_ACTION,
    COL_MORE,
    COL_COUNT
};

/* --- widgets -------------------------------------------------------------- */
static AromaNode *s_root = NULL;
static AromaNode *s_status_label = NULL;
static AromaNode *s_search_box = NULL;
static AromaNode *s_tab_btns[TAB_COUNT];
static AromaNode *s_refresh_btn = NULL;
static AromaNode *s_table = NULL;
/* Per-row live widgets, parallel to the table rows (rebuilt on refresh). */
static AromaNode *s_row_btns[STORE_MAX_ROWS];
static AromaNode *s_row_bars[STORE_MAX_ROWS];
static AromaNode *s_row_remove[STORE_MAX_ROWS];
static char s_row_ids[STORE_MAX_ROWS][STORE_ID_MAX];
static int s_row_kind[STORE_MAX_ROWS]; /* 0=none 1=online 2=installed */
static int s_row_count = 0;

/* --- installed tab: local install + server URL ----------------------------- */
static AromaNode *s_install_panel = NULL;
static char s_apak_path[512] = "";
static char s_server_url[512] = "";

/* --- app details dialog (row tap) ------------------------------------------ */
static AromaNode *s_dialog = NULL;
static char s_dialog_id[STORE_ID_MAX] = "";

/* --- online index + worker handoff ------------------------------------------ */
static StoreEntry *s_online = NULL;
static int s_online_count = 0;
static pthread_mutex_t s_online_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool s_fetch_busy = false;
static bool s_download_busy = false;

static bool s_ui_dirty = false;
static char s_pending_status[320] = "";
static bool s_have_pending_status = false;
/* Finished download awaiting UI-thread install (install may rebuild nodes). */
static char s_pending_install[512] = "";
static char s_pending_install_id[STORE_ID_MAX] = "";
/* Active download progress (written by worker, read by UI thread). */
static char s_dl_id[STORE_ID_MAX] = "";
static char s_dl_name[STORE_NAME_MAX] = "";
static long s_dl_done = 0;
static long s_dl_total = 0;
static bool s_store_open = false;

/* === helpers ================================================================ */

static void store_set_status(const char *text)
{
    if (s_status_label && text)
    {
        aroma_label_set_text(s_status_label, text);
        aroma_ui_request_redraw(NULL);
    }
}

static void post_status(const char *text)
{
    if (!text)
        return;
    pthread_mutex_lock(&s_online_mtx);
    snprintf(s_pending_status, sizeof(s_pending_status), "%s", text);
    s_have_pending_status = true;
    pthread_mutex_unlock(&s_online_mtx);
}

static void format_count(long n, char *out, size_t out_len)
{
    if (n >= 1000000)
        snprintf(out, out_len, "%.1fM", n / 1000000.0);
    else if (n >= 1000)
        snprintf(out, out_len, "%.1fK", n / 1000.0);
    else
        snprintf(out, out_len, "%ld", n);
}

static void format_size(long bytes, char *out, size_t out_len)
{
    if (bytes >= 1048576)
        snprintf(out, out_len, "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024)
        snprintf(out, out_len, "%.1f KB", bytes / 1024.0);
    else if (bytes > 0)
        snprintf(out, out_len, "%ld B", bytes);
    else
        snprintf(out, out_len, "?");
}

static bool str_contains_ci(const char *hay, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    if (!hay)
        return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
    {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return true;
    }
    return false;
}

/* Info column from real data only: unknown parts are left out. */
static void build_info(char *out, size_t out_len, const char *author,
                       const char *category, const char *version, long size,
                       float rating, int rating_count, long downloads,
                       const char *state)
{
    char sz[32];
    format_size(size, sz, sizeof(sz));
    int n = snprintf(out, out_len, "%s | %s | v%s | %s",
                     author[0] ? author : "Unknown", category, version, sz);
    if (n < 0)
        return;
    if (rating > 0.0f && rating_count > 0)
    {
        char cc[32];
        format_count(rating_count, cc, sizeof(cc));
        n += snprintf(out + n, n < (int)out_len ? out_len - (size_t)n : 0,
                      " | %.1f(%s)", rating, cc);
    }
    if (downloads > 0)
    {
        char dc[32];
        format_count(downloads, dc, sizeof(dc));
        n += snprintf(out + n, n < (int)out_len ? out_len - (size_t)n : 0,
                      " | %s", dc);
    }
    if (state && state[0])
        snprintf(out + n, n < (int)out_len ? out_len - (size_t)n : 0,
                 " | %s", state);
}

static AromaNode *make_button(const char *text,
                              bool (*cb)(AromaNode *, void *), void *ud)
{
    AromaNode *b = aroma_button_create(s_table, text, 0, 0, 80, 30);
    if (b)
    {
        if (cb)
            aroma_button_set_on_click(b, cb, ud);
        aroma_button_set_font(b, state.ui_font);
        aroma_button_setup_events(b, aroma_ui_request_redraw, NULL);
    }
    return b;
}

static const char *online_action_text(const StoreEntry *e)
{
    InstalledPackage *inst = package_manager_find(e->id);
    if (!inst)
        return "Download";
    if (inst->manifest.version_code < e->version_code)
        return "Update";
    return "Open";
}

/* === table view =============================================================== */

static bool on_row_primary(AromaNode *node, void *user_data);
static bool on_row_remove(AromaNode *node, void *user_data);
static void on_table_row(int row_idx, void *user_data);

/* Add one fully-populated row. Action cell holds a button, or a progress
 * bar while that id is downloading. Installed rows also get Remove. */
static void table_add_row(const char *icon, const char *name,
                          const char *info, const char *id, int kind,
                          const char *btn_text, bool downloadable)
{
    if (!s_table || s_row_count >= STORE_MAX_ROWS)
        return;
    int row = aroma_table_add_row(s_table);
    if (row < 0)
        return;
    int r = s_row_count;

    AromaNode *icon_node = aroma_ui_icon(s_table,
                                         icon[0] ? icon : "AROMA_ICON_WIDGETS",
                                         0, 0, 36, 0xFF1A73E8,
                                         state.icon_font);
    aroma_table_set_cell_widget(s_table, row, COL_ICON, icon_node);
    aroma_table_set_cell_text(s_table, row, COL_NAME, name);
    aroma_table_set_cell_text(s_table, row, COL_INFO, info);

    s_row_btns[r] = NULL;
    s_row_bars[r] = NULL;
    s_row_remove[r] = NULL;
    if (kind == 1 && downloadable && s_dl_id[0] &&
        strcmp(s_dl_id, id) == 0)
    {
        s_row_bars[r] = aroma_ui_progressbar(s_table, 0, 0, 100, 12,
                                             PROGRESS_TYPE_DETERMINATE, 0.0f);
        aroma_table_set_cell_widget(s_table, row, COL_ACTION,
                                    s_row_bars[r]);
        aroma_table_set_cell_text(s_table, row, COL_MORE, "");
    }
    else
    {
        s_row_btns[r] = make_button(btn_text, on_row_primary,
                                    (void *)(intptr_t)r);
        aroma_table_set_cell_widget(s_table, row, COL_ACTION,
                                    s_row_btns[r]);
        if (kind == 2 || (kind == 1 && package_manager_find(id)))
        {
            s_row_remove[r] = make_button("Remove", on_row_remove,
                                          (void *)(intptr_t)r);
            aroma_table_set_cell_widget(s_table, row, COL_MORE,
                                        s_row_remove[r]);
        }
        else
        {
            aroma_table_set_cell_text(s_table, row, COL_MORE, "");
        }
    }

    snprintf(s_row_ids[r], sizeof(s_row_ids[r]), "%s", id);
    s_row_kind[r] = kind;
    s_row_count++;
}

static void clear_rows(void)
{
    if (s_table)
        aroma_table_clear_rows(s_table, true);
    for (int r = 0; r < STORE_MAX_ROWS; r++)
    {
        s_row_btns[r] = NULL;
        s_row_bars[r] = NULL;
        s_row_remove[r] = NULL;
        s_row_ids[r][0] = '\0';
        s_row_kind[r] = 0;
    }
    s_row_count = 0;
}

static void refresh_store_list(void)
{
    if (!s_table)
        return;
    clear_rows();
    bool installed_tab = (s_tab == TAB_INSTALLED);
    AromaRect *tr = aroma_node_get_rect(s_table);
    if (tr)
    {
        tr->y = installed_tab ? 170 : 116;
        aroma_node_invalidate(s_table);
    }

    if (installed_tab)
    {
        int count = package_manager_count();
        for (int i = 0; i < count && s_row_count < 5; i++)
        {
            InstalledPackage *pkg = package_manager_get(i);
            if (!pkg)
                continue;
            char info[128];
            build_info(info, sizeof(info), pkg->manifest.author,
                       pkg->manifest.category[0] ? pkg->manifest.category
                                                 : "Apps",
                       pkg->manifest.version, 0, pkg->manifest.rating,
                       pkg->manifest.rating_count, pkg->manifest.downloads,
                       strncmp(pkg->manifest.id, "com.aroma.",
                               sizeof("com.aroma.") - 1) == 0 ? "System"
                                                              : NULL);
            table_add_row(pkg->manifest.icon, pkg->manifest.name, info,
                          pkg->manifest.id, 2, "Open", false);
        }
        if (count == 0)
            store_set_status("Nothing installed yet - get something free.");
        else
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "%d installed.", count);
            store_set_status(buf);
        }
        return;
    }

    /* Online tabs: filter the cached index by tab + search. */
    StoreEntry view[STORE_MAX_ENTRIES];
    int total = 0;
    pthread_mutex_lock(&s_online_mtx);
    for (int i = 0; i < s_online_count && total < STORE_MAX_ENTRIES; i++)
    {
        const StoreEntry *e = &s_online[i];
        bool games = (strcasecmp(e->category, "Games") == 0);
        if (s_tab == TAB_GAMES && !games)
            continue;
        if (s_tab == TAB_APPS && games)
            continue;
        if (!(str_contains_ci(e->name, s_search) ||
              str_contains_ci(e->id, s_search) ||
              str_contains_ci(e->author, s_search) ||
              str_contains_ci(e->description, s_search)))
            continue;
        view[total++] = *e;
    }
    pthread_mutex_unlock(&s_online_mtx);

    int shown = total < STORE_MAX_ROWS ? total : STORE_MAX_ROWS;
    for (int i = 0; i < shown; i++)
    {
        char info[128];
        InstalledPackage *inst = package_manager_find(view[i].id);
        build_info(info, sizeof(info), view[i].author, view[i].category,
                   view[i].version, view[i].size, view[i].rating,
                   view[i].rating_count, view[i].downloads,
                   inst ? (inst->manifest.version_code < view[i].version_code
                               ? "Update available"
                               : "Installed")
                        : NULL);
        table_add_row(view[i].icon, view[i].name, info, view[i].id, 1,
                      online_action_text(&view[i]), true);
    }
    if (s_online_count == 0)
        store_set_status("No store data - check connection, hit Refresh.");
    else if (total == 0)
        store_set_status("No matches - try another search.");
    else if (total > shown)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Showing %d of %d.", shown, total);
        store_set_status(buf);
    }
    else
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%d package(s).", total);
        store_set_status(buf);
    }
}

/* === install / download ======================================================= */

static bool on_path_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_apak_path, sizeof(s_apak_path), "%s", text ? text : "");
    return true;
}

static bool on_install_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!s_apak_path[0])
    {
        store_set_status("Enter a path to an .apak file first");
        return true;
    }
    char err[256] = {0};
    if (!package_manager_install_apak(s_apak_path, err, sizeof(err)))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Install failed: %s",
                 err[0] ? err : "unknown error");
        store_set_status(buf);
        return true;
    }
    const char *id = package_manager_last_installed_id();
    if (id && !vehicle_view_add_package_card(id))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Installed %s but UI failed to load", id);
        store_set_status(buf);
        refresh_store_list();
        return true;
    }
    char buf[320];
    snprintf(buf, sizeof(buf), "Installed %s - find it in the app drawer",
             id ? id : "package");
    store_set_status(buf);
    refresh_store_list();
    return true;
}

static bool on_url_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_server_url, sizeof(s_server_url), "%s", text ? text : "");
    setup_store_set("store_url", s_server_url);
    setup_store_save();
    return true;
}

static void *store_fetch_thread(void *arg)
{
    char url[512];
    snprintf(url, sizeof(url), "%s", (const char *)arg);
    free(arg);
    char err[256] = {0};
    int count = 0;
    StoreEntry *entries = store_client_fetch(url, &count, err, sizeof(err));
    pthread_mutex_lock(&s_online_mtx);
    store_client_free(s_online);
    s_online = entries;
    s_online_count = entries ? count : 0;
    if (!entries)
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Store error: %s",
                 err[0] ? err : "unknown error");
        snprintf(s_pending_status, sizeof(s_pending_status), "%s", buf);
        s_have_pending_status = true;
    }
    else if (count == 0)
    {
        snprintf(s_pending_status, sizeof(s_pending_status), "Store is empty.");
        s_have_pending_status = true;
    }
    else
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Found %d package(s).", count);
        snprintf(s_pending_status, sizeof(s_pending_status), "%s", buf);
        s_have_pending_status = true;
    }
    pthread_mutex_unlock(&s_online_mtx);
    s_fetch_busy = false;
    s_ui_dirty = true;
    return NULL;
}

static bool start_fetch(void)
{
    if (s_fetch_busy)
        return true;
    if (!s_server_url[0])
    {
        store_set_status("Set the store server URL (Installed tab) first");
        return true;
    }
    s_fetch_busy = true;
    char *url = malloc(strlen(s_server_url) + 1);
    if (!url)
    {
        s_fetch_busy = false;
        store_set_status("Out of memory");
        return true;
    }
    snprintf(url, strlen(s_server_url) + 1, "%s", s_server_url);
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, store_fetch_thread, url) != 0)
    {
        free(url);
        s_fetch_busy = false;
        store_set_status("Could not start refresh");
    }
    else
    {
        store_set_status("Contacting store...");
    }
    pthread_attr_destroy(&attr);
    return true;
}

static bool on_refresh_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    return start_fetch();
}

static int store_download_progress(const char *id, long downloaded, long total,
                                   void *user_data)
{
    (void)user_data;
    (void)id;
    pthread_mutex_lock(&s_online_mtx);
    s_dl_done = downloaded;
    s_dl_total = total;
    pthread_mutex_unlock(&s_online_mtx);
    return 0;
}

static void *store_download_thread(void *arg)
{
    StoreEntry entry = *(StoreEntry *)arg;
    free(arg);
    char url[512];
    snprintf(url, sizeof(url), "%s", s_server_url);

    bool id_ok = entry.id[0] != '\0';
    for (const char *p = entry.id; id_ok && *p; p++)
    {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            id_ok = false;
    }
    if (!id_ok)
    {
        pthread_mutex_lock(&s_online_mtx);
        s_dl_id[0] = '\0';
        snprintf(s_pending_status, sizeof(s_pending_status),
                 "Refusing package with unsafe id");
        s_have_pending_status = true;
        pthread_mutex_unlock(&s_online_mtx);
        s_download_busy = false;
        s_ui_dirty = true;
        return NULL;
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/.dl-%s.apak", package_manager_dir(),
             entry.id);
    char err[256] = {0};
    if (!store_client_download(url, &entry, tmp, store_download_progress,
                               NULL, err, sizeof(err)))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Download failed: %s",
                 err[0] ? err : "unknown error");
        pthread_mutex_lock(&s_online_mtx);
        s_dl_id[0] = '\0';
        snprintf(s_pending_status, sizeof(s_pending_status), "%s", buf);
        s_have_pending_status = true;
        pthread_mutex_unlock(&s_online_mtx);
        s_download_busy = false;
        s_ui_dirty = true;
        return NULL;
    }
    /* Hand the file to the UI thread for install (node work). */
    pthread_mutex_lock(&s_online_mtx);
    snprintf(s_pending_install, sizeof(s_pending_install), "%s", tmp);
    snprintf(s_pending_install_id, sizeof(s_pending_install_id), "%s",
             entry.id);
    snprintf(s_pending_status, sizeof(s_pending_status), "Installing %s...",
             entry.name);
    s_have_pending_status = true;
    pthread_mutex_unlock(&s_online_mtx);
    s_download_busy = false;
    s_ui_dirty = true;
    return NULL;
}

static bool start_download_entry(const StoreEntry *entry)
{
    if (s_download_busy)
    {
        store_set_status("A download is already running - wait for it");
        return true;
    }
    s_download_busy = true;
    pthread_mutex_lock(&s_online_mtx);
    snprintf(s_dl_id, sizeof(s_dl_id), "%s", entry->id);
    snprintf(s_dl_name, sizeof(s_dl_name), "%s", entry->name);
    s_dl_done = 0;
    s_dl_total = 0;
    pthread_mutex_unlock(&s_online_mtx);
    StoreEntry *copy = malloc(sizeof(*copy));
    if (!copy)
    {
        s_download_busy = false;
        pthread_mutex_lock(&s_online_mtx);
        s_dl_id[0] = '\0';
        pthread_mutex_unlock(&s_online_mtx);
        store_set_status("Out of memory");
        return true;
    }
    *copy = *entry;
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, store_download_thread, copy) != 0)
    {
        free(copy);
        s_download_busy = false;
        pthread_mutex_lock(&s_online_mtx);
        s_dl_id[0] = '\0';
        pthread_mutex_unlock(&s_online_mtx);
        store_set_status("Could not start download");
    }
    else
    {
        refresh_store_list();
    }
    pthread_attr_destroy(&attr);
    return true;
}

static const StoreEntry *find_online(const char *id, StoreEntry *copy_out)
{
    static StoreEntry found;
    bool ok = false;
    pthread_mutex_lock(&s_online_mtx);
    for (int i = 0; i < s_online_count; i++)
    {
        if (strcmp(s_online[i].id, id) == 0)
        {
            found = s_online[i];
            ok = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_online_mtx);
    if (!ok)
        return NULL;
    if (copy_out)
        *copy_out = found;
    return copy_out ? copy_out : &found;
}

static bool do_open_package(const char *id)
{
    if (!id || !id[0])
        return true;
    if (!package_manager_find(id))
    {
        store_set_status("Install it first");
        return true;
    }
    if (s_root)
        close_store(s_root);
    if (!vehicle_view_open_package(id))
        store_set_status("Could not open package");
    return true;
}

static bool do_remove_package(const char *id)
{
    if (!id || !id[0])
        return true;
    char err[256] = {0};
    if (!package_manager_uninstall(id, err, sizeof(err)))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Remove failed: %s",
                 err[0] ? err : "unknown error");
        store_set_status(buf);
        return true;
    }
    vehicle_view_remove_package_card(id);
    if (err[0])
        store_set_status(err);
    else
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Removed %s", id);
        store_set_status(buf);
    }
    refresh_store_list();
    return true;
}

/* === app details dialog (row tap) ===============================================
 * Tapping a row opens a dialog with real info + actions:
 *   - installed    -> Open + Uninstall (+ Update when an online update exists)
 *   - not installed-> Download/Install
 * The dialog is rebuilt on every tap (old node destroyed first) so there is
 * never a stale hidden node behind the table. Action callbacks only hide
 * (never destroy) because the dialog event handler touches the widget after
 * the callback returns. */

static void dialog_dismiss_current(void)
{
    if (s_dialog)
        aroma_dialog_hide(s_dialog);
}

static void on_dialog_close_action(void *user_data)
{
    (void)user_data;
    dialog_dismiss_current();
}

static void on_dialog_open_action(void *user_data)
{
    (void)user_data;
    char id[STORE_ID_MAX] = "";
    snprintf(id, sizeof(id), "%s", s_dialog_id);
    dialog_dismiss_current();
    do_open_package(id);
}

static void on_dialog_uninstall_action(void *user_data)
{
    (void)user_data;
    char id[STORE_ID_MAX] = "";
    snprintf(id, sizeof(id), "%s", s_dialog_id);
    dialog_dismiss_current();
    do_remove_package(id);
}

static void on_dialog_download_action(void *user_data)
{
    (void)user_data;
    StoreEntry e;
    if (!find_online(s_dialog_id, &e))
    {
        dialog_dismiss_current();
        return;
    }
    dialog_dismiss_current();
    start_download_entry(&e);
}

static void show_app_dialog(const char *id, const char *title,
                            const char *message, const char *primary_label,
                            void (*primary_cb)(void *),
                            bool show_uninstall)
{
    if (!s_root || !id || !id[0])
        return;
    if (s_dialog)
    {
        aroma_dialog_destroy(s_dialog);
        s_dialog = NULL;
    }
    snprintf(s_dialog_id, sizeof(s_dialog_id), "%s", id);

    s_dialog = aroma_dialog_create(s_root, title ? title : id,
                                   message ? message : "",
                                   560, 320, DIALOG_TYPE_BASIC);
    if (!s_dialog)
    {
        s_dialog_id[0] = '\0';
        return;
    }
    aroma_dialog_set_font(s_dialog, state.ui_font);
    aroma_node_set_z_index(s_dialog, Z_LAYER_STATUS_BAR + 20);
    if (primary_label && primary_cb)
        aroma_dialog_add_action(s_dialog, primary_label, primary_cb, NULL);
    if (show_uninstall)
        aroma_dialog_add_action(s_dialog, "Uninstall",
                                on_dialog_uninstall_action, NULL);
    aroma_dialog_add_action(s_dialog, "Close", on_dialog_close_action, NULL);
    /* aroma_dialog_hide() marks the node hidden; a rebuilt dialog starts
     * visible=false + hidden=false, but unhide defensively for reuse. */
    aroma_node_set_hidden(s_dialog, false);
    aroma_dialog_show(s_dialog);
}

static void show_online_app_dialog(const StoreEntry *e)
{
    if (!e)
        return;
    InstalledPackage *inst = package_manager_find(e->id);
    char info[128];
    build_info(info, sizeof(info), e->author, e->category, e->version,
               e->size, e->rating, e->rating_count, e->downloads,
               inst ? (inst->manifest.version_code < e->version_code
                           ? "Update available"
                           : "Installed")
                    : NULL);
    char msg[256];
    if (e->description[0])
    {
        /* Keep "<info> - <desc>" inside the dialog's 256-byte message. */
        size_t info_len = strlen(info);
        size_t room = info_len + 3 < sizeof(msg) ? sizeof(msg) - info_len - 4 : 0;
        char desc[192];
        snprintf(desc, sizeof(desc), "%s", e->description);
        if (room < strlen(desc))
            desc[room] = '\0';
        snprintf(msg, sizeof(msg), "%s - %s", info, desc);
    }
    else
    {
        snprintf(msg, sizeof(msg), "%s", info);
    }
    if (!inst)
    {
        show_app_dialog(e->id, e->name, msg, "Download",
                        on_dialog_download_action, false);
    }
    else if (inst->manifest.version_code < e->version_code)
    {
        show_app_dialog(e->id, e->name, msg, "Update",
                        on_dialog_download_action, true);
    }
    else
    {
        show_app_dialog(e->id, e->name, msg, "Open",
                        on_dialog_open_action, true);
    }
}

static void show_installed_app_dialog(const InstalledPackage *pkg)
{
    if (!pkg)
        return;
    /* If the store index knows a newer build, offer Update as primary. */
    StoreEntry online;
    const StoreEntry *on = find_online(pkg->manifest.id, &online);
    bool has_update = on && pkg->manifest.version_code < on->version_code;

    char info[128];
    if (has_update)
    {
        build_info(info, sizeof(info), pkg->manifest.author,
                   pkg->manifest.category[0] ? pkg->manifest.category : "Apps",
                   pkg->manifest.version, on->size, on->rating,
                   on->rating_count, on->downloads, "Update available");
    }
    else
    {
        build_info(info, sizeof(info), pkg->manifest.author,
                   pkg->manifest.category[0] ? pkg->manifest.category : "Apps",
                   pkg->manifest.version, 0, pkg->manifest.rating,
                   pkg->manifest.rating_count, pkg->manifest.downloads,
                   strncmp(pkg->manifest.id, "com.aroma.",
                           sizeof("com.aroma.") - 1) == 0 ? "System Installed"
                                                          : "Installed");
    }
    char msg[256];
    const char *desc = has_update && on->description[0]
                           ? on->description
                           : pkg->manifest.description;
    if (desc && desc[0])
    {
        size_t info_len = strlen(info);
        size_t room = info_len + 3 < sizeof(msg) ? sizeof(msg) - info_len - 4 : 0;
        char trunc[192];
        snprintf(trunc, sizeof(trunc), "%s", desc);
        if (room < strlen(trunc))
            trunc[room] = '\0';
        snprintf(msg, sizeof(msg), "%s - %s", info, trunc);
    }
    else
    {
        snprintf(msg, sizeof(msg), "%s", info);
    }
    if (has_update)
    {
        show_app_dialog(pkg->manifest.id, pkg->manifest.name, msg, "Update",
                        on_dialog_download_action, true);
    }
    else
    {
        show_app_dialog(pkg->manifest.id, pkg->manifest.name, msg, "Open",
                        on_dialog_open_action, true);
    }
}

/* === row actions ============================================================== */

static bool on_row_primary(AromaNode *node, void *user_data)
{
    (void)node;
    int row = (int)(intptr_t)user_data;
    if (row < 0 || row >= s_row_count || !s_row_ids[row][0])
        return true;
    if (s_row_kind[row] == 2)
        return do_open_package(s_row_ids[row]);
    StoreEntry e;
    if (!find_online(s_row_ids[row], &e))
        return true;
    const char *act = online_action_text(&e);
    if (strcmp(act, "Open") == 0)
        return do_open_package(e.id);
    return start_download_entry(&e);
}

static bool on_row_remove(AromaNode *node, void *user_data)
{
    (void)node;
    int row = (int)(intptr_t)user_data;
    if (row < 0 || row >= s_row_count || !s_row_ids[row][0])
        return true;
    return do_remove_package(s_row_ids[row]);
}

/* Tapping a table row opens the app details dialog (info + Open/Uninstall
 * when installed, Download/Install when not). */
static void on_table_row(int row_idx, void *user_data)
{
    (void)user_data;
    if (row_idx < 0 || row_idx >= s_row_count || !s_row_ids[row_idx][0])
        return;
    if (s_row_kind[row_idx] == 1)
    {
        StoreEntry e;
        if (find_online(s_row_ids[row_idx], &e))
            show_online_app_dialog(&e);
    }
    else if (s_row_kind[row_idx] == 2)
    {
        InstalledPackage *pkg = package_manager_find(s_row_ids[row_idx]);
        if (pkg)
            show_installed_app_dialog(pkg);
    }
}

/* === tabs / search ============================================================== */

static void apply_tab_visibility(void)
{
    bool installed = (s_tab == TAB_INSTALLED);
    if (s_install_panel)
        aroma_node_set_hidden(s_install_panel, !installed);
    if (s_refresh_btn)
        aroma_node_set_hidden(s_refresh_btn, installed);
    aroma_ui_request_redraw(NULL);
}

static bool on_tab_click(AromaNode *node, void *user_data)
{
    (void)node;
    int tab = (int)(intptr_t)user_data;
    if (tab < 0 || tab >= TAB_COUNT)
        return true;
    s_tab = tab;
    if (s_dialog)
        aroma_dialog_hide(s_dialog);
    apply_tab_visibility();
    refresh_store_list();
    if (tab != TAB_INSTALLED && s_online_count == 0 && !s_fetch_busy)
        start_fetch();
    return true;
}

static bool on_search_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_search, sizeof(s_search), "%s", text ? text : "");
    refresh_store_list();
    return true;
}

/* === UI-thread update: drains everything workers posted ========================== */

static void store_app_update(AromaAppPlugin *app)
{
    (void)app;
    char status[320] = {0};
    bool have_status = false;
    char install[512] = "";
    pthread_mutex_lock(&s_online_mtx);
    if (s_have_pending_status)
    {
        snprintf(status, sizeof(status), "%s", s_pending_status);
        s_have_pending_status = false;
        have_status = true;
    }
    if (s_pending_install[0])
    {
        snprintf(install, sizeof(install), "%s", s_pending_install);
        s_pending_install[0] = '\0';
        s_pending_install_id[0] = '\0';
    }
    bool dl_active = s_dl_id[0] != '\0';
    long dl_done = s_dl_done;
    long dl_total = s_dl_total;
    char dl_id[STORE_ID_MAX] = "";
    if (dl_active)
        snprintf(dl_id, sizeof(dl_id), "%s", s_dl_id);
    pthread_mutex_unlock(&s_online_mtx);

    if (install[0])
    {
        /* UI thread: may live-update (teardown + rebuild nodes). */
        char err[256] = {0};
        if (!package_manager_install_apak(install, err, sizeof(err)))
        {
            char buf[320];
            snprintf(buf, sizeof(buf), "Install failed: %s",
                     err[0] ? err : "unknown error");
            store_set_status(buf);
            have_status = false;
        }
        else
        {
            const char *id = package_manager_last_installed_id();
            if (id && !vehicle_view_add_package_card(id))
            {
                char buf[320];
                snprintf(buf, sizeof(buf), "Installed %s but UI failed to load",
                         id);
                store_set_status(buf);
                have_status = false;
            }
            else
            {
                char buf[320];
                snprintf(buf, sizeof(buf),
                         "Installed %s - find it in the app drawer",
                         id ? id : "package");
                store_set_status(buf);
                have_status = false;
            }
        }
        remove(install);
        pthread_mutex_lock(&s_online_mtx);
        s_dl_id[0] = '\0';
        pthread_mutex_unlock(&s_online_mtx);
        s_ui_dirty = true;
    }
    if (have_status)
        store_set_status(status);

    /* Live download progress on the matching row's bar. */
    if (dl_active && s_store_open && !install[0])
    {
        for (int r = 0; r < s_row_count; r++)
        {
            if (s_row_kind[r] == 1 && strcmp(s_row_ids[r], dl_id) == 0 &&
                s_row_bars[r])
            {
                float p = 0.0f;
                if (dl_total > 0)
                {
                    p = (float)dl_done / (float)dl_total;
                    if (p > 1.0f)
                        p = 1.0f;
                }
                aroma_progressbar_set_progress(s_row_bars[r], p);
                char buf[128];
                if (dl_total > 0)
                    snprintf(buf, sizeof(buf), "Downloading %s... %ld%%",
                             s_dl_name, (100L * dl_done) / dl_total);
                else
                    snprintf(buf, sizeof(buf), "Downloading %s... %ld KB",
                             s_dl_name, dl_done / 1024);
                store_set_status(buf);
                aroma_ui_request_redraw(NULL);
                break;
            }
        }
    }
    if (s_ui_dirty)
    {
        s_ui_dirty = false;
        if (s_store_open)
            refresh_store_list();
    }
}

/* === open / close chrome ============================================================ */

static void store_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    rect->x = 0;
    rect->y = WIN_H + (int)((0 - WIN_H) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    aroma_node_invalidate(target);
}

static void store_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    rect->x = 0;
    rect->y = (int)(WIN_H * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(target);
}

static bool open_store(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
        send_app_drawer_behind();
    s_store_open = true;
    /* Drop any hidden dialog left over from the last session (safe here:
     * not inside a dialog callback). */
    if (s_dialog)
    {
        aroma_dialog_destroy(s_dialog);
        s_dialog = NULL;
        s_dialog_id[0] = '\0';
    }
    refresh_store_list();
    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, store_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    vehicle_view_raise_subtree(card_node);
    if (s_tab != TAB_INSTALLED && s_online_count == 0 && !s_fetch_busy)
        start_fetch();
    return true;
}

static void close_store(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    s_store_open = false;
    /* Hide (never destroy here): this can run inside a dialog action
     * callback, and the dialog event handler touches the widget after the
     * callback returns. The next show_app_dialog() destroys the node. */
    if (s_dialog)
        aroma_dialog_hide(s_dialog);
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, store_closing_anim, NULL);
    if (anim)
        aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}

static bool store_app_init(AromaAppPlugin *app)
{
    (void)app;
    return true;
}

static void store_app_destroy(AromaAppPlugin *app)
{
    (void)app;
}

static bool store_app_build_ui(AromaAppPlugin *app, AromaNode *parent)
{
    (void)app;
    if (!parent)
        return false;
    s_root = parent;

    AromaNode *title = aroma_ui_label(parent, "Aroma Play", 20, 12,
                                      LABEL_STYLE_LABEL_LARGE, state.ui_font);
    (void)title;
    s_search_box = aroma_ui_textbox(parent, 300, 10, 360, 40, "Search",
                                    on_search_changed, NULL, state.ui_font);
    AromaNode *close_btn = aroma_ui_iconbutton(
        parent, AROMA_ICON_CLOSE, WIN_W - 60, 12, 40, ICON_BUTTON_FILLED,
        close_store, parent, state.icon_font);
    (void)close_btn;

    s_status_label = aroma_ui_label(parent, "Welcome to Aroma Play",
                                    20, 540, LABEL_STYLE_LABEL_SMALL,
                                    state.ui_font);

    int tx = 20;
    for (int t = 0; t < TAB_COUNT; t++)
    {
        s_tab_btns[t] = aroma_ui_button(parent, s_tab_names[t], tx, 64, 110,
                                        36, on_tab_click,
                                        (void *)(intptr_t)t, state.ui_font);
        tx += 118;
    }
    s_refresh_btn = aroma_ui_button(parent, "Refresh", 874, 64, 110, 36,
                                    on_refresh_click, NULL, state.ui_font);

    /* Installed tab: local install + server URL.
     * NOTE: children coords are panel-relative (panel itself is at y=106),
     * so y~=10 here == y~=116 absolute. Using absolute y here would double-
     * offset after layout and land behind the table (which starts at 170). */
    s_install_panel = aroma_ui_container(
        parent, 0, 106, WIN_W, 56,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_START);
    AromaNode *path_label = aroma_ui_label(s_install_panel, "Local .apak:",
                                           20, 18, LABEL_STYLE_LABEL_SMALL,
                                           state.ui_font);
    (void)path_label;
    AromaNode *path_entry = aroma_ui_textbox(s_install_panel, 110, 10, 330,
                                             36, "/path/to/app.apak",
                                             on_path_changed, NULL,
                                             state.ui_font);
    (void)path_entry;
    AromaNode *install_btn = aroma_ui_button(s_install_panel, "Install", 450,
                                             10, 100, 36, on_install_click,
                                             NULL, state.ui_font);
    (void)install_btn;
    snprintf(s_server_url, sizeof(s_server_url), "%s",
             setup_store_get("store_url", STORE_DEFAULT_URL));
    AromaNode *url_label = aroma_ui_label(s_install_panel, "Server:",
                                          570, 18, LABEL_STYLE_LABEL_SMALL,
                                          state.ui_font);
    (void)url_label;
    AromaNode *url_entry = aroma_ui_textbox(s_install_panel, 630, 10, 280,
                                            36, STORE_DEFAULT_URL,
                                            on_url_changed, NULL,
                                            state.ui_font);
    aroma_textbox_set_text(url_entry, s_server_url);

    /* The app list: icon | name | info | action | more. No headers. */
    s_table = aroma_table_create(parent, 20, 116, 984, 0, COL_COUNT);
    aroma_table_set_font(s_table, state.ui_font);
    aroma_table_set_header_visible(s_table, false);
    aroma_table_set_row_height(s_table, 60);
    aroma_table_set_col_width(s_table, COL_ICON, 56);
    aroma_table_set_col_width(s_table, COL_NAME, 250);
    aroma_table_set_col_width(s_table, COL_INFO, 400);
    aroma_table_set_col_width(s_table, COL_ACTION, 150);
    aroma_table_set_col_width(s_table, COL_MORE, 128);
    aroma_table_set_callback(s_table, on_table_row, NULL);
    /* Progressbar needs no font; icon cells get theirs at creation. */

    apply_tab_visibility();
    refresh_store_list();
    return true;
}

static bool store_app_show(AromaAppPlugin *app, AromaNode *parent)
{
    (void)parent;
    return open_store(NULL, app->app_root);
}

static void store_app_hide(AromaAppPlugin *app)
{
    close_store(app->app_root);
}

void register_store_app(void)
{
    static AromaAppPlugin app = {0};
    app.id = "com.aroma.store";
    app.name = "Aroma Play";
    app.icon = AROMA_ICON_SHOP;
    app.card_color = aroma_color_blend(0xFFFFFFFF, 0xFF1A73E8, 0.1f);
    app.init = store_app_init;
    app.build_ui = store_app_build_ui;
    app.destroy = store_app_destroy;
    app.show = store_app_show;
    app.hide = store_app_hide;
    app.update = store_app_update;

    app_registry_register_app(&app);
}
