static void ide_claim_node(AromaNode *node)
{
    if (node)
        aroma_layout_note_placed(node);
}

static void ide_claim_layout_nodes(void)
{
                                                                            
                                                                     
    if (s_root)
        aroma_node_set_layout_fill(s_root);

    ide_claim_node(s_editor);
    ide_claim_node(s_editor_bg);                                    
    ide_claim_node(s_settings_canvas);
    ide_claim_node(s_panel_bg);                                    
    ide_claim_node(s_panel_canvas);
    ide_claim_node(s_card_outline_bg);
    ide_claim_node(s_outline_canvas);
    ide_claim_node(s_toast_canvas);
    ide_claim_node(s_sidebar_canvas);
    ide_claim_node(s_card_sidebar_bg);
    ide_claim_node(s_rail_canvas);
    ide_claim_node(s_card_rail_bg);
    ide_claim_node(s_designer_palette);
    ide_claim_node(s_div_rail_right);
    ide_claim_node(s_div_outline_left);
    ide_claim_node(s_div_panel_top);
    ide_claim_node(s_title_canvas);
    ide_claim_node(s_card_title_bg);
    ide_claim_node(s_tabs_canvas);
    ide_claim_node(s_card_tabs_bg);
    ide_claim_node(s_breadcrumb_canvas);
    ide_claim_node(s_card_breadcrumb_bg);
    ide_claim_node(s_status_canvas);
    ide_claim_node(s_card_status_bg);
    ide_claim_node(s_div_title_bottom);
    ide_claim_node(s_div_status_top);
    ide_claim_node(s_designer_toolbar);
    ide_claim_node(s_designer_canvas);
    ide_claim_node(s_designer_preview);
    ide_claim_node(s_designer_overlay);
}

/* IntelliJ-style welcome screen: centred logo, product name and version,
 * then a short left-aligned list of actions, Everything is derived from the window
 * size, so it works in a small window. */
#define HOME_BG 0xFF4A494Du
#define HOME_ROW_H 36
#define HOME_LIST_W 280
#define HOME_LOGO 76

#define HOME_ITEM_COUNT() (s_home_has_recent_project ? 4 : 3)

/* In-window changelog (no separate native window / web view needed). */
static int s_changelog_scroll;
static int s_changelog_max_scroll;

static void home_changelog_panel(int *px, int *py, int *pw, int *ph)
{
    *px = 20;
    *py = TITLE_H + 12;
    *pw = win_w - 40;
    *ph = win_h - TITLE_H - 24;
}

static void home_metrics(int *col_x, int *top, int *btn_y0)
{
    int items = HOME_ITEM_COUNT();
    int total = HOME_LOGO + 20 + 34 + 8 + 20 + 34 + items * HOME_ROW_H;
    int t = TITLE_H + (win_h - TITLE_H - total) / 2;
    if (t < TITLE_H + 8)
        t = TITLE_H + 8;
    *top = t;
    *col_x = (win_w - HOME_LIST_W) / 2;
    *btn_y0 = t + HOME_LOGO + 20 + 34 + 8 + 20 + 34;
}

static int home_button_at(int x, int y)
{
    int col_x, top, y0;
    home_metrics(&col_x, &top, &y0);
    if (x < col_x || x >= col_x + HOME_LIST_W)
        return -1;
    for (int i = 0; i < HOME_ITEM_COUNT(); i++)
    {
        int by = y0 + i * HOME_ROW_H;
        if (y >= by && y < by + HOME_ROW_H)
            return i;
    }
    return -1;
}

static bool home_on_key(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_home_mode || !event || event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;
    if (s_changelog_visible)
    {
        if (event->data.key.key_code == 0xFF1B || event->data.key.key_code == 27)             
        {
            home_hide_changelog();
            return true;
        }
        return true;
    }
    if (!s_dialog_open)
        return false;
    return dialog_on_key(event, NULL);
}

static bool ide_home_global_key(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;
    if (!s_home_mode)
        return false;
    if (s_changelog_visible)
    {
        if (event->data.key.key_code == 0xFF1B || event->data.key.key_code == 27)             
        {
            home_hide_changelog();
            return true;
        }
        return true;
    }
    if (!s_dialog_open)
        return false;
    return dialog_on_key(event, NULL);
}

static bool home_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_home_mode || s_dialog_open)
        return false;
    if (s_changelog_visible)
    {
        if (s_home_hover != -1)
        {
            s_home_hover = -1;
            s_home_dirty = true;
        }
        return false;
    }
    int hover = home_button_at(event->data.mouse.x, event->data.mouse.y);
    if (hover != s_home_hover)
    {
        s_home_hover = hover;
        s_home_dirty = true;
    }
    return false;
}

static bool home_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_home_hover != -1)
    {
        s_home_hover = -1;
        s_home_dirty = true;
    }
    return false;
}

static bool home_titlebar_click(AromaEvent *event)
{
    if (!event || event->data.mouse.y < 0 || event->data.mouse.y >= TITLE_H)
        return false;
    if (event->data.mouse.x >= win_w - 96)
    {
        int x = event->data.mouse.x;
        if (x >= win_w - 40)
            aroma_ui_request_exit();
        else if (x >= win_w - 66)
        {
            toggle_window_fullscreen(&s_home_fullscreen);
            s_home_dirty = true;
        }
        else
            aroma_ui_set_window_minimized(aroma_ui_get_window_at(0), true);
        return true;
    }
    if (event->data.mouse.button == 2)
    {
        aroma_ui_request_exit();
        return true;
    }
    if (event->data.mouse.button == 3)
    {
        s_home_window_menu_x = event->data.mouse.x;
        if (s_home_window_menu_x > win_w - 170)
            s_home_window_menu_x = win_w - 170;
        aroma_node_set_hidden(s_home_window_menu_canvas, false);
        aroma_canvas_reset(s_home_window_menu_canvas);
        fill(s_home_window_menu_canvas, s_home_window_menu_x, TITLE_H, 170,
             MENU_ITEM_H * 3 + 12, 6, M3_SURFACE_HIGH);
        text(s_home_window_menu_canvas, "Minimize", s_home_window_menu_x + 12,
             TITLE_H + 6, C_TEXT_SOFT, font_ui(11));
        text(s_home_window_menu_canvas, "Maximize / Restore",
             s_home_window_menu_x + 12, TITLE_H + 6 + MENU_ITEM_H,
             C_TEXT_SOFT, font_ui(11));
        text(s_home_window_menu_canvas, "Close", s_home_window_menu_x + 12,
             TITLE_H + 6 + MENU_ITEM_H * 2, C_TEXT_SOFT, font_ui(11));
        return true;
    }
    uint64_t click_now = now_ms();
    if (click_now - s_home_last_click_ms < 350)
    {
        aroma_ui_toggle_window_maximized(aroma_ui_get_window_at(0));
        s_home_last_click_ms = 0;
    }
    else
    {
        s_home_last_click_ms = click_now;
        aroma_ui_begin_window_drag(aroma_ui_get_window_at(0));
    }
    return true;
}

static bool home_window_menu_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event)
        return true;
    int item = (event->data.mouse.y - TITLE_H) / MENU_ITEM_H;
    aroma_node_set_hidden(s_home_window_menu_canvas, true);
    if (item == 0)
        aroma_ui_set_window_minimized(aroma_ui_get_window_at(0), true);
    else if (item == 1)
        aroma_ui_toggle_window_maximized(aroma_ui_get_window_at(0));
    else if (item == 2)
        aroma_ui_request_exit();
    return true;
}

static bool home_on_scroll(AromaEvent *event, void *user_data);

void ide_home_init(IncenseRegistry *registry, IdeAssets *assets)
{
    s_assets = assets;
    s_home_canvas = find_widget(registry, "home_canvas");
    s_home_mode = true;
    s_home_project_path[0] = '\0';
    s_home_error[0] = '\0';
    s_home_dirty = true;
    s_home_hover = -1;
    s_home_drawn_w = s_home_drawn_h = 0;
    s_home_pending_dialog = 0;
    s_home_fullscreen = false;

    char recent_file[PATH_MAX];
    home_recent_file(recent_file, sizeof(recent_file));
    if (recent_file[0])
    {
        FILE *file = fopen(recent_file, "r");
        if (file)
        {
            char recent_path[PATH_MAX];
            if (fgets(recent_path, sizeof(recent_path), file))
            {
                trim_line(recent_path);
                ide_home_set_recent_project(recent_path);
            }
            fclose(file);
        }
    }

    if (s_home_canvas)
    {
        aroma_node_set_layout_fill(s_home_canvas);
        subscribe_node(s_home_canvas, EVENT_TYPE_MOUSE_CLICK, ide_home_on_click);
        subscribe_node(s_home_canvas, EVENT_TYPE_KEY_PRESS, home_on_key);
        subscribe_node(s_home_canvas, EVENT_TYPE_MOUSE_MOVE, home_on_move);
        subscribe_node(s_home_canvas, EVENT_TYPE_MOUSE_EXIT, home_on_exit);
        subscribe_node(s_home_canvas, EVENT_TYPE_MOUSE_SCROLL, home_on_scroll);
    }

                                                                          
                                                                              
                                                                           
    s_dialog_open = false;
    s_wizard_stepper = NULL;
    s_dialog_canvas = NULL;
                                                                             
                                                                         
                                                               
    s_home_host = find_widget(registry, "home_root");
    if (!s_home_host)
        s_home_host = g_windows[0].root_node;
    if (s_home_host)
    {
        s_home_window_menu_canvas = aroma_canvas_create(s_home_host, 0, 0,
                                                        win_w, win_h);
        if (s_home_window_menu_canvas)
        {
            aroma_node_set_z_index(s_home_window_menu_canvas, 210);
            aroma_node_set_hidden(s_home_window_menu_canvas, true);
            aroma_node_set_layout_fill(s_home_window_menu_canvas);
            subscribe_node(s_home_window_menu_canvas, EVENT_TYPE_MOUSE_CLICK,
                           home_window_menu_on_click);
        }
        s_dialog_canvas = aroma_canvas_create(s_home_host, 0, 0, win_w, win_h);
        if (s_dialog_canvas)
        {
            aroma_node_set_z_index(s_dialog_canvas, 180);
            aroma_node_set_hidden(s_dialog_canvas, true);
            aroma_node_set_layout_fill(s_dialog_canvas);
            subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_CLICK, dialog_on_click);
            subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_MOVE, dialog_on_move);
            subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_EXIT, dialog_on_exit);
            subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_SCROLL, props_on_scroll);
            subscribe_node(s_dialog_canvas, EVENT_TYPE_KEY_PRESS, dialog_on_key);
        }
    }

                                                                             
                                  
    aroma_event_set_global_key_handler(ide_home_global_key, NULL);
}

                                                                            
                                                                    
void ide_home_fail(const char *message)
{
    s_home_mode = true;
    s_home_project_path[0] = '\0';
    s_home_dirty = true;
    snprintf(s_home_error, sizeof(s_home_error), "%s",
             message && message[0] ? message : "Could not open the project");
}

static void home_action(int index, int col_x, int by, const char *glyph,
                        const char *label)
{
    bool hot = s_home_hover == index;
    AromaFont *ui14 = font_ui(14);
    if (hot)
        fill(s_home_canvas, col_x - 10, by + 2, HOME_LIST_W + 20,
             HOME_ROW_H - 4, 6, 0xFF55545AU);
    icon_center(s_home_canvas, glyph, col_x + 10, by + HOME_ROW_H / 2, 18,
                hot ? 0xFFFFFFFFu : 0xFFD0D0D4u);
    text_fit(s_home_canvas, label, col_x + 30, by + 9, HOME_LIST_W - 30,
             hot ? 0xFFFFFFFFu : 0xFFD0D0D4u, ui14);
}

/* AromaStudio logo, minimalist: a flat rounded tile with an "A" reduced to
 * a chevron, its crossbar replaced by a single dot (the incense ember). */
static void home_logo(int x, int y, int size)
{
    int cx = x + size / 2;
    int stroke = size / 11 > 3 ? size / 11 : 3;

    fill(s_home_canvas, x, y, size, size, size / 4, 0xFF0A84FFu);

    int apex_y = y + size * 22 / 100;
    int foot_y = y + size * 74 / 100;
    int half = size * 24 / 100;
    line(s_home_canvas, cx, apex_y, cx - half, foot_y, 0xFFFFFFFFu, stroke);
    line(s_home_canvas, cx, apex_y, cx + half, foot_y, 0xFFFFFFFFu, stroke);

    int dot = size * 10 / 100 > 4 ? size * 10 / 100 : 4;
    int dot_y = y + size * 56 / 100;
    fill(s_home_canvas, cx - dot / 2, dot_y - dot / 2, dot, dot, dot / 2,
         0xFFFFFFFFu);
}

typedef struct ChangelogCtx
{
    bool draw;
    int x;
    int y;      /* running content y (relative to content top) */
    int width;
    int view_top;
    int view_h;
    int scroll;
} ChangelogCtx;

static void changelog_emit(ChangelogCtx *c, const char *str, int indent,
                           AromaFont *font, uint32_t color, int line_h)
{
    int sy = c->view_top + c->y - c->scroll;
    if (c->draw && sy >= c->view_top - 2 && sy + line_h <= c->view_top + c->view_h + 2)
        text(s_home_canvas, str, c->x + indent, sy, color, font);
    c->y += line_h;
}

/* Word-wraps `src` (backticks removed) to the panel width. */
static void changelog_wrap(ChangelogCtx *c, const char *src, int indent,
                           AromaFont *font, uint32_t color, int line_h)
{
    char clean[256];
    size_t n = 0;
    for (const char *p = src; *p && n + 1 < sizeof(clean); p++)
        if (*p != '`')
            clean[n++] = *p;
    clean[n] = '\0';

    char cur[256];
    cur[0] = '\0';
    int max_w = c->width - indent;
    const char *p = clean;
    while (*p)
    {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *end = p;
        while (*end && *end != ' ')
            end++;
        char word[128];
        size_t wl = (size_t)(end - p);
        if (wl >= sizeof(word))
            wl = sizeof(word) - 1;
        memcpy(word, p, wl);
        word[wl] = '\0';
        p = end;

        char trial[512];
        snprintf(trial, sizeof(trial), "%s%s%s", cur, cur[0] ? " " : "", word);
        if (cur[0] && text_width(font, trial) > max_w)
        {
            changelog_emit(c, cur, indent, font, color, line_h);
            snprintf(cur, sizeof(cur), "%s", word);
        }
        else
        {
            snprintf(cur, sizeof(cur), "%s", trial);
        }
    }
    if (cur[0])
        changelog_emit(c, cur, indent, font, color, line_h);
}

/* Walks k_changelog_md; returns total content height.  With draw=false it
 * only measures (used to clamp scrolling). */
static int changelog_layout(bool draw, int x, int view_top, int view_h,
                            int width, int scroll)
{
    ChangelogCtx c = {draw, x, 0, width, view_top, view_h, scroll};
    AromaFont *ui13 = font_ui(13);
    AromaFont *ui16 = font_ui(16);
    const char *p = k_changelog_md;
    while (*p)
    {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line_buf[256];
        if (len >= sizeof(line_buf))
            len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, len);
        line_buf[len] = '\0';
        p = eol ? eol + 1 : p + len;

        if (line_buf[0] == '\0' || strncmp(line_buf, "# ", 2) == 0)
            continue; /* blank lines and the h1 (the panel header says it) */
        if (strncmp(line_buf, "## ", 3) == 0)
        {
            if (c.y > 0)
                c.y += 12;
            changelog_wrap(&c, line_buf + 3, 0, ui16, 0xFFFFFFFFu, 26);
            c.y += 2;
        }
        else if (strncmp(line_buf, "### ", 4) == 0)
        {
            c.y += 8;
            changelog_wrap(&c, line_buf + 4, 0, ui13, 0xFF64D2FFu, 22);
        }
        else if (strncmp(line_buf, "- ", 2) == 0)
        {
            int sy = c.view_top + c.y - c.scroll;
            if (draw && sy >= view_top - 2 && sy + 20 <= view_top + view_h + 2)
                fill(s_home_canvas, x + 4, sy + 8, 4, 4, 2, 0xFF8E8E93u);
            changelog_wrap(&c, line_buf + 2, 16, ui13, 0xFFD0D0D4u, 20);
        }
        else
        {
            changelog_wrap(&c, line_buf, 0, ui13, 0xFFD0D0D4u, 20);
        }
    }
    return c.y;
}

static void home_draw_changelog(void)
{
    int px, py, pw, ph;
    home_changelog_panel(&px, &py, &pw, &ph);

    fill(s_home_canvas, 0, TITLE_H, win_w, win_h - TITLE_H, 0, 0x99000000u);
    fill(s_home_canvas, px, py, pw, ph, 14, 0xFF2B2B2Eu);
    text(s_home_canvas, "Changelog", px + 18, py + 11, 0xFFF2F2F7u, font_ui(16));
    icon_center(s_home_canvas, AROMA_ICON_CLOSE, px + pw - 24, py + 20, 16,
                0xFFC7C7CCu);
    fill(s_home_canvas, px + 12, py + 40, pw - 24, 1, 0, 0xFF444448u);

    int view_top = py + 50;
    int view_h = ph - 62;
    int text_x = px + 20;
    int text_w = pw - 52;

    int total = changelog_layout(false, text_x, view_top, view_h, text_w, 0);
    s_changelog_max_scroll = total > view_h ? total - view_h : 0;
    if (s_changelog_scroll > s_changelog_max_scroll)
        s_changelog_scroll = s_changelog_max_scroll;
    if (s_changelog_scroll < 0)
        s_changelog_scroll = 0;

    changelog_layout(true, text_x, view_top, view_h, text_w, s_changelog_scroll);

    if (s_changelog_max_scroll > 0)
    {
        int track_x = px + pw - 12;
        int bar_h = view_h * view_h / total;
        if (bar_h < 24)
            bar_h = 24;
        int bar_y = view_top + (view_h - bar_h) * s_changelog_scroll /
                                   s_changelog_max_scroll;
        fill(s_home_canvas, track_x, bar_y, 4, bar_h, 2, 0xFF6A6A70u);
    }
}

static bool home_on_scroll(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_home_mode || !s_changelog_visible || !event ||
        event->event_type != EVENT_TYPE_MOUSE_SCROLL)
        return false;
    float steps = event->data.mouse.scroll_y;
    if (steps > -1.0f && steps < 1.0f)
        steps = steps < 0.0f ? -1.0f : (steps > 0.0f ? 1.0f : 0.0f);
    s_changelog_scroll -= (int)steps * 40;
    if (s_changelog_scroll > s_changelog_max_scroll)
        s_changelog_scroll = s_changelog_max_scroll;
    if (s_changelog_scroll < 0)
        s_changelog_scroll = 0;
    s_home_dirty = true;
    ide_home_draw();
    return true;
}

void ide_home_draw(void)
{
    if (!s_home_canvas || !s_home_mode || !s_assets)
        return;
    if (!s_home_dirty && s_home_drawn_w == win_w && s_home_drawn_h == win_h)
        return;
    s_home_dirty = false;
    s_home_drawn_w = win_w;
    s_home_drawn_h = win_h;

    aroma_canvas_reset(s_home_canvas);
    AromaFont *ui28 = font_ui(28);
    AromaFont *ui15 = font_ui(15);
    AromaFont *ui12 = font_ui(12);

    int col_x, top, y0;
    home_metrics(&col_x, &top, &y0);

    fill(s_home_canvas, 0, 0, win_w, win_h, 0, HOME_BG);

    /* title bar (custom: the window is undecorated) */
    fill(s_home_canvas, 0, 0, win_w, TITLE_H, 0, 0xFF252A3Au);
    text(s_home_canvas, "Welcome to AromaStudio", 14, 11, C_TEXT, ui12);
    icon_center(s_home_canvas, AROMA_ICON_REMOVE, win_w - 80, 18, 15,
                C_TEXT_SOFT);
    icon_center(s_home_canvas,
                s_home_fullscreen ? AROMA_ICON_FULLSCREEN_EXIT
                                  : AROMA_ICON_FULLSCREEN,
                win_w - 54, 18, 14, C_TEXT_SOFT);
    icon_center(s_home_canvas, AROMA_ICON_CLOSE, win_w - 28, 18, 13,
                C_TEXT_SOFT);

    /* logo, name, version */
    home_logo((win_w - HOME_LOGO) / 2, top, HOME_LOGO);
    int ty = top + HOME_LOGO + 24;
    {
        int aw = text_width(ui28, "Aroma");
        int sw = text_width(ui28, "Studio");
        int wx = (win_w - (aw + sw)) / 2;
        text(s_home_canvas, "Aroma", wx, ty, 0xFFC9C9CDu, ui28);
        text(s_home_canvas, "Studio", wx + aw, ty, 0xFF64D2FFu, ui28);
    }
    text_center(s_home_canvas, "Version alpha 0.0.1", 0, ty + 34 + 8, win_w,
                20, 0xFF8E8E93u, ui15);

    /* action list */
    int idx = 0;
    if (s_home_has_recent_project)
    {
        char recent_label[160];
        snprintf(recent_label, sizeof(recent_label), "Reopen %s",
                 s_home_recent_project_name);
        home_action(idx, col_x, y0 + idx * HOME_ROW_H, AROMA_ICON_HISTORY,
                    recent_label);
        idx++;
    }
    home_action(idx, col_x, y0 + idx * HOME_ROW_H, AROMA_ICON_ADD,
                "Create New Project");
    idx++;
    home_action(idx, col_x, y0 + idx * HOME_ROW_H, AROMA_ICON_FOLDER_OPEN,
                "Open");
    idx++;
    home_action(idx, col_x, y0 + idx * HOME_ROW_H, AROMA_ICON_DESCRIPTION,
                "Changelog");
    idx++;

    if (s_home_error[0])
        text_fit(s_home_canvas, s_home_error, col_x, y0 + idx * HOME_ROW_H + 6,
                 HOME_LIST_W + 40, M3_ERROR, ui12);

    if (s_changelog_visible)
        home_draw_changelog();

    if (s_dialog_open)
        draw_dialog();

    aroma_node_invalidate(s_home_canvas);
}

                                                                           
                                                                             
static void home_dialog_start_dir(const char *preferred, char *out,
                                  size_t out_size)
{
    struct stat st;
    if (preferred && preferred[0] && stat(preferred, &st) == 0 &&
        S_ISDIR(st.st_mode))
    {
                                                                           
                                                     
        char cwd[PATH_MAX];
        if (preferred[0] != '/' && getcwd(cwd, sizeof(cwd)))
        {
            if (preferred[0] == '.' && preferred[1] == '/')
                preferred += 2;
            if (strcmp(preferred, ".") == 0)
                snprintf(out, out_size, "%s", cwd);
            else
                snprintf(out, out_size, "%s/%s", cwd, preferred);
        }
        else
        {
            snprintf(out, out_size, "%s", preferred);
        }
        return;
    }
    const char *home = getenv("HOME");
    if (home && home[0] && stat(home, &st) == 0 && S_ISDIR(st.st_mode))
    {
        snprintf(out, out_size, "%s", home);
        return;
    }
    snprintf(out, out_size, "%s", ".");
}

                                                                              
                                                                               
                                                                           
                                  
static void home_pick_project(const char *preferred_dir)
{
    char start[PATH_MAX];
    home_dialog_start_dir(preferred_dir, start, sizeof(start));

    char *selected = NULL;
    AromaFileDialogResult result = aroma_file_dialog_pick_folder(
        "Open an Aroma project", start, &selected);

    if (result == AROMA_FILE_DIALOG_CANCEL)
    {
        aroma_file_dialog_free(selected);
        return;
    }
    if (result == AROMA_FILE_DIALOG_ERROR)
    {
        aroma_file_dialog_free(selected);
        open_dialog(IDE_DIALOG_OPEN_PROJECT, start);
        return;
    }
    if (!selected || !selected[0])
    {
        aroma_file_dialog_free(selected);
        return;
    }

    char json_path[PATH_MAX + 16];
    snprintf(json_path, sizeof(json_path), "%s/aroma.json", selected);
    if (access(json_path, R_OK) != 0)
    {
        snprintf(s_home_error, sizeof(s_home_error),
                 "No aroma.json project file found in %s", selected);
    }
    else
    {
        snprintf(s_home_project_path, sizeof(s_home_project_path), "%s",
                 selected);
        s_home_mode = false;

                                                                            
        snprintf(s_home_recent_project_path, sizeof(s_home_recent_project_path),
                 "%s", selected);
        snprintf(s_home_recent_project_name, sizeof(s_home_recent_project_name),
                 "%s", base_name(selected));
        s_home_has_recent_project = true;
        ide_home_set_recent_project(selected);
    }
    aroma_file_dialog_free(selected);
}

static void home_recent_file(char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || !home[0])
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s/.config/aromaui/studio_recent_project", home);
}

void ide_home_set_recent_project(const char *path)
{
    if (!path || !path[0])
        return;
    char json_path[PATH_MAX + 16];
    snprintf(json_path, sizeof(json_path), "%s/aroma.json", path);
    if (access(json_path, R_OK) != 0)
        return;
    snprintf(s_home_recent_project_path, sizeof(s_home_recent_project_path),
             "%s", path);
    snprintf(s_home_recent_project_name, sizeof(s_home_recent_project_name),
             "%s", base_name(path));
    s_home_has_recent_project = true;

    char recent_file[PATH_MAX];
    home_recent_file(recent_file, sizeof(recent_file));
    if (recent_file[0])
    {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", recent_file);
        char *slash = strrchr(dir, '/');
        if (slash)
        {
            *slash = '\0';
            mkdir(dir, 0755);
        }
        FILE *file = fopen(recent_file, "w");
        if (file)
        {
            fprintf(file, "%s\n", path);
            fclose(file);
        }
    }
}

static void home_show_changelog(void)
{
    s_changelog_visible = true;
    s_changelog_scroll = 0;
    s_home_dirty = true;
    ide_home_draw();
    aroma_node_invalidate(s_home_canvas);
}

static void home_hide_changelog(void)
{
    s_changelog_visible = false;
    s_home_dirty = true;
    ide_home_draw();
    aroma_node_invalidate(s_home_canvas);
}

bool ide_home_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (getenv("IDE_DEBUG_CLICKS") && event)
        fprintf(stderr, "[home-click] mouse=(%d,%d) button=%u target=%llu\n",
                event->data.mouse.x, event->data.mouse.y,
                (unsigned)event->data.mouse.button,
                (unsigned long long)(event->target_node
                                         ? event->target_node->node_id : 0));
    if (!s_home_mode)
        return false;
    if (home_titlebar_click(event))
        return true;
    if (s_changelog_visible)
    {
        int px, py, pw, ph;
        home_changelog_panel(&px, &py, &pw, &ph);
        int mx = event->data.mouse.x;
        int my = event->data.mouse.y;
        bool inside = mx >= px && mx < px + pw && my >= py && my < py + ph;
        bool on_close = mx >= px + pw - 44 && mx < px + pw &&
                        my >= py && my < py + 40;
        if (!inside || on_close)
            home_hide_changelog();
        return true;
    }
    if (s_dialog_open)
        return false;

    int button = home_button_at(event->data.mouse.x, event->data.mouse.y);
    if (button < 0)
        return false;

    s_home_error[0] = '\0';
    s_home_dirty = true;

    int idx = 0;
    if (s_home_has_recent_project)
    {
        if (button == idx++)
        {
                                                         
            if (access(s_home_recent_project_path, R_OK) == 0)
            {
                char json_path[PATH_MAX + 16];
                snprintf(json_path, sizeof(json_path),
                         "%s/aroma.json", s_home_recent_project_path);
                if (access(json_path, R_OK) == 0)
                {
                    snprintf(s_home_project_path, sizeof(s_home_project_path),
                             "%s", s_home_recent_project_path);
                    s_home_mode = false;
                }
                else
                {
                    snprintf(s_home_error, sizeof(s_home_error),
                             "Recent project no longer valid");
                }
            }
            else
            {
                snprintf(s_home_error, sizeof(s_home_error),
                         "Recent project folder not found");
            }
            return true;
        }
    }
    if (button == idx++)
        open_dialog(IDE_DIALOG_NEW_PROJECT, "");
    else if (button == idx++)
        home_pick_project(NULL);
    else if (button == idx)
        home_show_changelog();
    return true;
}

bool ide_home_active(void)
{
    return s_home_mode;
}

const char *ide_home_project_path(void)
{
    if (!s_home_project_path[0])
        return NULL;
    return s_home_project_path;
}

int ide_home_pending_dialog(void)
{
    return s_home_pending_dialog;
}

void ide_home_pending_dialog_clear(void)
{
    s_home_pending_dialog = 0;
}

void ide_home_open_pending_dialog(void)
{
    if (s_home_pending_dialog)
    {
        int mode = s_home_pending_dialog;
        s_home_pending_dialog = 0;
        open_dialog(mode, "");
    }
}

void ide_after_mount(IncenseRegistry *registry, IdeAssets *assets)
{
    s_assets = assets;
                                                                          
    s_home_canvas = NULL;
    s_home_host = NULL;
    s_wizard_stepper = NULL;
    s_dialog_canvas = NULL;
    s_dialog_open = false;
    s_home_pending_dialog = 0;
    aroma_ui_set_window_close_callback(ide_window_closed, NULL);
    AromaWindow *main_window = aroma_ui_get_window_at(0);
    aroma_ui_set_window_decorated(main_window, false);
    aroma_event_set_global_key_handler(ide_global_key, NULL);
    IncenseRegisterCallback("new_note", INCENSE_CALLBACK_VOID_PTR,
                            preview_new_note, NULL);
    ide_lsp_refresh_all();
    cache_nodes(registry);

    s_tab_count = 0;
    s_active_file = -1;
    s_sidebar_mode = 0;
    s_panel_tab = 0;
    s_lsp_connected = true;

    tree_build();
    workspace_search(s_search_query);
    project_init_state();

    log_add("initialize", "Opening Aroma project %s", ide_workspace_root);
    log_add("initialize", "aroma-language-server ready in 24 ms");
    log_add("window/logMessage", "Incense schema v2 loaded");
    log_add("window/logMessage", "C11 parser indexed %d files", ide_file_count);
    log_add("workspace/diagnostic", "Published diagnostics for %d workspace files",
            ide_file_count);

    ide_layout_recalc();

    bool has_files = ide_file_count > 0;
    if (has_files && s_root && assets && assets->editor)
    {
        s_editor = aroma_code_editor_create(s_root, EDITOR_X, EDITOR_Y, editor_w,
                                            editor_h);
        if (s_editor)
        {
            aroma_code_editor_set_font(s_editor, assets->editor);
            apply_editor_palette();
            aroma_code_editor_set_line_numbers(s_editor, true);
            aroma_code_editor_set_minimap(s_editor, true);
            aroma_code_editor_set_tab_width(s_editor, 2);
            aroma_code_editor_set_on_change(s_editor, editor_on_change, NULL);
            aroma_code_editor_set_on_cursor_changed(s_editor, editor_on_cursor, NULL);
            aroma_code_editor_set_on_save(s_editor, editor_on_save, NULL);
            aroma_code_editor_set_on_completion_request(s_editor,
                                                        editor_on_completion, NULL);
            aroma_code_editor_set_on_hover_request(s_editor, editor_on_hover, NULL);
            aroma_code_editor_set_on_definition_request(s_editor,
                                                        editor_on_definition, NULL);
            aroma_code_editor_setup_events(s_editor);
            aroma_node_set_z_index(s_editor, 5);
        }
    }

    create_overlays();
    ide_claim_layout_nodes();
    ide_sync_layout_nodes();
    setup_events();

    static const char *const initial_paths[] = {
        "ui/app.aroma", "src/main.c", "src/notes.c", "README.md"};
    int initial_active = -1;
    for (size_t i = 0; i < sizeof(initial_paths) / sizeof(initial_paths[0]); i++)
    {
        int index = ide_workspace_file_by_path(initial_paths[i]);
        if (index < 0)
            continue;
        bool already = false;
        for (int tab = 0; tab < s_tab_count; tab++)
        {
            if (s_tabs[tab] == index)
                already = true;
        }
        if (already || s_tab_count >= EDITOR_TAB_LIMIT)
            continue;
        s_tabs[s_tab_count++] = index;
        ide_files[index].open = true;
        if (initial_active < 0)
            initial_active = index;
    }
    if (initial_active < 0 && ide_file_count > 0)
    {
        s_tabs[s_tab_count++] = 0;
        ide_files[0].open = true;
        initial_active = 0;
    }
    if (s_editor && initial_active >= 0)
    {
        ide_open_file(initial_active, true);
        if (!s_designer_visible)
        {
            aroma_code_editor_goto_line_col(s_editor, 8, 5);
            aroma_code_editor_focus(s_editor);
        }
    }

    draw_all();
}