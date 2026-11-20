#define _POSIX_C_SOURCE 200809L

#include "ide_internal.h"

#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_event.h>
#include <aroma_incense_loader.h>
#include <aroma_material_icons.h>
#include <aroma_node.h>
#include <aroma_style.h>
#include <aroma_ui.h>
#include <limits.h>
#include <widgets/aroma_canvas.h>
#include <widgets/aroma_code_editor.h>
#include <widgets/aroma_card.h>
#include <widgets/aroma_container.h>
#include <widgets/aroma_dropdown.h>
#include <widgets/aroma_label.h>
#include <widgets/aroma_sidebar.h>
#include <widgets/aroma_switch.h>
#include <widgets/aroma_checkbox.h>
#include <widgets/aroma_webview.h>
#include <widgets/aroma_window.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

#include <aroma_file_dialog.h>
typedef enum IdeDialogMode IdeDialogMode;
static void draw_all(void);
static void ide_update_cursor(int x, int y);
static void markdown_preview_create(void);
static void open_dialog(IdeDialogMode mode, const char *initial);
static void draw_dialog(void);
static void home_show_changelog(void);
static void home_hide_changelog(void);
static void cmd_palette_init(void);
static void cmd_palette_filter(void);
static void cmd_palette_execute_selected(void);
static bool cmd_palette_on_key(uint32_t key);
static void ide_open_file_via_nfd(void);
static void save_file(int index);
static bool save_file_ex(int index, bool quiet);
static int save_all_files_ex(bool quiet);
static void editor_flush_pending(bool force);
static void close_tab(int index);
static void refresh_workspace(void);
static void run_project_task(int index);
static void editor_on_definition(AromaNode *node, int line, int column, void *user_data);
static void draw_title(void);
static void draw_rail(void);
static void draw_sidebar(void);
void ide_home_init(IncenseRegistry *registry, IdeAssets *assets);
void ide_home_draw(void);
bool ide_home_on_click(AromaEvent *event, void *user_data);
bool ide_home_active(void);
const char *ide_home_project_path(void);
static void draw_tabs(void);
static void draw_breadcrumb(void);
static void draw_outline(void);
static void draw_designer_tree(AromaNode *canvas, int x, int *y, int w, int depth);
static void draw_panel(void);
static void draw_status(void);
static void draw_menu(void);
static void draw_toast(void);
static void menu_animate_show(void);
static void menu_animate_hide(void);
static void toast_animate_show(void);
static void toast_animate_hide(void);
static void draw_properties_content(AromaNode *canvas, int offset_x,
                                    int offset_y, int canvas_w,
                                    int canvas_h);
static void settings_window_open(void);
static void settings_window_close(void);
static void draw_settings_window(void);
static void settings_window_redraw(void);
static void settings_row_toggle(int category, int row);
static void settings_row_cycle(int category, int row);
static void settings_apply_theme(void);
static void settings_apply_editor(void);
static void settings_apply_all(void);
static void apply_editor_palette(void);
static void preview_new_note(void *user_data);

static int title_ctrl_center(int index);
static int title_ctrl_hit(int x, int y);
static void ide_window_closed(AromaWindow *window, void *user_data);
static void project_init_state(void);
static bool project_create(const char *name, const char *path);
static bool project_create_extended(const char *name, const char *path,
                                    int template_id, bool with_cmake,
                                    bool with_readme);
static bool project_create_wizard(const char *name, const char *project_root, int type, bool with_cmake, bool with_readme);
static bool project_open(const char *path);
static void project_build(void);
static void project_append_build_output(const char *text);
static void project_append_run_output(const char *text);
static void project_draw_output_panel(void);
static void project_show_in_sidebar(void);
static void set_panel_visible(bool visible);
static void panel_detach(void);
static void panel_attach(void);
static void panel_toggle_detach(void);
static AromaNode *panel_get_canvas(void);
static bool panel_detached_on_resize(AromaEvent *event, void *user_data);
static bool panel_detached_on_focus_gained(AromaEvent *event, void *user_data);
static void panel_sync_detached_size(void);
static bool sidebar_detached_on_resize(AromaEvent *event, void *user_data);
static bool sidebar_detached_on_focus_gained(AromaEvent *event, void *user_data);
static void sidebar_detach(void);
static void sidebar_attach(void);
static void sidebar_toggle_detach(void);
static AromaNode *sidebar_get_canvas(void);
static void subscribe_node(AromaNode *node, AromaEventType type,
                           AromaEventHandler handler);
static bool panel_on_key(AromaEvent *event, void *user_data);
static bool panel_on_move(AromaEvent *event, void *user_data);
static bool panel_on_click(AromaEvent *event, void *user_data);
static bool panel_on_exit(AromaEvent *event, void *user_data);
static bool sidebar_on_move(AromaEvent *event, void *user_data);
static bool sidebar_on_click(AromaEvent *event, void *user_data);
static bool sidebar_on_exit(AromaEvent *event, void *user_data);
static bool sidebar_on_scroll(AromaEvent *event, void *user_data);

#ifndef AROMA_IDE_SDK_ROOT
#define AROMA_IDE_SDK_ROOT "."
#endif

#define PROJECT_MAX_PROMPTS 4
#define PROJECT_PATH_MAX 512
#define PROJECT_NAME_MAX 128
#define PROJECT_CMD_MAX 4096
#define PROJECT_BUILD_DIR "build"
#define PROJECT_ANDROID_MAX_DEVICES 16
#define PROJECT_ANDROID_SERIAL_MAX 128

typedef struct ProjectState
{
    char root[PROJECT_PATH_MAX];
    char name[PROJECT_NAME_MAX];
    char build_cmd[PROJECT_CMD_MAX];
    char run_cmd[PROJECT_CMD_MAX];
    int target;                         
    char android_serial[PROJECT_ANDROID_SERIAL_MAX];
    bool has_build_output;
    char build_output[65536];
    volatile bool is_building;
    bool has_run_output;
    char run_output[65536];
    volatile bool is_running;
    volatile bool is_cleaning;
    bool has_clean_output;
    char clean_output[65536];
    int output_mode;                              
    pthread_t build_thread;
    pthread_t run_thread;
} ProjectState;

static ProjectState s_project;

/* Guards the project output buffers and the pending UI message queue that
 * worker threads (build/run/clean) use to talk to the UI thread. */
static pthread_mutex_t s_ui_lock = PTHREAD_MUTEX_INITIALIZER;
static char s_pending_toast[256];
static bool s_pending_toast_set;
static char s_pending_log[200];
static bool s_pending_log_set;
static char s_android_devices[PROJECT_ANDROID_MAX_DEVICES]
                             [PROJECT_ANDROID_SERIAL_MAX];
static char s_android_device_details[PROJECT_ANDROID_MAX_DEVICES][256];
static int s_android_device_count;
static int s_android_device_selected = -1;
static int s_android_device_scroll;
static int s_android_device_hover = -1;
static char s_android_adb_path[PROJECT_PATH_MAX];

static void ide_sync_layout_nodes(void);
static void toast(const char *fmt, ...);
static void log_add(const char *category, const char *fmt, ...);
static void ide_open_file(int index, bool split);
static void project_build(void);
static void project_set_target(int target);
static void project_android_next_device(void);
static void project_android_select_device(int index);
static void project_android_refresh_devices(bool notify);
static void project_android_setup(void);
static void open_android_device_manager(void);
static void home_recent_file(char *out, size_t out_size);
static void project_run(void);
static void project_close(void);
static void close_dialog(void);
static void destroy_prop_widgets(void);
static void cmd_palette_init(void);
static void cmd_palette_filter(void);
static void cmd_palette_execute_selected(void);
static void wizard_destroy_widgets(void);
static void wizard_create_widgets(void);
static void wizard_render_step(int step);
static void wizard_on_back(void);
static void wizard_on_next(void);
static void wizard_on_create(void);
static void wizard_on_cancel(void);
static void wizard_on_browse(void);
static void workspace_search(const char *query);
static AromaFont *font_ui(int size);
static AromaFont *font_mono(int size);
static void fill(AromaNode *node, int x, int y, int w, int h, int r, uint32_t color);
static void text(AromaNode *node, const char *text, int x, int y, uint32_t color, AromaFont *font);
static void text_fit(AromaNode *node, const char *text, int x, int y, int max_w, uint32_t color, AromaFont *font);
static int text_width(AromaFont *font, const char *text);
static void text_center(AromaNode *node, const char *text, int x, int y, int w, int h, uint32_t color, AromaFont *font);
static void icon_center(AromaNode *node, const char *glyph, int x, int y, int size,
                 uint32_t color);
static void line(AromaNode *node, int x1, int y1, int x2, int y2, uint32_t color, int thickness);

static int win_w = 1440;
static int win_h = 900;
static int last_win_w = 0;
static int last_win_h = 0;
static int right_x = 1226;
static int editor_w;
static int panel_w;
static int ds_code_x;
static int ds_dup_x;
static int ds_delete_x;
static int ds_save_x;
static int design_canvas_w;
static int design_body_y;
static int design_body_h;
static int status_y;
static int panel_y;
static int panel_h;
static int editor_h;

static int s_sidebar_w = 260;
static int s_rail_w = 54;
static int s_palette_w = 196;
static int s_panel_h_resizable = 248;

                                                                        
                                                                     
static bool s_panel_detached;
static AromaWindow *s_panel_detached_window;
static AromaNode *s_panel_detached_canvas;
static int s_panel_detached_w;
static int s_panel_detached_h;
static int s_panel_draw_px;
static int s_panel_draw_py;
static int s_panel_draw_right_x;
static int s_panel_draw_w;
static int s_panel_draw_h;
static AromaNode *s_panel_draw_canvas;
static AromaNode *s_panel_scrollable_log;

static bool s_sidebar_detached;
static AromaWindow *s_sidebar_detached_window;
static AromaNode *s_sidebar_detached_canvas;
static AromaNode *s_sidebar_canvas_saved;
static bool s_sidebar_detach_hover;

static int s_outline_w = 214;
static bool s_resizing_outline = false;
#define TITLE_MENU_X 14
#define TITLE_MENU_GAP 16
#define TITLE_SEARCH_W 220
#define TITLE_CTRL_W 30
#define TITLE_CTRL_COUNT 6
#define TITLE_ACTION_GAP 8
#define TITLE_BUILD_W 66
#define TITLE_RUN_W 58

static bool s_resizing_sidebar = false;
static bool s_resizing_panel = false;

#define TITLE_H 36
#define RAIL_W s_rail_w
#define SIDEBAR_W s_sidebar_w
#define SIDEBAR_X (s_rail_w)
#define EDITOR_X (s_rail_w + (s_sidebar_detached ? 0 : s_sidebar_w))
#define SIDEBAR_DRAW_X (s_sidebar_detached ? 0 : SIDEBAR_X)
#define TAB_H 36
#define TAB_ACTION_W 78
#define BREADCRUMB_H 28
#define EDITOR_Y (TITLE_H + TAB_H + BREADCRUMB_H)
#define STATUS_H 32
#define PANEL_HEADER_H 30

#define DESIGN_TOOLBAR_H 40
#define DESIGN_PALETTE_X EDITOR_X
#define DESIGN_PALETTE_W s_palette_w
#define DESIGN_CANVAS_X (EDITOR_X + s_palette_w + 8)

#define MENU_PANEL_X 56
#define MENU_panel_y 30
#define MENU_panel_w 276
#define MENU_ITEM_H 26

static int s_toast_x;
static int s_toast_y;
static int s_toast_w;
static int s_toast_h;
static int s_dialog_x;
static int s_dialog_y;
static int s_dialog_w;
static int s_dialog_h;

static int s_dialog_prop_scroll;
static int s_dialog_prop_sel;
static int s_dialog_prop_edit_row;
static char s_dialog_prop_value[1024];

static int s_wiz_x;
static int s_wiz_y;
static int s_wiz_w;
static int s_wiz_h;

#define WIZ_STEPPER_Y (s_wiz_y + 72)
#define WIZ_CONTENT_Y (s_wiz_y + 152)
#define WIZ_FOOTER_Y (s_wiz_y + s_wiz_h - 58)

                                                                            
static bool s_theme_is_dark = true;
static int  s_setting_accent = 0;

static const uint32_t s_accent_dark[5] = {
    0xFF0A84FFu, 0xFFBF5AF2u, 0xFF30D158u, 0xFFFF9F0Au, 0xFFFF453Au};
static const uint32_t s_accent_light[5] = {
    0xFF007AFFu, 0xFFAF52DEu, 0xFF34C759u, 0xFFFF9500u, 0xFFFF3B30u};

#define IDE_THEME(dark, light) (s_theme_is_dark ? (dark) : (light))
#define M3_PRIMARY (s_theme_is_dark ? s_accent_dark[s_setting_accent] : s_accent_light[s_setting_accent])
#define M3_ON_PRIMARY 0xFFFFFFFFu
#define M3_PRIMARY_CONTAINER IDE_THEME(0xFF003D73u, 0xFFCCE4FFu)
#define M3_ON_PRIMARY_CONTAINER IDE_THEME(0xFFB9DCFFu, 0xFF003D73u)
#define M3_SECONDARY_CONTAINER IDE_THEME(0xFF2C2C2Eu, 0xFFE5E5EAu)
#define M3_ON_SECONDARY_CONTAINER IDE_THEME(0xFFE5E5EAu, 0xFF1C1C1Eu)
#define M3_ERROR IDE_THEME(0xFFFF453Au, 0xFFFF3B30u)
#define M3_SURFACE IDE_THEME(0xFF1C1C1Eu, 0xFFF2F2F7u)
#define M3_SURFACE_LOWEST IDE_THEME(0xFF18181Au, 0xFFF5F5F7u)
#define M3_SURFACE_LOW IDE_THEME(0xFF202022u, 0xFFFFFFFFu)
#define M3_SURFACE_CONTAINER IDE_THEME(0xFF242426u, 0xFFE9E9EEu)
#define M3_SURFACE_CONTAINER_HIGH IDE_THEME(0xFF2C2C2Eu, 0xFFE5E5EAu)
#define M3_SURFACE_HIGH IDE_THEME(0xFF2C2C2Eu, 0xFFE5E5EAu)
#define M3_SURFACE_HIGHEST IDE_THEME(0xFF3A3A3Cu, 0xFFD1D1D6u)
#define M3_ON_SURFACE IDE_THEME(0xFFF2F2F7u, 0xFF1C1C1Eu)
#define M3_ON_SURFACE_VAR IDE_THEME(0xFFC7C7CCu, 0xFF3C3C43u)
#define M3_TEXT_DIM IDE_THEME(0xFF8E8E93u, 0xFF6C6C70u)
#define M3_OUTLINE IDE_THEME(0xFF8E8E93u, 0xFF8E8E93u)
#define M3_OUTLINE_VAR IDE_THEME(0xFF38383Au, 0xFFC6C6C8u)
#define M3_INVERSE_SURFACE IDE_THEME(0xFFF2F2F7u, 0xFF1C1C1Eu)
#define M3_INVERSE_ON_SURFACE IDE_THEME(0xFF1C1C1Eu, 0xFFF2F2F7u)
#define M3_SCRIM 0x66000000u
#define M3_STATE_HOVER IDE_THEME(0x14FFFFFFu, 0x14000000u)
#define M3_STATE_PRESS IDE_THEME(0x24FFFFFFu, 0x24000000u)
#define M3_BLUE IDE_THEME(0xFF64D2FFu, 0xFF007AFFu)
#define M3_GREEN IDE_THEME(0xFF30D158u, 0xFF34C759u)
#define M3_YELLOW IDE_THEME(0xFFFFD60Au, 0xFFFFCC00u)
#define M3_CYAN IDE_THEME(0xFF64D2FFu, 0xFF32ADE6u)
#define M3_PURPLE IDE_THEME(0xFFBF5AF2u, 0xFFAF52DEu)
#define M3_ORANGE IDE_THEME(0xFFFF9F0Au, 0xFFFF9500u)
#define M3_PINK IDE_THEME(0xFFFF375Fu, 0xFFFF2D55u)

#define C_BG M3_SURFACE
#define C_TITLE IDE_THEME(0xFF202A3Du, 0xFFE5E5EAu)
#define C_RAIL M3_SURFACE
#define C_SIDEBAR M3_SURFACE_LOW
#define C_TAB_BG M3_SURFACE
#define C_TAB_ACTIVE M3_SURFACE
#define C_BREADCRUMB M3_SURFACE
#define C_EDITOR M3_SURFACE_LOWEST
#define C_PANEL M3_SURFACE_LOW
#define C_BORDER M3_OUTLINE_VAR
#define C_BORDER_SOFT M3_OUTLINE_VAR
#define C_TEXT M3_ON_SURFACE
#define C_TEXT_SOFT M3_ON_SURFACE_VAR
#define C_TEXT_DIM M3_TEXT_DIM
#define C_TEXT_FAINT M3_OUTLINE
#define C_PRIMARY M3_PRIMARY
#define C_PRIMARY_DARK M3_PRIMARY_CONTAINER
#define C_PRIMARY_SOFT ((M3_PRIMARY & 0x00FFFFFFu) | 0x33000000u)
#define C_BLUE M3_BLUE
#define C_GREEN M3_GREEN
#define C_YELLOW M3_YELLOW
#define C_RED M3_ERROR
#define C_PURPLE M3_PURPLE
#define C_CYAN M3_CYAN
#define C_GUTTER M3_OUTLINE
#define C_HOVER M3_STATE_PRESS
#define C_HOVER_SOFT M3_STATE_HOVER
#define C_SELECT ((M3_PRIMARY & 0x00FFFFFFu) | 0x33000000u)
#define C_STATUS_TEXT M3_ON_SURFACE_VAR
#define C_STATUS_SOFT M3_TEXT_DIM
#define C_STATUS_DARK M3_SURFACE_CONTAINER

#define EDITOR_TAB_LIMIT 8
#define MAX_SYMBOLS 12
#define MAX_PROBLEMS 16
#define PANEL_VISIBLE_ROWS 8
#define PANEL_X_MARGIN 62
#define LOG_CAPACITY 64
#define LOG_VISIBLE 9

const int ide_ui_font_sizes[IDE_UI_FONT_COUNT] = {10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 34};
const int ide_icon_font_sizes[IDE_ICON_FONT_COUNT] = {10, 12, 14, 16, 18, 20, 22, 24, 28, 32};
const int ide_mono_font_sizes[IDE_MONO_FONT_COUNT] = {10, 11, 12, 13, 14, 16, 18};

typedef struct IdeTreeItem
{
    char name[IDE_NAME_MAX];
    char path[IDE_PATH_MAX];
    int depth;
    bool is_dir;
    bool expanded;
    int file_index;
    int parent;
    int first_child;
    int next_sibling;
} IdeTreeItem;

#define IDE_MAX_TREE_NODES (IDE_MAX_FILES * 2 + 16)
#define TREE_ROW_HEIGHT 22
#define TREE_TOP 96
#define TREE_VISIBLE_HEIGHT 510
#define SEARCH_MAX_HITS 80

typedef struct SearchHit
{
    int file;
    int line;
    int column;
    int length;
    char snippet[180];
} SearchHit;

static IdeTreeItem s_tree_nodes[IDE_MAX_TREE_NODES];
static int s_tree_node_count;
static int s_tree_rows[IDE_MAX_TREE_NODES];
static int s_tree_row_count;
static int s_tree_scroll;

static SearchHit s_search_hits[SEARCH_MAX_HITS];
static int s_search_hit_count;
static char s_search_query[96] = "aroma";
static char s_search_summary[96] = "Search the project";
static int s_search_y[SEARCH_MAX_HITS];
static int s_search_scroll;

typedef enum IdeDialogMode
{
    IDE_DIALOG_NONE,
    IDE_DIALOG_NEW_FILE,
    IDE_DIALOG_OPEN_FILE,
    IDE_DIALOG_SEARCH,
    IDE_DIALOG_NEW_PROJECT,
    IDE_DIALOG_OPEN_PROJECT,
    IDE_DIALOG_COMMAND_PALETTE,
    IDE_DIALOG_PROPERTIES,
    IDE_DIALOG_ANDROID_DEVICES,
} IdeDialogMode;

                                                                        
                                                                        
                                                                        

typedef struct CommandItem
{
    const char *label;
    const char *desc;
    const char *shortcut;
    const char *icon;
} CommandItem;

#define CMD_MAX 128
#define CMD_LIST_MAX 4
#define CMD_ITEM_H 36

static const CommandItem k_commands[] = {
              
    {"New File", "Create a new file in the workspace", "Ctrl+N", AROMA_ICON_ADD_BOX},
    {"Open File...", "Open a file from the workspace", "Ctrl+O", AROMA_ICON_FOLDER_OPEN},
    {"Save", "Save the active file", "Ctrl+S", AROMA_ICON_SAVE},
    {"Save All", "Save all open files", "Ctrl+Shift+S", AROMA_ICON_SAVE},
    {"Close Editor", "Close the active editor tab", "Ctrl+W", AROMA_ICON_CLOSE},
    {"New Project...", "Create a new AromaUI project", "Ctrl+Shift+N", AROMA_ICON_ADD},
    {"Open Project...", "Open an existing AromaUI project", "Ctrl+Shift+O", AROMA_ICON_FOLDER},
    {"Build Project", "Build the current project", "Ctrl+Shift+B", AROMA_ICON_BUILD},
    {"Run Project", "Run the current project", "F5", AROMA_ICON_PLAY_ARROW},
    {"Clean Build", "Clean and rebuild the project", "", AROMA_ICON_REFRESH},
    {"Exit", "Quit AromaUI Studio", "Alt+F4", AROMA_ICON_CLOSE},
              
    {"Undo", "Undo the last change", "Ctrl+Z", AROMA_ICON_UNDO},
    {"Redo", "Redo the last change", "Ctrl+Y", AROMA_ICON_REDO},
    {"Cut", "Cut the selection", "Ctrl+X", AROMA_ICON_CONTENT_CUT},
    {"Copy", "Copy the selection", "Ctrl+C", AROMA_ICON_CONTENT_COPY},
    {"Paste", "Paste from clipboard", "Ctrl+V", AROMA_ICON_CONTENT_PASTE},
    {"Select All", "Select all content", "Ctrl+A", AROMA_ICON_SELECT_ALL},
    {"Find", "Find in the active file", "Ctrl+F", AROMA_ICON_FIND_IN_PAGE},
    {"Replace", "Find and replace", "Ctrl+H", AROMA_ICON_FIND_REPLACE},
              
    {"Explorer", "Toggle the file explorer sidebar", "Ctrl+B", AROMA_ICON_EXPLORE},
    {"Toggle Panel", "Toggle the bottom panel", "Ctrl+J", AROMA_ICON_DASHBOARD},
    {"Detach Panel", "Move the bottom panel into its own window", "Ctrl+Shift+J", AROMA_ICON_OPEN_IN_NEW},
    {"Detach Sidebar", "Move the sidebar into its own window", "", AROMA_ICON_OPEN_IN_NEW},
    {"Problems", "Show problems and diagnostics", "", AROMA_ICON_ERROR_OUTLINE},
    {"Output", "Show build/output logs", "", AROMA_ICON_DASHBOARD},
            
    {"Go to File...", "Quickly open any file", "Ctrl+P", AROMA_ICON_DESCRIPTION},
    {"Go to Definition", "Jump to symbol definition", "F12", AROMA_ICON_LINK},
    {"Search Workspace", "Search text across all files", "Ctrl+Shift+F", AROMA_ICON_SEARCH},
    {"Next Problem", "Jump to the next problem", "F8", AROMA_ICON_ARROW_DOWNWARD},
    {"Language Server", "LSP status and actions", "", AROMA_ICON_AUTORENEW},
             
    {"Build Project", "Compile the project", "F7", AROMA_ICON_BUILD},
    {"Run Project", "Execute the project", "F5", AROMA_ICON_PLAY_ARROW},
    {"Clean Build", "Clean then build", "", AROMA_ICON_REFRESH},
    {"Target: Linux", "Build and run on the desktop", "", AROMA_ICON_COMPUTER},
    {"Target: Android", "Build and run on an Android device", "", AROMA_ICON_PHONE_ANDROID},
    {"Next Android Device", "Switch the active Android device", "", AROMA_ICON_PHONE_ANDROID},
    {"Android Device Manager", "View and select connected Android devices", "", AROMA_ICON_PHONE_ANDROID},
    {"Refresh Android Devices", "Re-scan connected devices with adb", "", AROMA_ICON_SYNC},
    {"Setup Android Project", "Generate and build the Android target", "", AROMA_ICON_BUILD},
    {"Language Server", "Restart LSP", "", AROMA_ICON_REPLAY},
                          
    {"Settings", "Open IDE settings", "Ctrl+,", AROMA_ICON_SETTINGS},
    {"Command Palette", "Show this command palette", "Ctrl+Shift+P", AROMA_ICON_KEYBOARD},
    {"Recent Commands", "List recently used commands", "", AROMA_ICON_HISTORY},
    {"Changelog", "View the IDE changelog (alpha 0.0.1)", "", AROMA_ICON_DESCRIPTION},
    {"Toggle Sidebar", "Show/hide the file explorer", "Ctrl+B", AROMA_ICON_EXPLORE},
    {"Focus Editor", "Return focus to the code editor", "Esc", AROMA_ICON_OPEN_IN_NEW},
};

#define CMD_COUNT ((int)(sizeof(k_commands) / sizeof(k_commands[0])))

typedef struct LogEntry
{
    char time_text[12];
    char method[44];
    char message[200];
    uint32_t color;
} LogEntry;

typedef enum IdeMenuAction
{
    IDE_ACTION_NONE = 0,
    IDE_ACTION_NEW_FILE,
    IDE_ACTION_OPEN_FILE,
    IDE_ACTION_SAVE,
    IDE_ACTION_SAVE_ALL,
    IDE_ACTION_CLOSE_EDITOR,
    IDE_ACTION_NEW_PROJECT,
    IDE_ACTION_OPEN_PROJECT,
    IDE_ACTION_BUILD,
    IDE_ACTION_RUN,
    IDE_ACTION_CLEAN,
    IDE_ACTION_TARGET_LINUX,
    IDE_ACTION_TARGET_ANDROID,
    IDE_ACTION_ANDROID_DEVICE_NEXT,
    IDE_ACTION_ANDROID_DEVICE_MANAGER,
    IDE_ACTION_ANDROID_REFRESH,
    IDE_ACTION_ANDROID_SETUP,
    IDE_ACTION_EXIT,
    IDE_ACTION_UNDO,
    IDE_ACTION_REDO,
    IDE_ACTION_CUT,
    IDE_ACTION_COPY,
    IDE_ACTION_PASTE,
    IDE_ACTION_SELECT_ALL,
    IDE_ACTION_FIND,
    IDE_ACTION_DEFINITION,
    IDE_ACTION_TOGGLE_SIDEBAR,
    IDE_ACTION_TOGGLE_PANEL,
    IDE_ACTION_TOGGLE_PANEL_DETACH,
    IDE_ACTION_TOGGLE_SIDEBAR_DETACH,
    IDE_ACTION_SHOW_PROBLEMS,
    IDE_ACTION_SHOW_OUTPUT,
    IDE_ACTION_SHOW_TRACE,
    IDE_ACTION_SEARCH_WORKSPACE,
    IDE_ACTION_NEXT_PROBLEM,
    IDE_ACTION_LANGUAGE_SERVER,
    IDE_ACTION_ABOUT,
    IDE_ACTION_SHORTCUTS,
    IDE_ACTION_OPEN_README,
    IDE_ACTION_TOGGLE_COMMENT
} IdeMenuAction;

typedef struct MenuItem
{
    const char *label;
    const char *shortcut;
    IdeMenuAction action;
} MenuItem;

static const MenuItem k_file_menu[] = {
    {"New File",        "Ctrl+N",       IDE_ACTION_NEW_FILE},
    {"Open File...",    "Ctrl+O",       IDE_ACTION_OPEN_FILE},
    {"Save",            "Ctrl+S",       IDE_ACTION_SAVE},
    {"Save All",        "Ctrl+Shift+S", IDE_ACTION_SAVE_ALL},
    {"Close Editor",    "Ctrl+W",       IDE_ACTION_CLOSE_EDITOR},
    {"New Project...",  "Ctrl+Shift+N", IDE_ACTION_NEW_PROJECT},
    {"Open Project...", "Ctrl+Shift+O", IDE_ACTION_OPEN_PROJECT},
    {"Build Project",   "Ctrl+Shift+B", IDE_ACTION_BUILD},
    {"Run Project",     "F5",           IDE_ACTION_RUN},
    {"Exit",            "Alt+F4",       IDE_ACTION_EXIT},
};

static const MenuItem k_edit_menu[] = {
    {"Undo",        "Ctrl+Z", IDE_ACTION_UNDO},
    {"Redo",        "Ctrl+Y", IDE_ACTION_REDO},
    {"Cut",         "Ctrl+X", IDE_ACTION_CUT},
    {"Copy",        "Ctrl+C", IDE_ACTION_COPY},
    {"Paste",       "Ctrl+V", IDE_ACTION_PASTE},
    {"Select All",  "Ctrl+A", IDE_ACTION_SELECT_ALL},
    {"Find",        "Ctrl+F", IDE_ACTION_FIND},
};

static const MenuItem k_selection_menu[] = {
    {"Select All", "Ctrl+A", IDE_ACTION_SELECT_ALL},
    {"Cut",        "Ctrl+X", IDE_ACTION_CUT},
    {"Copy",       "Ctrl+C", IDE_ACTION_COPY},
    {"Paste",      "Ctrl+V", IDE_ACTION_PASTE},
};

static const MenuItem k_view_menu[] = {
    {"Explorer",        "Ctrl+B", IDE_ACTION_TOGGLE_SIDEBAR},
    {"Toggle Panel",    "Ctrl+J", IDE_ACTION_TOGGLE_PANEL},
    {"Detach Panel",    "Ctrl+Shift+J", IDE_ACTION_TOGGLE_PANEL_DETACH},
    {"Detach Sidebar",  "",       IDE_ACTION_TOGGLE_SIDEBAR_DETACH},
    {"Problems",        "",       IDE_ACTION_SHOW_PROBLEMS},
    {"Output",          "",       IDE_ACTION_SHOW_OUTPUT},
    {"LSP Trace",       "",       IDE_ACTION_SHOW_TRACE},
};

static const MenuItem k_go_menu[] = {
    {"Go to File...",      "Ctrl+P",       IDE_ACTION_OPEN_FILE},
    {"Go to Definition",   "F12",          IDE_ACTION_DEFINITION},
    {"Search Workspace",   "Ctrl+Shift+F", IDE_ACTION_SEARCH_WORKSPACE},
    {"Next Problem",       "F8",           IDE_ACTION_NEXT_PROBLEM},
    {"Language Server",    "",             IDE_ACTION_LANGUAGE_SERVER},
};

static const MenuItem k_run_menu[] = {
    {"Build Project",          "F7", IDE_ACTION_BUILD},
    {"Run Project",            "F5", IDE_ACTION_RUN},
    {"Clean Build",            "",   IDE_ACTION_CLEAN},
    {"Target: Linux",          "",   IDE_ACTION_TARGET_LINUX},
    {"Target: Android",        "",   IDE_ACTION_TARGET_ANDROID},
    {"Next Android Device",    "",   IDE_ACTION_ANDROID_DEVICE_NEXT},
    {"Android Device Manager", "", IDE_ACTION_ANDROID_DEVICE_MANAGER},
    {"Refresh Android Devices","",   IDE_ACTION_ANDROID_REFRESH},
    {"Setup Android Project",  "",   IDE_ACTION_ANDROID_SETUP},
    {"Language Server",        "",   IDE_ACTION_LANGUAGE_SERVER},
};

static const MenuItem k_help_menu[] = {
    {"About AromaUI Studio", "", IDE_ACTION_ABOUT},
    {"Keyboard Shortcuts",   "", IDE_ACTION_SHORTCUTS},
    {"Open README",          "", IDE_ACTION_OPEN_README},
};

static const MenuItem *const k_menu_items[6] = {
    k_file_menu, k_edit_menu, k_selection_menu, k_view_menu,
    k_go_menu, k_run_menu, k_help_menu};

static const int k_menu_item_counts[6] = {10, 7, 4, 7, 5, 9};
static const char *const k_menus[6] = { "File", "Edit", "Selection", "View", "Go", "Run"};
static const int k_menu_w[6] = {34, 32, 54, 32, 18, 24};   
static const char *const k_panel_tabs[3] = {"PROBLEMS", "OUTPUT", "LSP TRACE"};
#define TITLE_MENU_COUNT ((int)(sizeof(k_menus) / sizeof(k_menus[0])))
static const char *const k_output_lines[] = {
    "AromaUI Studio 0.1.0  ·  Aroma project workspace",
    "[init] aroma-language-server ready in 24 ms",
    "[init] Incense schema v2 loaded",
    "[init] C11 parser indexed the project",
    "[watcher] project folder opened read/write",
    "[LSP] textDocument/didOpen published",
    "[build] No build has run in this session",
    "[hint] Use Run > Build Project or the explorer task list",
};

static const char *const k_terminal_lines[] = {
    "AromaUI Studio terminal",
    "$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug",
    "-- Configuring done",
    "-- Generating done",
    "$ cmake --build build -j",
    "[100%] Built target aroma_notes",
    "$ ./build/aroma_notes",
    "$ ",
};

static AromaNode *s_root;
static AromaNode *s_title_canvas;
static AromaNode *s_rail_canvas;
static AromaNode *s_sidebar_canvas;
static AromaNode *s_tabs_canvas;
static AromaNode *s_breadcrumb_canvas;
static AromaNode *s_outline_canvas;
static AromaNode *s_panel_canvas;
static AromaNode *s_status_canvas;
static AromaNode *s_editor_bg;
static AromaNode *s_settings_canvas;
static AromaNode *s_panel_bg;

static int s_outline_scroll;
static int s_outline_ctrl_hover = -1;
static int s_title_ctrl_hover = -1;
static int s_title_search_hover;
static uint64_t s_title_last_click_ms;
static bool s_title_maximized;
static bool s_title_fullscreen;
static uint64_t s_home_last_click_ms;
static bool s_home_maximized;
static bool s_home_fullscreen;
static bool s_custom_window_maximized;
static AromaNode *s_home_window_menu_canvas;
static int s_home_window_menu_x;

                      
static AromaNode *s_card_title_bg;
static AromaNode *s_card_rail_bg;
static AromaNode *s_card_sidebar_bg;
static AromaNode *s_card_tabs_bg;
static AromaNode *s_card_breadcrumb_bg;
static AromaNode *s_card_editor_bg;
static AromaNode *s_card_outline_bg;
static AromaNode *s_card_panel_bg;
static AromaNode *s_card_status_bg;

                   
static AromaNode *s_div_title_bottom;
static AromaNode *s_div_rail_right;
static AromaNode *s_div_outline_left;
static AromaNode *s_div_panel_top;
static AromaNode *s_div_status_top;
static AromaNode *s_menu_canvas;
static AromaNode *s_dialog_canvas;
static AromaNode *s_toast_canvas;
static AromaNode *s_editor;
static AromaTheme s_ide_theme;

static AromaNode *s_markdown_webview;
static bool s_markdown_visible;

static AromaWindow *s_changelog_window;
static bool s_changelog_visible;

                                                        
static const char *k_changelog_md =
    "# AromaUI Studio Changelog\n\n"
    "## [0.0.1] - Alpha\n\n"
    "### Added\n"
    "- Drag-and-drop UI designer with live property panel\n"
    "- Code editor with syntax highlighting and completions\n"
    "- Project workspace with file tree and multi-tab editing\n"
    "- Home screen: open/create projects, recent projects, changelog\n"
    "- Native file dialogs (zenity/kdialog via NFD) for open/save/pick-folder\n"
    "- X11/GLPS windowing backend with keyboard and pointer input\n"
    "- Markdown preview for `.md` files\n"
    "- Language server (clangd-style) integration for C/C++\n"
    "- Build/run/clean commands from the command palette\n"
    "- Material-inspired theming with icons\n\n"
    "### Fixed\n"
    "- Enter key now queues correctly on X11 (keysym and raw keycode paths)\n"
    "- Dialog Enter/Escape handling across home and editor screens\n\n"
    "### Known Issues\n"
    "- Native file dialogs require zenity or kdialog on Linux\n"
    "- Webview preview requires a working display server\n";

static bool s_designer_visible;
static bool s_designer_available;
static bool s_designer_committing;
static IdeDesignDocument s_design_doc;
static char s_design_error[192];
static bool s_home_mode;
static AromaNode *s_home_canvas;
static char s_home_project_path[PATH_MAX];
static char s_home_error[256];
static char s_home_recent_project_path[PATH_MAX];
static char s_home_recent_project_name[128];
static bool s_home_has_recent_project;
static AromaNode *s_home_host;
static bool s_home_dirty;
static int s_home_hover = -1;
static int s_home_drawn_w, s_home_drawn_h;
static int s_home_pending_dialog = 0;
static AromaNode *s_designer_toolbar;
static AromaNode *s_designer_palette;
static AromaNode *s_designer_canvas;
static AromaNode *s_designer_preview;
static AromaNode *s_designer_overlay;
static IncenseRegistry *s_designer_preview_registry;
static bool s_designer_preview_ok;
static bool s_designer_preview_dirty = true;
static bool s_designer_preview_refreshing;
static bool s_designer_dragging;
static int s_designer_preview_scale;
static int s_designer_palette_hover;
static int s_designer_tool_hover;
static int s_designer_drag_palette;
static int s_designer_drag_node;
static int s_designer_drag_offset_x;
static int s_designer_drag_offset_y;
static int s_designer_drag_page_x;
static int s_designer_drag_page_y;
static bool s_designer_drag_page_valid;
static bool s_designer_drag_moved;
static int s_designer_resize_node;
static int s_designer_resize_corner;
static int s_designer_resize_start_x;
static int s_designer_resize_start_y;
static int s_designer_resize_orig_x;
static int s_designer_resize_orig_y;
static int s_designer_resize_start_w;
static int s_designer_resize_start_h;
static bool s_designer_resizing;
static uint64_t s_designer_drag_last_update;
static uint64_t s_designer_last_click_time = 0;
static int s_designer_last_click_node = -1;
static int s_designer_palette_scroll;
static char *s_design_undo[16];
static int s_design_undo_count;
static int s_design_undo_pos;
static char *s_design_clipboard;
static int s_prop_edit_index;
static int s_prop_edit_row;
static char s_prop_edit_buf[256];
static bool s_prop_edit_active;
static int s_prop_dropdown_index;
static int s_prop_dropdown_row;
static bool s_prop_dropdown_open;
static int s_prop_dropdown_extra_h;
static AromaNode *s_prop_row_widgets[48];
static AromaNode *s_prop_label_widgets[48];
static AromaNode *s_prop_value_widgets[48];
static AromaNode *s_prop_dropdown_widgets[48];
static int s_prop_dropdown_widget_count[48];
static AromaNode *s_prop_category_widgets[16];
static int s_prop_last_selected = -1;
static int s_prop_last_panel_y = 0;
static int s_prop_cat_count;
static struct PropCategory
{
    const char *name;
    int start_row;
    int row_count;
    bool expanded;
} s_prop_cats[16];

                                                  
static const char *const k_prop_type_button[]     = {"filled", "tonal", "outlined", "text", "elevated"};
static const char *const k_prop_type_card[]       = {"filled", "elevated", "outlined", "glass"};
static const char *const k_prop_type_chip[]       = {"assist", "filter", "input", "suggestion"};
static const char *const k_prop_type_progress[]   = {"determinate", "indeterminate"};
static const char *const k_prop_type_dialog[]     = {"basic", "fullscreen"};
static const char *const k_prop_variant_ib[]      = {"standard", "filled", "tonal", "outlined"};
static const char *const k_prop_style_label[]     = {"large", "medium", "small"};
static const char *const k_prop_scale[]           = {"fill", "fit", "cover"};
static const char *const k_prop_layout[]          = {"flex", "grid"};
static const char *const k_prop_direction[]       = {"row", "column"};
static const char *const k_prop_orientation[]     = {"horizontal", "vertical"};
static const char *const k_prop_bool[]            = {"false", "true"};
static const char *const k_prop_tabs_variant[]    = {"top", "bar"};
static const char *const k_prop_tooltip_pos[]     = {"top", "bottom", "left", "right"};
static const char *const k_prop_position[]        = {"top", "bottom", "left", "right"};
static const char *const k_prop_anim[]            = {"fade", "slide_x", "slide_y", "scale_x", "scale_y"};
static const char *const k_prop_easing[]          = {"ease_out_cubic", "ease_out_back", "ease_in_out", "linear"};
static const char *const k_prop_pack[]            = {"", "lucide"};
static const char *const k_prop_icon[]            = {
    "AROMA_ICON_ADD", "AROMA_ICON_ARROW_BACK", "AROMA_ICON_ARROW_FORWARD",
    "AROMA_ICON_CHECK", "AROMA_ICON_CLOSE", "AROMA_ICON_DELETE",
    "AROMA_ICON_EDIT", "AROMA_ICON_EMAIL", "AROMA_ICON_FAVORITE",
    "AROMA_ICON_FOLDER", "AROMA_ICON_HOME", "AROMA_ICON_INFO",
    "AROMA_ICON_MENU", "AROMA_ICON_MORE_HORIZ", "AROMA_ICON_NOTIFICATIONS",
    "AROMA_ICON_PLAY_ARROW", "AROMA_ICON_REFRESH", "AROMA_ICON_SAVE",
    "AROMA_ICON_SEARCH", "AROMA_ICON_SETTINGS", "AROMA_ICON_STAR",
    "AROMA_ICON_WARNING"
};


static void ide_update_panel_rects(void)
{
    if (s_panel_detached)
        return;
    if (s_panel_bg)
    {
        AromaRect *r = aroma_node_get_rect(s_panel_bg);
        if (r)
        {
            r->x = SIDEBAR_X;
            r->y = panel_y;
            r->width = panel_w;
            r->height = panel_h;
        }
    }
    if (s_panel_canvas)
    {
        AromaRect *r = aroma_node_get_rect(s_panel_canvas);
        if (r)
        {
            r->x = SIDEBAR_X;
            r->y = panel_y;
            r->width = panel_w;
            r->height = panel_h;
        }
    }
}

static const char *const *prop_dropdown_options(const char *widget_tag,
                                                const char *prop_key,
                                                int *count)
{
    *count = 0;

                                          
    if (strcmp(prop_key, "checked")       == 0 ||
        strcmp(prop_key, "value")         == 0 ||
        strcmp(prop_key, "autoplay")      == 0 ||
        strcmp(prop_key, "loop")          == 0 ||
        strcmp(prop_key, "muted")         == 0 ||
        strcmp(prop_key, "controls")      == 0 ||
        strcmp(prop_key, "interactive")   == 0 ||
        strcmp(prop_key, "auto_rotate")   == 0 ||
        strcmp(prop_key, "attribution")   == 0 ||
        strcmp(prop_key, "selected")      == 0 ||
        strcmp(prop_key, "visible")       == 0 ||
        strcmp(prop_key, "hidden")        == 0 ||
        strcmp(prop_key, "grid")          == 0 ||
        strcmp(prop_key, "frame")         == 0 ||
        strcmp(prop_key, "wrap")          == 0)
    {
        *count = 2;
        return k_prop_bool;
    }

                         
    if (strcmp(prop_key, "type") == 0)
    {
        if (strcmp(widget_tag, "Button") == 0)
        { *count = 5; return k_prop_type_button; }
        if (strcmp(widget_tag, "Card")   == 0 ||
            strcmp(widget_tag, "Panel")  == 0)
        { *count = 4; return k_prop_type_card; }
        if (strcmp(widget_tag, "Chip")   == 0)
        { *count = 4; return k_prop_type_chip; }
        if (strcmp(widget_tag, "ProgressBar") == 0)
        { *count = 2; return k_prop_type_progress; }
        if (strcmp(widget_tag, "Dialog") == 0)
        { *count = 2; return k_prop_type_dialog; }
        if (strcmp(widget_tag, "Tabs") == 0)
        { *count = 2; return k_prop_tabs_variant; }
        if (strcmp(widget_tag, "SegmentedControl") == 0)
        { *count = 2; return k_prop_tabs_variant; }
        if (strcmp(widget_tag, "Checkbox") == 0 ||
            strcmp(widget_tag, "Switch") == 0 ||
            strcmp(widget_tag, "RadioButton") == 0)
        { *count = 2; return k_prop_bool; }
        if (strcmp(widget_tag, "Slider") == 0 ||
            strcmp(widget_tag, "Gauge") == 0 ||
            strcmp(widget_tag, "ProgressBar") == 0)
        { *count = 2; return k_prop_layout; }
    }

                           
    if (strcmp(prop_key, "variant") == 0)
    { *count = 4; return k_prop_variant_ib; }

                         
    if (strcmp(prop_key, "style") == 0)
    { *count = 3; return k_prop_style_label; }

                         
    if (strcmp(prop_key, "scale") == 0)
    { *count = 3; return k_prop_scale; }

                                                    
    if (strcmp(prop_key, "layout") == 0)
    { *count = 2; return k_prop_layout; }
    if (strcmp(prop_key, "direction") == 0)
    { *count = 2; return k_prop_direction; }
    if (strcmp(prop_key, "orientation") == 0)
    { *count = 2; return k_prop_orientation; }

                                      
    if (strcmp(prop_key, "position") == 0)
    { *count = 4; return k_prop_tooltip_pos; }

                             
    if (strcmp(prop_key, "animation") == 0)
    { *count = 5; return k_prop_anim; }
    if (strcmp(prop_key, "animation_easing") == 0)
    { *count = 4; return k_prop_easing; }

                                    
    if (strcmp(prop_key, "pack") == 0)
    { *count = 2; return k_prop_pack; }
    if (strcmp(prop_key, "icon") == 0)
    { *count = (int)(sizeof(k_prop_icon) / sizeof(k_prop_icon[0])); return k_prop_icon; }

    return NULL;
}

typedef struct PropSchemaEntry
{
    const char *key;
    const char *label;
    const char *category;
    bool is_dropdown;
    bool is_numeric;
    bool is_color;
} PropSchemaEntry;

typedef struct PropCategoryEntry
{
    const char *name;
    int start_idx;
    int count;
} PropCategoryEntry;

static const PropSchemaEntry k_prop_schema_all[] = {
                
    {"x", "X", "Layout", false, true, false},
    {"y", "Y", "Layout", false, true, false},
    {"width", "Width", "Layout", false, true, false},
    {"height", "Height", "Layout", false, true, false},
    {"layout", "Layout", "Layout", true, false, false},
    {"direction", "Direction", "Layout", true, false, false},
    {"orientation", "Orientation", "Layout", true, false, false},
    {"gap", "Gap", "Layout", false, true, false},
    {"cols", "Cols", "Layout", false, true, false},
    {"rows", "Rows", "Layout", false, true, false},
                    
    {"text", "Text", "Appearance", false, false, false},
    {"color", "Color", "Appearance", false, false, true},
    {"type", "Type", "Appearance", true, false, false},
    {"variant", "Variant", "Appearance", true, false, false},
    {"style", "Style", "Appearance", true, false, false},
    {"scale", "Scale", "Appearance", true, false, false},
    {"corner_radius", "Corner Radius", "Appearance", false, true, false},
    {"border_color", "Border Color", "Appearance", false, false, true},
    {"bg_color", "Background Color", "Appearance", false, false, true},
    {"text_color", "Text Color", "Appearance", false, false, true},
                  
    {"checked", "Checked", "Behavior", true, false, false},
    {"value", "Value", "Behavior", true, false, false},
    {"autoplay", "Autoplay", "Behavior", true, false, false},
    {"loop", "Loop", "Behavior", true, false, false},
    {"muted", "Muted", "Behavior", true, false, false},
    {"controls", "Controls", "Behavior", true, false, false},
    {"interactive", "Interactive", "Behavior", true, false, false},
    {"auto_rotate", "Auto Rotate", "Behavior", true, false, false},
    {"visible", "Visible", "Behavior", true, false, false},
    {"hidden", "Hidden", "Behavior", true, false, false},
    {"selected", "Selected", "Behavior", true, false, false},
    {"wrap", "Wrap", "Behavior", true, false, false},
              
    {"placeholder", "Placeholder", "Text", false, false, false},
    {"font", "Font", "Text", false, false, false},
    {"size", "Size", "Text", false, true, false},
                     
    {"on_click", "On Click", "Interaction", false, false, false},
    {"on_change", "On Change", "Interaction", false, false, false},
    {"on_select", "On Select", "Interaction", false, false, false},
    {"on_submit", "On Submit", "Interaction", false, false, false},
                  
    {"z_index", "Z Index", "Advanced", false, true, false},
    {"pack", "Icon Pack", "Advanced", true, false, false},
    {"icon", "Icon", "Advanced", false, false, false},
    {"icon_size", "Icon Size", "Advanced", false, true, false},
    {"animation", "Animation", "Advanced", true, false, false},
    {"animation_easing", "Easing", "Advanced", true, false, false},
    {"position", "Position", "Advanced", true, false, false},
                         
    {"src", "Source", "Media", false, false, false},
    {"@embed", "Embed", "Media", false, false, false},
    {"model", "Model", "Media", false, false, false},
               
    {"min", "Min", "Range", false, true, false},
    {"max", "Max", "Range", false, true, false},
    {"step", "Step", "Range", false, true, false},
    {"content_width", "Content Width", "Layout", false, true, false},
    {"content_height", "Content Height", "Layout", false, true, false},
    {"columns", "Columns", "Layout", false, true, false},
    {"length", "Length", "Layout", false, true, false},
    {"bottom_padding", "Bottom Padding", "Layout", false, true, false},
    {"text_scale", "Text Scale", "Appearance", false, true, false},
    {"blur_radius", "Blur Radius", "Appearance", false, true, false},
    {"hover_lift", "Hover Lift", "Appearance", false, true, false},
    {"press_lift", "Press Lift", "Appearance", false, true, false},
    {"track_color", "Track Color", "Appearance", false, false, true},
    {"indicator_color", "Indicator Color", "Appearance", false, false, true},
    {"icon_color", "Icon Color", "Appearance", false, false, true},
    {"fill_color", "Fill Color", "Appearance", false, false, true},
    {"frame_color", "Frame Color", "Appearance", false, false, true},
    {"grid_color", "Grid Color", "Appearance", false, false, true},
    {"line_color", "Line Color", "Appearance", false, false, true},
    {"tick_color", "Tick Color", "Appearance", false, false, true},
    {"needle_color", "Needle Color", "Appearance", false, false, true},
    {"hub_color", "Hub Color", "Appearance", false, false, true},
    {"tint", "Tint", "Appearance", false, false, true},
    {"message", "Message", "Text", false, false, false},
    {"label", "Label", "Text", false, false, false},
    {"title", "Title", "Text", false, false, false},
    {"action", "Action", "Interaction", false, false, false},
    {"on_swipe_left", "On Swipe Left", "Interaction", false, false, false},
    {"on_swipe_right", "On Swipe Right", "Interaction", false, false, false},
    {"duration", "Duration", "Behavior", false, true, false},
    {"show", "Show", "Behavior", true, false, false},
    {"popup", "Popup", "Behavior", true, false, false},
    {"wrap", "Wrap", "Behavior", true, false, false},
    {"group", "Group", "Behavior", false, false, false},
    {"year", "Year", "Date and Time", false, true, false},
    {"month", "Month", "Date and Time", false, true, false},
    {"day", "Day", "Date and Time", false, true, false},
    {"hour", "Hour", "Date and Time", false, true, false},
    {"minute", "Minute", "Date and Time", false, true, false},
    {"mode", "Mode", "Date and Time", true, false, false},
    {"lat", "Latitude", "Map", false, true, false},
    {"lon", "Longitude", "Map", false, true, false},
    {"zoom", "Zoom", "Map", false, true, false},
    {"attribution", "Attribution", "Map", true, false, false},
    {"route_width", "Route Width", "Map", false, true, false},
    {"model", "Model", "Media", false, false, false},
    {"radius", "Radius", "Advanced", false, true, false},
    {"thickness", "Thickness", "Advanced", false, true, false},
    {"theta", "Theta", "Advanced", false, true, false},
    {"phi", "Phi", "Advanced", false, true, false},
    {"src", "Source", "Media", false, false, false},
    {"cam_theta", "Camera Theta", "Advanced", false, true, false},
    {"cam_phi", "Camera Phi", "Advanced", false, true, false},
    {"cam_radius", "Camera Radius", "Advanced", false, true, false},
    {"fov", "Field of View", "Advanced", false, true, false},
    {"near", "Near Plane", "Advanced", false, true, false},
    {"far", "Far Plane", "Advanced", false, true, false},
    {"target_x", "Target X", "Advanced", false, true, false},
    {"target_y", "Target Y", "Advanced", false, true, false},
    {"target_z", "Target Z", "Advanced", false, true, false},
    {"cam_target_x", "Camera Target X", "Advanced", false, true, false},
    {"cam_target_y", "Camera Target Y", "Advanced", false, true, false},
    {"cam_target_z", "Camera Target Z", "Advanced", false, true, false},
    {"series", "Series", "Chart", false, false, false},
    {"points", "Points", "Chart", false, false, false},
    {"lines", "Lines", "Chart", false, false, false},
    {"x_labels", "X Labels", "Chart", false, false, false},
    {"y_labels", "Y Labels", "Chart", false, false, false},
    {"x_min", "X Min", "Chart", false, true, false},
    {"x_max", "X Max", "Chart", false, true, false},
    {"y_min", "Y Min", "Chart", false, true, false},
    {"y_max", "Y Max", "Chart", false, true, false},
    {"grid", "Grid", "Chart", true, false, false},
    {"frame", "Frame", "Chart", true, false, false},
    {"line", "Line", "Chart", true, false, false},
    {"point_type", "Point Type", "Chart", true, false, false},
    {"bar_gap", "Bar Gap", "Chart", false, true, false},
    {"pad_left", "Padding Left", "Chart", false, true, false},
    {"pad_right", "Padding Right", "Chart", false, true, false},
    {"pad_top", "Padding Top", "Chart", false, true, false},
    {"pad_bottom", "Padding Bottom", "Chart", false, true, false},
    {"line_thickness", "Line Thickness", "Chart", false, true, false},
    {"size", "Size", "Chart", false, true, false},
    {"colors", "Colors", "Chart", false, false, false},
    {"series_colors", "Series Colors", "Chart", false, false, false},
    {"series_lines", "Series Lines", "Chart", false, false, false},
    {"series_points", "Series Points", "Chart", false, false, false},
    {"series_thickness", "Series Thickness", "Chart", false, false, false},
    {"thicknesses", "Thicknesses", "Chart", false, false, false},
};

#define PROP_SCHEMA_COUNT (sizeof(k_prop_schema_all) / sizeof(k_prop_schema_all[0]))

                                                                                     
typedef struct WidgetPropMap
{
    const char *tag;
    const char *const *keys;
    int count;
} WidgetPropMap;

                                            
static const char *const k_core_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};

                                   
static const char *const k_button_props[] = {
    "x", "y", "width", "height", "text", "type", "variant", "style",
    "color", "corner_radius", "icon", "icon_size", "pack",
    "checked", "visible", "hidden", "z_index", "on_click"};
static const char *const k_label_props[] = {
    "x", "y", "width", "height", "text", "style", "color", "text_color",
    "font", "size", "wrap", "visible", "hidden", "z_index"};
static const char *const k_container_props[] = {
    "x", "y", "width", "height", "layout", "direction", "gap",
    "color", "corner_radius", "visible", "hidden", "z_index"};
static const char *const k_card_props[] = {
    "x", "y", "width", "height", "type", "corner_radius", "color",
    "border_color", "visible", "hidden", "z_index"};
static const char *const k_input_props[] = {
    "x", "y", "width", "height", "text", "placeholder", "type",
    "checked", "value", "visible", "hidden", "z_index", "on_change"};
static const char *const k_slider_props[] = {
    "x", "y", "width", "height", "value", "min", "max", "orientation",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_progress_props[] = {
    "x", "y", "width", "height", "type", "value", "min", "max",
    "visible", "hidden", "z_index"};
static const char *const k_tabs_props[] = {
    "x", "y", "width", "height", "type", "variant", "visible",
    "hidden", "z_index", "on_select"};
static const char *const k_scrollview_props[] = {
    "x", "y", "width", "height", "direction", "visible", "hidden", "z_index"};
static const char *const k_list_props[] = {
    "x", "y", "width", "height", "direction", "visible", "hidden", "z_index"};
static const char *const k_divider_props[] = {
    "x", "y", "width", "height", "orientation", "color",
    "visible", "hidden", "z_index"};
static const char *const k_icon_props[] = {
    "x", "y", "width", "height", "pack", "icon", "icon_size", "color",
    "visible", "hidden", "z_index"};
static const char *const k_image_props[] = {
    "x", "y", "width", "height", "src", "scale", "visible", "hidden", "z_index"};
static const char *const k_dropdown_props[] = {
    "x", "y", "width", "height", "text", "type", "value",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_switch_props[] = {
    "x", "y", "width", "height", "checked", "color",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_checkbox_props[] = {
    "x", "y", "width", "height", "checked", "color",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_radio_props[] = {
    "x", "y", "width", "height", "checked", "color",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_chip_props[] = {
    "x", "y", "width", "height", "text", "type", "color",
    "visible", "hidden", "z_index", "on_click"};
static const char *const k_dialog_props[] = {
    "x", "y", "width", "height", "type", "visible", "hidden", "z_index"};
static const char *const k_tooltip_props[] = {
    "x", "y", "width", "height", "text", "position", "visible",
    "hidden", "z_index"};
static const char *const k_gauge_props[] = {
    "x", "y", "width", "height", "value", "min", "max", "type",
    "visible", "hidden", "z_index"};
static const char *const k_loading_props[] = {
    "x", "y", "width", "height", "autoplay", "visible", "hidden", "z_index"};
static const char *const k_table_props[] = {
    "x", "y", "width", "height", "cols", "rows", "visible", "hidden", "z_index"};
static const char *const k_canvas_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_webview_props[] = {
    "x", "y", "width", "height", "src", "visible", "hidden", "z_index"};
static const char *const k_video_props[] = {
    "x", "y", "width", "height", "src", "autoplay", "loop", "muted",
    "controls", "visible", "hidden", "z_index"};
static const char *const k_calendar_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_datepicker_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_timepicker_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_stepper_props[] = {
    "x", "y", "width", "height", "value", "min", "max", "step",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_segmented_props[] = {
    "x", "y", "width", "height", "type", "variant", "value",
    "visible", "hidden", "z_index", "on_change"};
static const char *const k_sidebar_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_menu_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_carousel_props[] = {
    "x", "y", "width", "height", "autoplay", "visible", "hidden", "z_index"};
static const char *const k_map_props[] = {
    "x", "y", "width", "height", "visible", "hidden", "z_index"};
static const char *const k_3d_props[] = {
    "x", "y", "width", "height", "model", "auto_rotate", "interactive",
    "visible", "hidden", "z_index"};
static const char *const k_chart_props[] = {
    "x", "y", "width", "height", "type", "visible", "hidden", "z_index"};
static const char *const k_snackbar_props[] = {
    "x", "y", "width", "height", "text", "visible", "hidden", "z_index"};

static const char *const k_embed_props[] = {
    "x", "y", "width", "height", "@embed", "visible", "hidden", "z_index"};

static const WidgetPropMap k_widget_prop_map[] = {
    {"Button", k_button_props, (int)(sizeof(k_button_props) / sizeof(k_button_props[0]))},
    {"Label", k_label_props, (int)(sizeof(k_label_props) / sizeof(k_label_props[0]))},
    {"Container", k_container_props, (int)(sizeof(k_container_props) / sizeof(k_container_props[0]))},
    {"Card", k_card_props, (int)(sizeof(k_card_props) / sizeof(k_card_props[0]))},
    {"Panel", k_card_props, (int)(sizeof(k_card_props) / sizeof(k_card_props[0]))},
    {"Textbox", k_input_props, (int)(sizeof(k_input_props) / sizeof(k_input_props[0]))},
    {"Switch", k_switch_props, (int)(sizeof(k_switch_props) / sizeof(k_switch_props[0]))},
    {"Checkbox", k_checkbox_props, (int)(sizeof(k_checkbox_props) / sizeof(k_checkbox_props[0]))},
    {"RadioButton", k_radio_props, (int)(sizeof(k_radio_props) / sizeof(k_radio_props[0]))},
    {"Slider", k_slider_props, (int)(sizeof(k_slider_props) / sizeof(k_slider_props[0]))},
    {"ProgressBar", k_progress_props, (int)(sizeof(k_progress_props) / sizeof(k_progress_props[0]))},
    {"Tabs", k_tabs_props, (int)(sizeof(k_tabs_props) / sizeof(k_tabs_props[0]))},
    {"SegmentedControl", k_segmented_props, (int)(sizeof(k_segmented_props) / sizeof(k_segmented_props[0]))},
    {"ScrollView", k_scrollview_props, (int)(sizeof(k_scrollview_props) / sizeof(k_scrollview_props[0]))},
    {"ListView", k_list_props, (int)(sizeof(k_list_props) / sizeof(k_list_props[0]))},
    {"Divider", k_divider_props, (int)(sizeof(k_divider_props) / sizeof(k_divider_props[0]))},
    {"Icon", k_icon_props, (int)(sizeof(k_icon_props) / sizeof(k_icon_props[0]))},
    {"Image", k_image_props, (int)(sizeof(k_image_props) / sizeof(k_image_props[0]))},
    {"Dropdown", k_dropdown_props, (int)(sizeof(k_dropdown_props) / sizeof(k_dropdown_props[0]))},
    {"Chip", k_chip_props, (int)(sizeof(k_chip_props) / sizeof(k_chip_props[0]))},
    {"Dialog", k_dialog_props, (int)(sizeof(k_dialog_props) / sizeof(k_dialog_props[0]))},
    {"Tooltip", k_tooltip_props, (int)(sizeof(k_tooltip_props) / sizeof(k_tooltip_props[0]))},
    {"Gauge", k_gauge_props, (int)(sizeof(k_gauge_props) / sizeof(k_gauge_props[0]))},
    {"Loading", k_loading_props, (int)(sizeof(k_loading_props) / sizeof(k_loading_props[0]))},
    {"Table", k_table_props, (int)(sizeof(k_table_props) / sizeof(k_table_props[0]))},
    {"Canvas", k_canvas_props, (int)(sizeof(k_canvas_props) / sizeof(k_canvas_props[0]))},
    {"WebView", k_webview_props, (int)(sizeof(k_webview_props) / sizeof(k_webview_props[0]))},
    {"Video", k_video_props, (int)(sizeof(k_video_props) / sizeof(k_video_props[0]))},
    {"Calendar", k_calendar_props, (int)(sizeof(k_calendar_props) / sizeof(k_calendar_props[0]))},
    {"DatePicker", k_datepicker_props, (int)(sizeof(k_datepicker_props) / sizeof(k_datepicker_props[0]))},
    {"TimePicker", k_timepicker_props, (int)(sizeof(k_timepicker_props) / sizeof(k_timepicker_props[0]))},
    {"Stepper", k_stepper_props, (int)(sizeof(k_stepper_props) / sizeof(k_stepper_props[0]))},
    {"Sidebar", k_sidebar_props, (int)(sizeof(k_sidebar_props) / sizeof(k_sidebar_props[0]))},
    {"Menu", k_menu_props, (int)(sizeof(k_menu_props) / sizeof(k_menu_props[0]))},
    {"Carousel", k_carousel_props, (int)(sizeof(k_carousel_props) / sizeof(k_carousel_props[0]))},
    {"Map", k_map_props, (int)(sizeof(k_map_props) / sizeof(k_map_props[0]))},
    {"ThreeDViewer", k_3d_props, (int)(sizeof(k_3d_props) / sizeof(k_3d_props[0]))},
    {"Chart", k_chart_props, (int)(sizeof(k_chart_props) / sizeof(k_chart_props[0]))},
    {"Snackbar", k_snackbar_props, (int)(sizeof(k_snackbar_props) / sizeof(k_snackbar_props[0]))},
    {"Embed", k_embed_props, (int)(sizeof(k_embed_props) / sizeof(k_embed_props[0]))},
};

#define WIDGET_PROP_MAP_COUNT (sizeof(k_widget_prop_map) / sizeof(k_widget_prop_map[0]))

                                                                        
                                                                             
                                          
static const char *const k_exact_common[] = {
    "x", "y", "width", "height", "visible", "z_index"};
static const char *const k_exact_container[] = {
    "x", "y", "width", "height", "layout", "direction", "gap", "rows", "cols",
    "content_width", "content_height", "on_swipe_left", "on_swipe_right",
    "visible", "z_index"};
static const char *const k_exact_button[] = {
    "x", "y", "width", "height", "text", "type", "color", "text_color",
    "icon", "icon_size", "pack", "size", "on_click", "visible", "z_index"};
static const char *const k_exact_label[] = {
    "x", "y", "width", "height", "text", "style", "color", "font", "size",
    "visible", "z_index"};
static const char *const k_exact_input[] = {
    "x", "y", "width", "height", "placeholder", "text", "on_change",
    "on_submit", "visible", "z_index"};
static const char *const k_exact_switch[] = {
    "x", "y", "width", "height", "value", "on_change", "visible", "z_index"};
static const char *const k_exact_checkbox[] = {
    "x", "y", "width", "height", "label", "checked", "on_change", "visible",
    "z_index"};
static const char *const k_exact_slider[] = {
    "x", "y", "width", "height", "min", "max", "value", "on_change", "visible",
    "z_index"};
static const char *const k_exact_progress[] = {
    "x", "y", "width", "height", "value", "track_color", "indicator_color",
    "visible", "z_index"};
static const char *const k_exact_card[] = {
    "x", "y", "width", "height", "type", "color", "border_color",
    "corner_radius", "blur_radius", "interactive", "hover_lift", "press_lift",
    "visible", "z_index"};
static const char *const k_exact_icon[] = {
    "x", "y", "width", "height", "text", "icon", "pack", "icon_size", "size",
    "color", "icon_color", "on_click", "visible", "z_index"};
static const char *const k_exact_iconbutton[] = {
    "x", "y", "width", "height", "icon", "pack", "icon_size", "size", "color",
    "icon_color", "variant", "on_click", "visible", "z_index"};
static const char *const k_exact_media[] = {
    "x", "y", "width", "height", "src", "corner_radius", "scale", "visible",
    "z_index"};
static const char *const k_exact_video[] = {
    "x", "y", "width", "height", "src", "autoplay", "loop", "muted",
    "controls", "click_to_toggle", "corner_radius", "visible", "z_index"};
static const char *const k_exact_gif[] = {
    "x", "y", "width", "height", "src", "autoplay", "radius", "corner_radius",
    "visible", "z_index"};
static const char *const k_exact_dropdown[] = {
    "x", "y", "width", "height", "text", "value", "max_visible", "on_change",
    "visible", "z_index"};
static const char *const k_exact_display[] = {
    "x", "y", "width", "height", "value", "min", "max", "type", "color",
    "radius", "thickness", "visible", "z_index"};
static const char *const k_exact_gauge[] = {
    "x", "y", "width", "height", "value", "min", "max", "start_angle",
    "end_angle", "ticks", "major_ticks", "minor_ticks", "major_length",
    "minor_length", "tick_color", "tick_thickness", "track_color",
    "track_thickness", "fill_color", "fill_thickness", "needle",
    "needle_color", "needle_thickness", "hub", "hub_color", "hub_radius",
    "hub_thickness", "ring_inset", "visible", "z_index"};
static const char *const k_exact_divider[] = {
    "x", "y", "length", "orientation", "color", "visible", "z_index"};
static const char *const k_exact_list[] = {
    "x", "y", "width", "height", "direction", "icon", "bottom_padding",
    "on_select", "visible", "z_index"};
static const char *const k_exact_dialog[] = {
    "x", "y", "width", "height", "title", "message", "type", "on_click",
    "visible", "z_index"};
static const char *const k_exact_tooltip[] = {
    "x", "y", "text", "position", "visible", "z_index"};
static const char *const k_exact_chip[] = {
    "x", "y", "label", "type", "icon", "selected", "on_click", "visible",
    "z_index"};
static const char *const k_exact_radio[] = {
    "x", "y", "width", "height", "label", "group", "selected", "on_click",
    "visible", "z_index"};
static const char *const k_exact_tabs[] = {
    "x", "y", "width", "height", "selected", "variant", "icon", "on_change",
    "on_select", "visible", "z_index"};
static const char *const k_exact_segmented[] = {
    "x", "y", "width", "height", "selected", "on_change", "visible", "z_index"};
static const char *const k_exact_carousel[] = {
    "x", "y", "width", "height", "selected", "on_change", "visible", "z_index"};
static const char *const k_exact_sidebar[] = {
    "x", "y", "width", "height", "icon", "on_select", "visible", "z_index"};
static const char *const k_exact_menu[] = {
    "x", "y", "visible", "icon", "on_click", "z_index"};
static const char *const k_exact_calendar[] = {
    "x", "y", "width", "height", "year", "month", "day", "popup", "on_change",
    "on_select", "visible", "z_index"};
static const char *const k_exact_datepicker[] = {
    "x", "y", "width", "height", "year", "month", "day", "popup", "title",
    "on_change", "visible", "z_index"};
static const char *const k_exact_timepicker[] = {
    "x", "y", "width", "height", "hour", "minute", "mode", "title", "on_change",
    "visible", "z_index"};
static const char *const k_exact_stepper[] = {
    "x", "y", "width", "height", "min", "max", "value", "selected", "step",
    "wrap", "on_change", "visible", "z_index"};
static const char *const k_exact_navigation[] = {
    "x", "y", "width", "height", "selected", "variant", "icon", "on_change",
    "on_select", "visible", "z_index"};
static const char *const k_exact_map[] = {
    "x", "y", "width", "height", "lat", "lon", "zoom", "attribution", "popup",
    "route_width", "color", "tint", "icon", "visible", "z_index"};
static const char *const k_exact_3d[] = {
    "x", "y", "width", "height", "src", "model", "theta", "phi", "radius",
    "fov", "near", "far", "target_x", "target_y", "target_z", "cam_theta",
    "cam_phi", "cam_radius", "cam_target_x", "cam_target_y", "cam_target_z",
    "interactive", "auto_rotate", "visible", "z_index"};
static const char *const k_exact_chart[] = {
    "x", "y", "width", "height", "type", "series", "points", "lines",
    "x_labels", "y_labels", "x_min", "x_max", "y_min", "y_max", "grid",
    "frame", "line", "point_type", "bar_gap", "pad_left", "pad_right",
    "pad_top", "pad_bottom", "line_thickness", "size", "colors",
    "series_colors", "series_lines", "series_points", "series_thickness",
    "thicknesses", "text_color", "frame_color", "grid_color", "line_color",
    "visible", "z_index"};
static const char *const k_exact_snackbar[] = {
    "x", "y", "width", "height", "text", "action", "duration", "show", "color",
    "text_color", "corner_radius", "on_click", "visible", "z_index"};

#define EXACT_PROPS(a) a, (int)(sizeof(a) / sizeof((a)[0]))
static const WidgetPropMap k_exact_widget_prop_map[] = {
    {"Button", EXACT_PROPS(k_exact_button)}, {"IconButton", EXACT_PROPS(k_exact_iconbutton)},
    {"Label", EXACT_PROPS(k_exact_label)}, {"Container", EXACT_PROPS(k_exact_container)},
    {"Column", EXACT_PROPS(k_exact_container)}, {"Row", EXACT_PROPS(k_exact_container)},
    {"Card", EXACT_PROPS(k_exact_card)}, {"Panel", EXACT_PROPS(k_exact_card)},
    {"ScrollView", EXACT_PROPS(k_exact_container)}, {"Canvas", EXACT_PROPS(k_exact_common)},
    {"Textbox", EXACT_PROPS(k_exact_input)}, {"Input", EXACT_PROPS(k_exact_input)},
    {"Switch", EXACT_PROPS(k_exact_switch)}, {"Checkbox", EXACT_PROPS(k_exact_checkbox)},
    {"RadioButton", EXACT_PROPS(k_exact_radio)}, {"Slider", EXACT_PROPS(k_exact_slider)},
    {"ProgressBar", EXACT_PROPS(k_exact_progress)}, {"Divider", EXACT_PROPS(k_exact_divider)},
    {"Icon", EXACT_PROPS(k_exact_icon)}, {"Image", EXACT_PROPS(k_exact_media)},
    {"GIF", EXACT_PROPS(k_exact_gif)}, {"Video", EXACT_PROPS(k_exact_video)},
    {"WebView", EXACT_PROPS(k_exact_media)}, {"Dropdown", EXACT_PROPS(k_exact_dropdown)},
    {"ListView", EXACT_PROPS(k_exact_list)}, {"Dialog", EXACT_PROPS(k_exact_dialog)},
    {"Tooltip", EXACT_PROPS(k_exact_tooltip)}, {"Chip", EXACT_PROPS(k_exact_chip)},
    {"Loading", EXACT_PROPS(k_exact_display)}, {"Gauge", EXACT_PROPS(k_exact_gauge)},
    {"Calendar", EXACT_PROPS(k_exact_calendar)}, {"DatePicker", EXACT_PROPS(k_exact_datepicker)},
    {"TimePicker", EXACT_PROPS(k_exact_timepicker)}, {"Stepper", EXACT_PROPS(k_exact_stepper)},
    {"Tabs", EXACT_PROPS(k_exact_tabs)},
    {"SegmentedControl", EXACT_PROPS(k_exact_segmented)},
    {"Carousel", EXACT_PROPS(k_exact_carousel)}, {"Sidebar", EXACT_PROPS(k_exact_sidebar)},
    {"Menu", EXACT_PROPS(k_exact_menu)}, {"Map", EXACT_PROPS(k_exact_map)},
    {"ThreeDViewer", EXACT_PROPS(k_exact_3d)}, {"Chart", EXACT_PROPS(k_exact_chart)},
    {"Snackbar", EXACT_PROPS(k_exact_snackbar)}, {"Table", EXACT_PROPS(k_exact_common)},
    {"DebugOverlay", EXACT_PROPS(k_exact_common)},
};

static const WidgetPropMap *widget_prop_find(const char *tag)
{
    for (size_t i = 0;
         i < sizeof(k_exact_widget_prop_map) / sizeof(k_exact_widget_prop_map[0]);
         i++)
    {
        if (strcmp(k_exact_widget_prop_map[i].tag, tag) == 0)
            return &k_exact_widget_prop_map[i];
    }
                                
    static WidgetPropMap fallback;
    fallback.tag = tag;
    fallback.keys = k_core_props;
    fallback.count = (int)(sizeof(k_core_props) / sizeof(k_core_props[0]));
    return &fallback;
}

static int prop_schema_find_index(const char *key)
{
    for (int i = 0; i < PROP_SCHEMA_COUNT; i++)
    {
        if (strcmp(k_prop_schema_all[i].key, key) == 0)
            return i;
    }
    return -1;
}

static bool s_panel_visible = true;

static void ide_layout_recalc(void)
{
    s_prop_dropdown_extra_h = 0;

                           
    if (s_sidebar_w < 160)
        s_sidebar_w = 160;
    if (s_sidebar_w > 400)
        s_sidebar_w = 400;
    if (s_palette_w < 140)
        s_palette_w = 140;
    if (s_palette_w > 260)
        s_palette_w = 260;

    status_y = win_h - STATUS_H;

                                                                         
                                                                       

                                                                          
                                                                          
                                    
    int outline_w = s_outline_w;
    int avail_w = win_w - EDITOR_X;
    if (avail_w - outline_w < 300)
        outline_w = avail_w - 300;
    if (outline_w < 120)
        outline_w = 0;
    right_x = win_w - outline_w;
    if (right_x < EDITOR_X)
        right_x = EDITOR_X;

    editor_w = right_x - EDITOR_X;
    if (editor_w < 80)
        editor_w = 80;
    panel_w = right_x - SIDEBAR_X;
    if (panel_w < 80)
        panel_w = 80;

    design_canvas_w = editor_w - (s_palette_w + 8) - 8;
    if (design_canvas_w < 80)
        design_canvas_w = 80;

                                                                           
    ds_code_x = right_x - 350;
    ds_dup_x = right_x - 264;
    ds_delete_x = right_x - 170;
    ds_save_x = right_x - 90;

                                                                          
                                                                        
    int content_h = status_y - EDITOR_Y;
    if (content_h < 160)
        content_h = 160;
    int max_panel_h = content_h - 120;
    if (max_panel_h < 120)
        max_panel_h = 120;
    if (s_panel_h_resizable < 120)
        s_panel_h_resizable = 120;
    if (s_panel_h_resizable > max_panel_h)
        s_panel_h_resizable = max_panel_h;

    panel_h = s_panel_h_resizable;
    if (s_panel_detached)
    {
                                                                            
        panel_y = status_y;
    }
    else
    {
        panel_y = status_y - panel_h;
    }
    if (panel_y < EDITOR_Y + 120)
    {
        panel_y = EDITOR_Y + 120;
        panel_h = status_y - panel_y;
        if (panel_h < 120)
            panel_h = 120;
        s_panel_h_resizable = panel_h;
    }

                                                                        
                         
    int editor_bottom = s_panel_visible ? panel_y : status_y;
    editor_h = editor_bottom - EDITOR_Y;
    if (editor_h < 100)
        editor_h = 100;

    design_body_y = EDITOR_Y + DESIGN_TOOLBAR_H;
    int design_bottom = s_panel_visible ? panel_y : status_y;
    design_body_h = design_bottom - design_body_y;
    if (design_body_h < 60)
        design_body_h = 60;

                                                               
    s_toast_w = 358;
    s_toast_h = 64;
    s_toast_x = win_w - s_toast_w - 32;
    s_toast_y = status_y - s_toast_h - 20;

                                                                 
    s_dialog_w = 592;
    s_dialog_h = 194;
    s_dialog_x = (win_w - s_dialog_w) / 2;
    s_dialog_y = (win_h - s_dialog_h) / 2;

    s_wiz_w = 752;
    s_wiz_h = 676;
    s_wiz_x = (win_w - s_wiz_w) / 2;
    s_wiz_y = (win_h - s_wiz_h) / 2;
}

static int s_wizard_step;
static int s_wizard_focus;
static int s_wizard_template;
static int s_wizard_hover;
static bool s_wizard_with_cmake;
static bool s_wizard_with_readme;
static bool s_wizard_open_after;
static char s_wizard_name[PROJECT_NAME_MAX];
static char s_wizard_path[PROJECT_PATH_MAX];
static char s_wizard_error[192];
static AromaNode *s_wizard_stepper;

static bool s_dialog_open;
static IdeDialogMode s_dialog_mode;
static char s_dialog_text[IDE_PATH_MAX];
static size_t s_dialog_length;
static char s_dialog_error[128];

                           
static char s_cmd_filter[128];
static size_t s_cmd_filter_len;
static int s_cmd_selected;
static int s_cmd_scroll;
static int s_cmd_filtered[CMD_MAX];
static int s_cmd_filtered_count;
static int s_cmd_recent[CMD_MAX];
static int s_cmd_recent_count;

static IdeAssets *s_assets;
static int s_active_file = -1;
static int s_tabs[IDE_MAX_FILES];
static int s_tab_count;
static int s_sidebar_mode;
static int s_panel_tab;
static int s_line = 1;
static int s_column = 1;
static int s_selection_chars;
static bool s_lsp_connected = true;

static char s_toast[256];
static uint64_t s_toast_until;
static bool s_toast_visible;
static bool s_toast_hiding;
static float s_toast_reveal = 0.0f;
static volatile bool s_project_ui_dirty;
static uint64_t s_project_ui_last;

static LogEntry s_logs[LOG_CAPACITY];
static int s_log_count;
static uint64_t s_last_change_log;
                                                                            
                                                                            
                                                                              
                                                                             
#define EDIT_FLUSH_IDLE_MS 90u
#define EDIT_FLUSH_MAX_MS 300u
static bool s_edit_pending;
static int s_edit_pending_file = -1;
static uint64_t s_edit_first_ms;
static uint64_t s_edit_last_ms;
static uint64_t s_last_completion_log;

static AromaCodeSymbol s_outline[MAX_SYMBOLS];
static int s_outline_count;

static int s_problem_file[MAX_PROBLEMS];
static int s_problem_diag[MAX_PROBLEMS];
static int s_problem_total;

static int s_script_y[4];
static int s_run_y[4];

                     
enum
{
    SETTING_CAT_EDITOR = 0,
    SETTING_CAT_THEME,
    SETTING_CAT_WINDOW,
    SETTING_CAT_OUTPUT,
    SETTING_CAT_FILE,
    SETTING_CAT_KEYBOARD,
    SETTING_CAT_ABOUT,
    SETTING_CAT_COUNT
};

static int s_setting_category = 0;
static int s_setting_scroll = 0;
static int s_setting_tab_scroll = 0;
static int s_setting_row_hover = -1;

static bool s_setting_line_numbers = true;
static bool s_setting_minimap = true;
static int s_setting_tab_width = 2;
static int s_setting_font_size = 12;
static bool s_setting_word_wrap = false;
static bool s_setting_auto_indent = true;
static bool s_setting_bracket_match = true;

static bool s_setting_theme_dark = true;
static int s_setting_font_family = 1;

static bool s_setting_transparency = false;
static int s_setting_opacity_index = 3;
static const float k_setting_opacity_values[5] = {
    0.80f, 0.86f, 0.90f, 0.94f, 1.0f};
static bool s_setting_experimental = false;

static bool s_setting_show_build_output = true;
static bool s_setting_show_run_output = true;
static bool s_setting_auto_scroll_output = true;

static bool s_setting_auto_save = false;
static bool s_setting_save_on_focus = false;
static bool s_setting_trim_trailing = true;
static bool s_setting_final_newline = true;

static int s_setting_hover = -1;
static bool s_settings_visible = false;
static bool s_settings_close_requested = false;

static AromaWindow *s_settings_window = NULL;
static AromaNode *s_settings_window_canvas = NULL;
static AromaNode *s_settings_window_root = NULL;
static bool s_settings_window_opening = false;
static bool s_settings_window_closing = false;
static float s_settings_window_anim_opacity = 1.0f;
static AromaNode *s_settings_sidebar = NULL;
static AromaNode *s_settings_title_label = NULL;
static AromaNode *s_settings_desc_label = NULL;
static AromaNode *s_settings_experimental_badge = NULL;
static bool s_settings_win_close_hover = false;
static int s_setting_cat_hover = -1;

static int s_title_hover = -1;
static int s_rail_hover = -1;
static int s_tab_hover = -1;
static int s_btn_hover = -1;
static bool s_tab_close_hover;
static int s_sidebar_action_hover = -1;
static int s_sidebar_row_hover = -1;
static int s_script_hover = -1;
static int s_outline_hover = -1;
static int s_panel_tab_hover = -1;
static int s_panel_icon_hover = -1;
static int s_panel_problem_hover = -1;
static int s_status_hover;
static bool s_menu_open;
static bool s_window_menu_open;
static bool s_menu_hiding;
static bool s_menu_last_window_menu;
static float s_menu_reveal = 0.0f;
static int s_window_menu_x;
static int s_menu_index;
static int s_menu_hover = -1;
static int s_menu_panel_x = MENU_PANEL_X;

static bool s_pending_definition;
static int s_pending_file;
static int s_pending_line;
static int s_pending_column;
static char s_pending_symbol[96];