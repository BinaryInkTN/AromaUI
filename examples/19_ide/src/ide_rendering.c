static int android_dev_visible_rows(void);
static void android_dev_clamp_scroll(void);

static int title_menu_text_width(int index)
{
    if (index < 0 || index >= TITLE_MENU_COUNT)
        return 0;
    static AromaFont *ui11;
    if (!ui11)
        ui11 = font_ui(11);
    return text_width(ui11, k_menus[index]);
}


static int title_menu_x(int index)
{
    int x = TITLE_MENU_X;
    for (int i = 0; i < index; i++)
        x += title_menu_text_width(i) + TITLE_MENU_GAP;
    return x;
}


static int title_menu_end(void)
{
    return title_menu_x(TITLE_MENU_COUNT - 1) +
           title_menu_text_width(TITLE_MENU_COUNT - 1);
}
static int title_search_x(void)
{
    return win_w - TITLE_CTRL_W * TITLE_CTRL_COUNT - 12 - TITLE_SEARCH_W;
}

static bool title_search_visible(void)
{
    return title_search_x() > title_menu_end() + 120;
}

static int title_run_x(void)
{
    return title_search_x() - 14 - TITLE_RUN_W;
}

static int title_build_x(void)
{
    return title_run_x() - TITLE_ACTION_GAP - TITLE_BUILD_W;
}

static bool title_actions_visible(void)
{
    return title_search_visible() && title_build_x() > title_menu_end() + 120;
}

static int title_ctrl_center(int index)
{
    return win_w - TITLE_CTRL_W * TITLE_CTRL_COUNT + TITLE_CTRL_W / 2 +
           index * TITLE_CTRL_W;
}

                                                                           
                                                                             
                                                                    
#define RAIL_TOP 52
#define RAIL_ITEM_H 44
#define RAIL_ITEM_COUNT 2
#define RAIL_SETTINGS_INDEX RAIL_ITEM_COUNT
#define RAIL_SETTINGS_H 36

static int rail_settings_y(void)
{
    int y = win_h - STATUS_H - 80;
    int min_y = RAIL_TOP + RAIL_ITEM_COUNT * RAIL_ITEM_H + 8;
    return y < min_y ? min_y : y;
}

                                                                       
                                                  
static int rail_hit(int x, int y)
{
    if (x < 0 || x >= s_rail_w)
        return -1;
    if (y >= RAIL_TOP && y < RAIL_TOP + RAIL_ITEM_COUNT * RAIL_ITEM_H)
        return (y - RAIL_TOP) / RAIL_ITEM_H;
    int settings_y = rail_settings_y();
    if (y >= settings_y && y < settings_y + RAIL_SETTINGS_H)
        return RAIL_SETTINGS_INDEX;
    return -1;
}

static int sidebar_action_count(void)
{
    if (s_sidebar_mode == 3)
        return 0;
    return s_sidebar_mode == 0 ? 3 : 2;
}

                                                                           
                                                                            
                                                   
static int sidebar_action_x(int i, int count)
{
    int right = SIDEBAR_DRAW_X + s_sidebar_w - 38;
    return right - (count - i) * 22 + 4;
}

static bool sidebar_detach_hit(int x, int y)
{
    int right = SIDEBAR_DRAW_X + s_sidebar_w;
    return y >= 42 && y < 64 && x >= right - 30 && x < right - 4;
}

static int sidebar_action_hit(int x, int y)
{
    if (y < 42 || y >= 64)
        return -1;
    int count = sidebar_action_count();
    for (int i = 0; i < count; i++)
    {
        int ax = sidebar_action_x(i, count);
        if (x >= ax - 2 && x < ax + 20)
            return i;
    }
    return -1;
}

                                                                          
                                                                              
                          
static void debug_click(const char *where, const AromaEvent *event)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("IDE_DEBUG_CLICKS") != NULL;
    if (!enabled || !event)
        return;
    fprintf(stderr, "[ide-click] %s mouse=(%d,%d) win=%dx%d\n", where,
            event->data.mouse.x, event->data.mouse.y, win_w, win_h);
}

static void draw_title(void)
{
    if (!s_title_canvas)
        return;
    aroma_canvas_reset(s_title_canvas);
    fill(s_title_canvas, 0, 0, win_w, TITLE_H, 12, M3_SURFACE);
    fill(s_title_canvas, 0, 12, win_w, TITLE_H - 12, 0, M3_SURFACE);
    line(s_title_canvas, 12, TITLE_H - 1, win_w - 12, TITLE_H - 1,
         C_PRIMARY, 1);
    AromaFont *ui10 = font_ui(10);
    AromaFont *ui11 = font_ui(11);

                                                                           
    for (int i = 0; i < TITLE_MENU_COUNT; i++)
    {
        int mx = title_menu_x(i);
        int mw = title_menu_text_width(i);
        if (s_title_hover == i)
            fill(s_title_canvas, mx - 8, 7, mw + 16, 24, 12, C_HOVER);
        text(s_title_canvas, k_menus[i], mx, 12,
             i == s_title_hover ? C_TEXT : M3_ON_SURFACE, ui11);
    }

    if (s_menu_open)
    {
        int mx = title_menu_x(s_menu_index);
        int mw = title_menu_text_width(s_menu_index);
        fill(s_title_canvas, mx, 31, mw, 3, 1, M3_SURFACE);
    }

                                                                             
    int path_left = title_menu_end() + 24;
    int path_right = title_actions_visible()
                         ? title_build_x() - 16
                         : (title_search_visible()
                                ? title_search_x() - 16
                                : win_w - TITLE_CTRL_W * TITLE_CTRL_COUNT - 16);
          
    if (title_actions_visible())
    {
        int by = 6;
        int bh = 24;
        int bx = title_build_x();
        int rx = title_run_x();
        fill(s_title_canvas, bx, by, TITLE_BUILD_W, bh, 5,
             s_btn_hover == 0 ? C_HOVER : M3_SURFACE_CONTAINER);
        icon_center(s_title_canvas, AROMA_ICON_BUILD, bx + 14, by + bh / 2, 12,
                    C_TEXT_SOFT);
        text(s_title_canvas, "Build", bx + 25, by + 6, C_TEXT_SOFT, ui10);

        fill(s_title_canvas, rx, by, TITLE_RUN_W, bh, 5,
             s_btn_hover == 1 ? C_HOVER : M3_SURFACE_CONTAINER);
        icon_center(s_title_canvas, AROMA_ICON_PLAY_ARROW, rx + 14, by + bh / 2,
                    12, C_GREEN);
        text(s_title_canvas, "Run", rx + 25, by + 6, C_TEXT_SOFT, ui10);
    }

                    
    if (title_search_visible())
    {
        int search_w = TITLE_SEARCH_W;
        int search_h = 24;
        int search_x = title_search_x();
        int search_y = 6;
        if (s_title_search_hover)
            fill(s_title_canvas, search_x - 4, search_y - 2, search_w + 8,
                 search_h + 4, 4, C_HOVER);
        fill(s_title_canvas, search_x, search_y, search_w, search_h, 5,
             M3_SURFACE_LOW);
        line(s_title_canvas, search_x, search_y, search_x + search_w, search_y,
             C_BORDER, 1);
        line(s_title_canvas, search_x, search_y + search_h - 1,
             search_x + search_w, search_y + search_h - 1, C_BORDER, 1);
        line(s_title_canvas, search_x, search_y, search_x,
             search_y + search_h, C_BORDER, 1);
        line(s_title_canvas, search_x + search_w - 1, search_y,
             search_x + search_w - 1, search_y + search_h, C_BORDER, 1);
        icon_center(s_title_canvas, AROMA_ICON_SEARCH, search_x + 14,
                    search_y + search_h / 2, 13, C_TEXT_DIM);
        text_fit(s_title_canvas, "Search entire project", search_x + 32,
                 search_y + 7, search_w - 40, C_TEXT_DIM, ui10);
    }

                                                           
    for (int i = 0; i < TITLE_CTRL_COUNT; i++)
    {
        int cx = title_ctrl_center(i);
        if (s_title_ctrl_hover == i)
            fill(s_title_canvas, cx - 12, 4, 24, 28, 4, C_HOVER);
    }
    icon_center(s_title_canvas, AROMA_ICON_SETTINGS, title_ctrl_center(0), 18,
                14, s_settings_visible ? M3_PRIMARY : C_TEXT_DIM);
    icon_center(s_title_canvas, AROMA_ICON_FILTER_LIST, title_ctrl_center(1),
                18, 14, s_panel_visible ? C_TEXT : C_TEXT_DIM);
    icon_center(s_title_canvas, AROMA_ICON_PHONE_ANDROID, title_ctrl_center(2), 18, 14,
                s_project.target == 1 ? C_GREEN : C_TEXT_DIM);
    icon_center(s_title_canvas, AROMA_ICON_REMOVE, title_ctrl_center(3), 18, 15,
                C_TEXT_SOFT);
    icon_center(s_title_canvas,
                s_title_fullscreen ? AROMA_ICON_FULLSCREEN_EXIT
                                   : AROMA_ICON_FULLSCREEN,
                title_ctrl_center(4), 18, 14, C_TEXT_SOFT);
    icon_center(s_title_canvas, AROMA_ICON_CLOSE, title_ctrl_center(5), 18, 12,
                C_TEXT_SOFT);
}

static void draw_rail(void)
{
    if (!s_rail_canvas)
        return;
    aroma_canvas_reset(s_rail_canvas);
    fill(s_rail_canvas, 0, TITLE_H, s_rail_w, win_h - STATUS_H - TITLE_H, 0, C_RAIL);

    static const char *const icons[RAIL_ITEM_COUNT] = {
        AROMA_ICON_FOLDER_OPEN,
        AROMA_ICON_SEARCH,
    };

    for (int i = 0; i < RAIL_ITEM_COUNT; i++)
    {
        int y = RAIL_TOP + i * RAIL_ITEM_H;
        bool active = s_sidebar_mode == i;
        bool hovered = s_rail_hover == i;
        icon_center(s_rail_canvas, icons[i], 27, y + 21, 22,
                    active ? M3_PRIMARY
                           : (hovered ? C_TEXT : C_TEXT_DIM));
    }

    int settings_y = rail_settings_y();
    bool settings_active = s_settings_visible;
    bool settings_lit = s_rail_hover == RAIL_SETTINGS_INDEX || settings_active;
    icon_center(s_rail_canvas, AROMA_ICON_SETTINGS, 27, settings_y + 18, 22,
                settings_active ? M3_PRIMARY
                                : (settings_lit ? C_TEXT : C_TEXT_DIM));
}

static void draw_sidebar_header(void)
{
    static const char *const titles[2] = {
        "EXPLORER", "SEARCH"};
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);

    int sx = SIDEBAR_DRAW_X;

    text(s_sidebar_canvas, titles[s_sidebar_mode], sx + 68, 44, C_TEXT_SOFT, ui11);
    text_fit(s_sidebar_canvas, ide_workspace_project_name(), sx + 68, 62, 180,
             C_TEXT_FAINT, ui10);

    int icon_count = sidebar_action_count();
    const char *icons[3];
    if (s_sidebar_mode == 0)
    {
        icons[0] = AROMA_ICON_ADD;
        icons[1] = AROMA_ICON_REFRESH;
        icons[2] = AROMA_ICON_UNFOLD_LESS;
    }
    else
    {
        icons[0] = AROMA_ICON_REFRESH;
        icons[1] = AROMA_ICON_UNFOLD_LESS;
        icons[2] = AROMA_ICON_UNFOLD_LESS;
    }
    for (int i = 0; i < icon_count; i++)
    {
        int x = sidebar_action_x(i, icon_count);
        icon_center(s_sidebar_canvas, icons[i], x + 9, 53, 14,
                    s_sidebar_action_hover == i ? C_TEXT : C_TEXT_DIM);
    }

                                           
    int detach_x = sx + s_sidebar_w - 26;
    icon_center(s_sidebar_canvas,
                s_sidebar_detached ? AROMA_ICON_OPEN_IN_NEW
                                   : AROMA_ICON_OPEN_WITH,
                detach_x + 9, 53, 14,
                s_sidebar_detach_hover ? C_TEXT : C_TEXT_DIM);

    line(s_sidebar_canvas, sx, 88, sx + s_sidebar_w, 88, C_BORDER, 1);
}

static void draw_explorer(void)
{
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    int bottom = s_panel_visible ? panel_y : status_y;
    int visible = tree_row_capacity();
    if (s_tree_scroll > s_tree_row_count - visible)
        s_tree_scroll = s_tree_row_count - visible;
    if (s_tree_scroll < 0)
        s_tree_scroll = 0;

    for (int row = 0; row < visible; row++)
    {
        int display_row = row + s_tree_scroll;
        if (display_row >= s_tree_row_count)
            break;
        int y = TREE_TOP + row * TREE_ROW_HEIGHT;
        if (y + TREE_ROW_HEIGHT > bottom)
            break;
        if (display_row >= IDE_MAX_TREE_NODES)
            break;
        int node_index = s_tree_rows[display_row];
        if (node_index < 0 || node_index >= s_tree_node_count)
            continue;
        IdeTreeItem *node = &s_tree_nodes[node_index];
    int icon_x = SIDEBAR_DRAW_X + 68 + node->depth * 14;
    bool active = !node->is_dir && node->file_index == s_active_file;
    bool hovered = s_sidebar_row_hover == display_row;
    if (active)
    {
        fill(s_sidebar_canvas, SIDEBAR_DRAW_X + 2, y - 1, s_sidebar_w - 4,
             TREE_ROW_HEIGHT, 8, C_SELECT);
        fill(s_sidebar_canvas, SIDEBAR_DRAW_X + 2, y + 2, 2, 14, 1, C_PRIMARY);
    }
    else if (hovered)
    {
        fill(s_sidebar_canvas, SIDEBAR_DRAW_X + 2, y - 1, s_sidebar_w - 4,
             TREE_ROW_HEIGHT, 3, C_HOVER_SOFT);
    }

    if (node->is_dir)
    {
        icon_center(s_sidebar_canvas,
                    node->expanded ? AROMA_ICON_EXPAND_MORE
                                   : AROMA_ICON_CHEVRON_RIGHT,
                    icon_x + 2, y + 10, 13, C_TEXT_DIM);
        icon_center(s_sidebar_canvas,
                    node->expanded ? AROMA_ICON_FOLDER_OPEN : AROMA_ICON_FOLDER,
                    icon_x + 17, y + 10, 16, M3_YELLOW);
        text_fit(s_sidebar_canvas, node->name, icon_x + 30, y + 3, 166,
                 C_TEXT, ui11);
    }
    else
    {
        icon_center(s_sidebar_canvas, file_icon(node->file_index), icon_x + 9,
                    y + 10, 16, file_icon_color(node->file_index));
        text_fit(s_sidebar_canvas, node->name, icon_x + 22, y + 3, 180,
                 active ? C_TEXT : C_TEXT_SOFT, ui11);
        if (node->file_index >= 0 && node->file_index < ide_file_count &&
            ide_files[node->file_index].dirty)
            fill(s_sidebar_canvas, SIDEBAR_DRAW_X + s_sidebar_w - 14, y + 8, 5,
                 5, 2, C_PRIMARY);
    }
    }

    if (s_tree_row_count > visible)
    {
        int track_y = TREE_TOP;
        int track_height = visible * TREE_ROW_HEIGHT;
        fill(s_sidebar_canvas, SIDEBAR_DRAW_X + s_sidebar_w - 4, track_y, 2,
             track_height, 1, 0x55636366u);
        int thumb_height = track_height * visible / s_tree_row_count;
        if (thumb_height < 20)
            thumb_height = 20;
        int maximum = s_tree_row_count - visible;
        int thumb_y = track_y;
        if (maximum > 0)
            thumb_y += (track_height - thumb_height) * s_tree_scroll / maximum;
        fill(s_sidebar_canvas, SIDEBAR_DRAW_X + s_sidebar_w - 4, thumb_y, 2,
             thumb_height, 1, M3_OUTLINE);
    }

    if (s_panel_visible || s_script_y[0] >= 0)
        return;

    int tasks_y = status_y - 146;
    if (TREE_TOP + s_tree_row_count * TREE_ROW_HEIGHT > tasks_y - 8)
        return;
    line(s_sidebar_canvas, SIDEBAR_DRAW_X, tasks_y,
         SIDEBAR_DRAW_X + s_sidebar_w, tasks_y, C_BORDER, 1);
    tasks_y += 10;
    icon_center(s_sidebar_canvas, AROMA_ICON_EXPAND_MORE, SIDEBAR_DRAW_X + 14,
                tasks_y + 9, 14, C_TEXT_DIM);
    text(s_sidebar_canvas, "PROJECT TASKS", SIDEBAR_DRAW_X + 26, tasks_y + 2,
         C_TEXT_SOFT, ui11);
    tasks_y += 24;

    static const char *const scripts[4] = {"Build project", "Run project",
                                           "Clean build", "Language server"};
    for (int i = 0; i < 4; i++)
    {
        s_script_y[i] = tasks_y;
        if (s_script_hover == i)
            fill(s_sidebar_canvas, SIDEBAR_DRAW_X + 2, tasks_y - 2,
                 SIDEBAR_W - 4, 22, 3, C_HOVER_SOFT);
        icon_center(s_sidebar_canvas, AROMA_ICON_PLAY_ARROW, SIDEBAR_DRAW_X + 26,
                    tasks_y + 9, 12, C_PRIMARY);
        text(s_sidebar_canvas, scripts[i], SIDEBAR_DRAW_X + 44, tasks_y + 2,
             C_TEXT_SOFT, ui10);
        tasks_y += 22;
    }
}

static void draw_search(void)
{
    int sx = SIDEBAR_DRAW_X;
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    fill(s_sidebar_canvas, sx + 10, 94, 240, 30, 5, M3_SURFACE_LOW);
    icon_center(s_sidebar_canvas, AROMA_ICON_SEARCH, sx + 24, 109, 14,
                C_TEXT_DIM);
    text_fit(s_sidebar_canvas, s_search_query[0] ? s_search_query : "Search workspace",
             sx + 38, 101, 184, s_search_query[0] ? C_TEXT : C_TEXT_DIM, ui11);
    icon_center(s_sidebar_canvas, AROMA_ICON_TUNE, sx + 236, 109, 14,
                C_TEXT_FAINT);

    text(s_sidebar_canvas, s_search_summary, sx + 10, 136, C_TEXT_DIM, ui10);

    int bottom = s_panel_visible ? panel_y : status_y;
    int row_height = 38;
    int visible = (bottom - 158) / row_height;
    int maximum = s_search_hit_count - visible;
    if (maximum < 0)
        maximum = 0;
    if (s_search_scroll > maximum)
        s_search_scroll = maximum;
    if (s_search_scroll < 0)
        s_search_scroll = 0;

    if (s_search_hit_count == 0)
    {
        icon_center(s_sidebar_canvas, AROMA_ICON_SEARCH, sx + 130, 210, 24,
                    C_TEXT_FAINT);
        text_center(s_sidebar_canvas, s_search_query[0] ? "No matches in this project"
                                                       : "Click the field to search",
                    sx + 10, 232, 240, 18, C_TEXT_DIM, ui11);
        return;
    }

    int y = 158;
    for (int row = 0; row < visible; row++)
    {
        int index = row + s_search_scroll;
        if (index >= s_search_hit_count)
            break;
        s_search_y[index] = y;
        const SearchHit *hit = &s_search_hits[index];
        if (hit->file < 0 || hit->file >= ide_file_count)
            continue;
        if (s_sidebar_row_hover == index)
            fill(s_sidebar_canvas, sx + 4, y - 4, 248, 36, 4, C_HOVER_SOFT);
        icon_center(s_sidebar_canvas, file_icon(hit->file), sx + 16, y + 4, 14,
                    file_icon_color(hit->file));
        text_fit(s_sidebar_canvas, ide_files[hit->file].name, sx + 30, y - 2, 150,
                 C_TEXT_SOFT, ui11);
        char location[24];
        snprintf(location, sizeof(location), ":%d", hit->line);
        int location_width = text_width(ui10, location);
        text(s_sidebar_canvas, location, sx + s_sidebar_w - 10 - location_width, y,
             C_TEXT_FAINT, ui10);
        text_fit(s_sidebar_canvas, hit->snippet, sx + 30, y + 14, 212, C_TEXT_DIM,
                 ui10);
        y += row_height;
    }
}

static void draw_run(void)
{
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    int sx = SIDEBAR_DRAW_X;

    fill(s_sidebar_canvas, sx + 10, 94, 240, 36, 6, C_PRIMARY);
    icon_center(s_sidebar_canvas, AROMA_ICON_PLAY_ARROW, sx + 26, 112, 16,
                M3_ON_PRIMARY);
    text(s_sidebar_canvas, "Run and Debug", sx + 44, 103, M3_ON_PRIMARY, ui11);

    icon_center(s_sidebar_canvas, AROMA_ICON_PLAY_CIRCLE_OUTLINE, sx + 18, 150,
                14, C_TEXT_DIM);
    text(s_sidebar_canvas, "Start Without Debugging", sx + 32, 142, C_TEXT_SOFT,
         ui11);

    text(s_sidebar_canvas, "Tasks", sx + 10, 184, C_TEXT_FAINT, ui10);

    static const char *const configs[4] = {
        "Build project", "Run application", "Clean build output",
        "Language server trace"};
    int y = 204;
    for (int i = 0; i < 4; i++)
    {
        s_run_y[i] = y;
        if (s_sidebar_row_hover == i)
            fill(s_sidebar_canvas, sx + 4, y - 3, 248, 24, 3, C_HOVER_SOFT);
        icon_center(s_sidebar_canvas, AROMA_ICON_SETTINGS_APPLICATIONS, sx + 18,
                    y + 8, 14, C_TEXT_DIM);
        text(s_sidebar_canvas, configs[i], sx + 34, y + 1, C_TEXT_SOFT, ui11);
        y += 24;
    }
}

                                                                           
           
                                                                            

static const char *const k_setting_cat_labels[SETTING_CAT_COUNT] = {
    "Editor", "Theme", "Window", "Output", "File", "Keyboard", "About"};
static const char *const k_setting_cat_icons[SETTING_CAT_COUNT] = {
    AROMA_ICON_EDIT, AROMA_ICON_PALETTE, AROMA_ICON_OPACITY,
    AROMA_ICON_DASHBOARD, AROMA_ICON_DESCRIPTION, AROMA_ICON_KEYBOARD,
    AROMA_ICON_INFO};
static const int k_setting_cat_rows[SETTING_CAT_COUNT] = {7, 3, 3, 3, 4, 1, 2};

static void draw_setting_toggle(AromaNode *canvas, int y, int item_x,
                                int item_w, bool *value, const char *label,
                                const char *desc, bool draw_control)
{
    int yy = y + 2;
    
    text(canvas, label, item_x + 36, yy + 4, C_TEXT, font_ui(11));
    text(canvas, desc, item_x + 36, yy + 18, C_TEXT_DIM, font_ui(10));
    if (!draw_control)
        return;
    int sw = 40, sh = 18;
    int toggle_x = item_x + item_w - sw - 20;
    fill(canvas, toggle_x, yy + 4, sw, sh, 6,
         *value ? C_PRIMARY : M3_SURFACE_HIGHEST);
    fill(canvas, toggle_x + (*value ? sh - 6 : 4), yy + 8, 6, 6, 3,
         0xFFFFFFFFu);
}

static void draw_setting_select(AromaNode *canvas, int y, int item_x,
                                int item_w, int *value,
                                const char *const *options, int opt_count,
                                const char *label, const char *desc,
                                bool draw_control)
{
    int yy = y + 2;
  
    text(canvas, label, item_x + 36, yy + 4, C_TEXT, font_ui(11));
    text(canvas, desc, item_x + 36, yy + 18, C_TEXT_DIM, font_ui(10));
    if (!draw_control)
        return;
    int index = *value;
    if (index < 0 || index >= opt_count)
        index = 0;
    char val[64];
    snprintf(val, sizeof(val), "%s", options[index]);
    int vw = text_width(font_ui(10), val);
    int val_x = item_x + item_w - vw - 30;
    fill(canvas, val_x - 6, yy + 2, vw + 16, 20, 4, M3_SURFACE_LOW);
    text(canvas, val, val_x, yy + 6, C_TEXT_SOFT, font_ui(10));
    icon_center(canvas, AROMA_ICON_KEYBOARD_ARROW_DOWN,
                val_x + vw + 6, yy + 12, 12, C_TEXT_DIM);
}

static void draw_setting_button(AromaNode *canvas, int y, int item_x,
                                int item_w, const char *label,
                                const char *desc)
{
    int yy = y + 2;
    
    text(canvas, label, item_x + 36, yy + 4, C_TEXT, font_ui(11));
    text(canvas, desc, item_x + 36, yy + 18, C_TEXT_DIM, font_ui(10));
}

                                                                         
                                        
static void draw_setting_entry(AromaNode *canvas, int y, int cat, int row,
                               int entry_idx, int item_x, int item_w,
                               bool draw_controls)
{
    if (!canvas)
        return;

    bool hover = (s_setting_category == cat && s_setting_row_hover == entry_idx);
    if (hover)
        fill(canvas, item_x - 6, y - 4, item_w + 12, 42, 5, C_HOVER_SOFT);

    if (cat == SETTING_CAT_EDITOR)
    {
        if (row == 0)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_line_numbers, "Line numbers",
                                "Show line numbers in the editor", draw_controls);
        else if (row == 1)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_minimap, "Minimap",
                                "Show code minimap", draw_controls);
        else if (row == 2)
        {
            static const char *const opts[] = {"2 spaces", "4 spaces",
                                               "8 spaces"};
            int value = s_setting_tab_width / 2 - 1;
            if (value < 0 || value > 2)
                value = 0;
            draw_setting_select(canvas, y, item_x, item_w, &value, opts, 3,
                                "Tab width",
                                "Spaces per indentation level", draw_controls);
        }
        else if (row == 3)
        {
            static const char *const opts[] = {"10", "11", "12", "14", "16"};
            static const int values[] = {10, 11, 12, 14, 16};
            int value = 2;
            for (int i = 0; i < 5; i++)
            {
                if (values[i] == s_setting_font_size)
                {
                    value = i;
                    break;
                }
            }
            draw_setting_select(canvas, y, item_x, item_w, &value, opts, 5,
                                "Font size", "Editor font size in px",
                                draw_controls);
        }
        else if (row == 4)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_word_wrap, "Word wrap",
                                "Wrap long lines", draw_controls);
        else if (row == 5)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_auto_indent, "Auto indent",
                                "Automatic indentation", draw_controls);
        else if (row == 6)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_bracket_match, "Bracket matching",
                                "Highlight matching brackets", draw_controls);
    }
    else if (cat == SETTING_CAT_THEME)
    {
        if (row == 0)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_theme_dark, "Dark theme",
                                "Use dark color theme", draw_controls);
        else if (row == 1)
        {
            static const char *const opts[] = {"Blue", "Purple", "Green",
                                               "Orange", "Red"};
            draw_setting_select(canvas, y, item_x, item_w, &s_setting_accent,
                                opts, 5, "Accent color", "UI accent color",
                                draw_controls);
        }
        else if (row == 2)
        {
            static const char *const opts[] = {"Monospace", "Sans", "Serif"};
            draw_setting_select(canvas, y, item_x, item_w,
                                &s_setting_font_family, opts, 3,
                                "Font family", "UI font family", draw_controls);
        }
    }
    else if (cat == SETTING_CAT_WINDOW)
    {
        if (row == 0)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_transparency, "Transparency",
                                "Make application windows translucent",
                                draw_controls);
        else if (row == 1)
        {
            static const char *const opts[] = {"80%", "86%", "90%", "94%",
                                               "100%"};
            int value = s_setting_opacity_index;
            if (value < 0 || value > 4)
                value = 3;
            draw_setting_select(canvas, y, item_x, item_w, &value, opts, 5,
                                "Window opacity",
                                "Translucency used when transparency is on",
                                draw_controls);
        }
        else if (row == 2)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_experimental,
                                "Experimental features",
                                "Enable in-development UI effects and badges",
                                draw_controls);
    }
    else if (cat == SETTING_CAT_OUTPUT)
    {
        if (row == 0)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_show_build_output,
                                "Show build output",
                                "Display build output in panel", draw_controls);
        else if (row == 1)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_show_run_output,
                                "Show run output",
                                "Display run output in panel", draw_controls);
        else if (row == 2)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_auto_scroll_output,
                                "Auto-scroll output",
                                "Scroll output to bottom automatically",
                                draw_controls);
    }
    else if (cat == SETTING_CAT_FILE)
    {
        if (row == 0)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_auto_save, "Auto save",
                                "Save files automatically", draw_controls);
        else if (row == 1)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_save_on_focus, "Save on focus change",
                                "Save when editor loses focus", draw_controls);
        else if (row == 2)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_trim_trailing,
                                "Trim trailing whitespace",
                                "Remove trailing whitespace on save",
                                draw_controls);
        else if (row == 3)
            draw_setting_toggle(canvas, y, item_x, item_w,
                                &s_setting_final_newline, "Final newline",
                                "Ensure file ends with newline", draw_controls);
    }
    else if (cat == SETTING_CAT_KEYBOARD)
    {
        if (row == 0)
            draw_setting_button(canvas, y, item_x, item_w,
                                "View keybindings",
                                "Show keyboard shortcuts reference");
    }
    else if (cat == SETTING_CAT_ABOUT)
    {
        if (row == 0)
        {
            text(canvas, "AromaUI Studio", item_x + 36, y + 6, C_TEXT,
                 font_ui(11));
            text(canvas, "Alpha 0.0.1", item_x + 36, y + 20, C_TEXT_DIM,
                 font_ui(10));
        }
        else if (row == 1)
        {
            text(canvas, "Built with AromaUI", item_x + 36, y + 6,
                 C_TEXT_SOFT, font_ui(10));
        }
    }
}

static void draw_settings(void)
{
    if (!s_sidebar_canvas)
        return;
    aroma_canvas_reset(s_sidebar_canvas);
    int sx = SIDEBAR_DRAW_X;
    fill(s_sidebar_canvas, sx, TITLE_H, s_sidebar_w,
         (s_panel_visible ? panel_y : status_y) - TITLE_H, 0, C_SIDEBAR);

    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);

    int cat_w = s_sidebar_w / 2;
    int item_x = sx + cat_w + 20;

                
    fill(s_sidebar_canvas, sx, TITLE_H, cat_w, 60, 0, C_SIDEBAR);
    icon_center(s_sidebar_canvas, AROMA_ICON_SETTINGS, sx + 34,
                TITLE_H + 30, 20, C_PRIMARY);
    text(s_sidebar_canvas, "Settings", sx + 64, TITLE_H + 24, C_TEXT,
         ui11);
    text(s_sidebar_canvas, "IDE preferences", sx + 64, TITLE_H + 42,
         C_TEXT_DIM, ui10);

                       
    int cy = TITLE_H + 80;
    int cat_h = 44;
    for (int i = 0; i < SETTING_CAT_COUNT; i++)
    {
        bool active = (s_setting_category == i);
        int row_y = cy + i * cat_h;
        if (active)
            fill(s_sidebar_canvas, sx, row_y, cat_w, cat_h, 4,
                 C_SELECT);
        icon_center(s_sidebar_canvas, k_setting_cat_icons[i],
                    sx + 34, row_y + cat_h / 2, 16,
                    active ? C_PRIMARY : C_TEXT_DIM);
        text(s_sidebar_canvas, k_setting_cat_labels[i], sx + 64, row_y + 10,
             active ? C_TEXT : C_TEXT_SOFT, ui11);
    }

                                                                    
    int iy = TITLE_H + 80;
    int item_h = 46;
    int bottom = s_panel_visible ? panel_y : status_y;
    int visible = (bottom - iy) / item_h;
    if (visible < 1)
        visible = 1;

    int start = s_setting_scroll;
    int shown = 0;

                                                                               
    typedef struct
    {
        int cat;
        int row;
        bool is_header;
    } SettingEntry;
    static SettingEntry entries[64];
    int entry_count = 0;
    for (int c = 0; c < SETTING_CAT_COUNT; c++)
    {
        entries[entry_count].cat = c;
        entries[entry_count].row = -1;
        entries[entry_count].is_header = true;
        entry_count++;
        for (int r = 0; r < k_setting_cat_rows[c]; r++)
        {
            entries[entry_count].cat = c;
            entries[entry_count].row = r;
            entries[entry_count].is_header = false;
            entry_count++;
        }
    }
    int total = entry_count;

                      
    int max_scroll = total - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (start > max_scroll)
        start = max_scroll;
    if (start < 0)
        start = 0;
    s_setting_scroll = start;

    int item_y = iy;
    for (int i = start; i < total && shown < visible; i++, shown++)
    {
        int c = entries[i].cat;
        int r = entries[i].row;
        bool is_header = entries[i].is_header;
        int ry = item_y;

        if (is_header)
        {
                                    
            fill(s_sidebar_canvas, item_x, ry, s_sidebar_w - cat_w - 40, 1, 0,
                 C_BORDER_SOFT);
            text(s_sidebar_canvas, k_setting_cat_labels[c], item_x, ry + 4,
                 C_TEXT_FAINT, ui10);
            item_y += 22;
        }
        else
        {
            int item_w = s_sidebar_w - cat_w - 40;
            draw_setting_entry(s_sidebar_canvas, ry, c, r, i, item_x, item_w,
                               true);
            item_y += item_h;
        }
    }

                     
    if (total > visible)
    {
        text(s_sidebar_canvas, "Scroll to see more", item_x,
             bottom - 24, C_TEXT_DIM, ui10);
    }

    text(s_sidebar_canvas, "Changes apply immediately.", item_x, bottom - 50,
         C_TEXT_FAINT, ui10);
}

                                                                           
                                                      
                                                                              

#define SETTINGS_WIN_W 900
#define SETTINGS_WIN_H 620
#define SETTINGS_WIN_HEADER_H 64
#define SETTINGS_WIN_SIDEBAR_X 5
#define SETTINGS_WIN_SIDEBAR_Y 84
#define SETTINGS_WIN_SIDEBAR_W 184
#define SETTINGS_WIN_SIDEBAR_H (SETTINGS_WIN_H - SETTINGS_WIN_SIDEBAR_Y - 58)
#define SETTINGS_WIN_CONTENT_X 224
#define SETTINGS_WIN_CONTENT_W (SETTINGS_WIN_W - SETTINGS_WIN_CONTENT_X - 24)
#define SETTINGS_WIN_ROWS_Y 150
#define SETTINGS_WIN_ROW_H 52
#define SETTINGS_WIN_FOOTER_H 58

static const char *const k_setting_cat_desc[SETTING_CAT_COUNT] = {
    "Text editor behavior",
    "Appearance and color theme",
    "Window transparency and in-development features",
    "Build and run output",
    "Saving and formatting",
    "Keyboard shortcuts",
    "About AromaUI Studio"};

enum
{
    SETTINGS_LABEL_TEXT = 0,
    SETTINGS_LABEL_SOFT,
    SETTINGS_LABEL_DIM,
    SETTINGS_LABEL_FAINT,
    SETTINGS_LABEL_ACCENT
};

typedef struct
{
    AromaNode *node;
    int role;
} SettingsLabelRef;

#define SETTINGS_LABEL_MAX 160
static SettingsLabelRef s_settings_labels[SETTINGS_LABEL_MAX];
static int s_settings_label_count;

typedef enum
{
    SETTINGS_DD_TAB_WIDTH = 0,
    SETTINGS_DD_FONT_SIZE,
    SETTINGS_DD_ACCENT,
    SETTINGS_DD_FONT_FAMILY,
    SETTINGS_DD_OPACITY
} SettingsDropdownKind;

typedef struct
{
    SettingsDropdownKind kind;
} SettingsDropdownBinding;

#define SETTINGS_DROPDOWN_MAX 8
static SettingsDropdownBinding s_settings_dropdowns[SETTINGS_DROPDOWN_MAX];
static int s_settings_dropdown_count;

static AromaNode *s_settings_close_button;

static uint32_t settings_label_color(int role)
{
    switch (role)
    {
    case SETTINGS_LABEL_SOFT:
        return C_TEXT_SOFT;
    case SETTINGS_LABEL_DIM:
        return C_TEXT_DIM;
    case SETTINGS_LABEL_FAINT:
        return C_TEXT_FAINT;
    case SETTINGS_LABEL_ACCENT:
        return M3_PRIMARY;
    case SETTINGS_LABEL_TEXT:
    default:
        return C_TEXT;
    }
}

static bool settings_no_hit(AromaNode *node, int x, int y, void *user_data)
{
    (void)node;
    (void)x;
    (void)y;
    (void)user_data;
    return false;
}

static AromaNode *settings_add_label(AromaNode *parent, const char *value,
                                     int x, int y, AromaFont *font, int role)
{
    AromaNode *label = aroma_ui_label(parent, value, x, y,
                                      LABEL_STYLE_LABEL_LARGE, font);
    if (!label)
        return NULL;
    aroma_node_set_z_index(label, 2);
    aroma_layout_note_placed(label);
    aroma_label_set_color(label, settings_label_color(role));
    if (s_settings_label_count < SETTINGS_LABEL_MAX)
    {
        s_settings_labels[s_settings_label_count].node = label;
        s_settings_labels[s_settings_label_count].role = role;
        s_settings_label_count++;
    }
    return label;
}

static void settings_refresh_label_colors(void)
{
    for (int i = 0; i < s_settings_label_count; i++)
    {
        if (s_settings_labels[i].node)
            aroma_label_set_color(s_settings_labels[i].node,
                                  settings_label_color(s_settings_labels[i].role));
    }
    if (s_settings_close_button)
        aroma_iconbutton_set_colors(s_settings_close_button, C_TITLE, C_TEXT_SOFT);
}

static float settings_window_opacity(void)
{
    int index = s_setting_opacity_index;
    if (index < 0)
        index = 0;
    if (index > 4)
        index = 4;
    float opacity = s_setting_transparency ? k_setting_opacity_values[index]
                                           : 1.0f;
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    return opacity;
}

static void settings_window_apply_opacity(float opacity)
{
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    s_settings_window_anim_opacity = opacity;
    if (s_settings_window)
        aroma_ui_set_window_opacity(s_settings_window, opacity);
}

static void settings_window_fade_step(AromaNode *target, float value,
                                      void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_settings_window || s_settings_window_closing == false)
        return;
    settings_window_apply_opacity(value);
}

static void settings_window_open_fade_step(AromaNode *target, float value,
                                           void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_settings_window || !s_settings_window_opening)
        return;
    settings_window_apply_opacity(settings_window_opacity() * value);
}

static void settings_window_open_finished(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_settings_window || !s_settings_window_opening)
        return;
    s_settings_window_opening = false;
    settings_window_apply_opacity(settings_window_opacity());
}

static void settings_window_close_finished(AromaNode *target, void *user_data);

static void settings_apply_windows(void)
{
    float opacity = settings_window_opacity();
    AromaWindow *main_window = aroma_ui_get_window_at(0);
    if (main_window)
        aroma_ui_set_window_opacity(main_window, opacity);
    if (!s_settings_window)
        return;

    /* A setting change or an explicit opacity request should win over an
     * in-flight entrance animation instead of fighting it every frame. */
    if (s_settings_window_opening)
    {
        if (s_settings_window_root)
            aroma_animation_stop(s_settings_window_root);
        s_settings_window_opening = false;
    }
    if (!s_settings_window_closing)
        settings_window_apply_opacity(opacity);
}

static void settings_apply_experimental(void)
{
    if (s_settings_experimental_badge)
        aroma_node_set_hidden(s_settings_experimental_badge,
                              !s_setting_experimental);
    if (s_settings_sidebar)
        aroma_sidebar_set_transition(s_settings_sidebar, AROMA_ANIM_FADE, 160u);
}

static int settings_tab_width_index(void)
{
    int index = s_setting_tab_width / 2 - 1;
    if (index < 0)
        index = 0;
    if (index > 2)
        index = 2;
    return index;
}

static int settings_font_size_index(void)
{
    static const int sizes[] = {10, 11, 12, 14, 16};
    for (int i = 0; i < 5; i++)
    {
        if (sizes[i] == s_setting_font_size)
            return i;
    }
    return 2;
}

static bool settings_switch_changed(AromaNode *node, void *user_data)
{
    bool *value = (bool *)user_data;
    if (value)
        *value = aroma_switch_get_state(node);
    settings_apply_all();
    return true;
}

static void settings_dropdown_changed(int index, const char *option,
                                      void *user_data)
{
    (void)option;
    SettingsDropdownBinding *binding = (SettingsDropdownBinding *)user_data;
    if (!binding)
        return;

    switch (binding->kind)
    {
    case SETTINGS_DD_TAB_WIDTH:
        if (index < 0)
            index = 0;
        if (index > 2)
            index = 2;
        s_setting_tab_width = 2 + index * 2;
        break;
    case SETTINGS_DD_FONT_SIZE:
    {
        static const int sizes[] = {10, 11, 12, 14, 16};
        if (index >= 0 && index < 5)
            s_setting_font_size = sizes[index];
        break;
    }
    case SETTINGS_DD_ACCENT:
        if (index < 0)
            index = 0;
        if (index > 4)
            index = 4;
        s_setting_accent = index;
        break;
    case SETTINGS_DD_FONT_FAMILY:
        if (index < 0)
            index = 0;
        if (index > 2)
            index = 2;
        s_setting_font_family = index;
        break;
    case SETTINGS_DD_OPACITY:
        if (index < 0)
            index = 0;
        if (index > 4)
            index = 4;
        s_setting_opacity_index = index;
        break;
    default:
        break;
    }
    settings_apply_all();
}

static bool settings_keybindings_clicked(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    toast("Ctrl+P open · Ctrl+S save · F7 build · F5 run · F12 definition");
    return true;
}

static void settings_close_button_clicked(void *user_data)
{
    (void)user_data;
    s_settings_close_requested = true;
}

static void settings_add_switch_row(AromaNode *parent, int row,
                                    const char *title, const char *desc,
                                    bool value, bool *binding)
{
    int x = SETTINGS_WIN_CONTENT_X;
    int y = SETTINGS_WIN_ROWS_Y + row * SETTINGS_WIN_ROW_H;
    settings_add_label(parent, title, x + 8, y + 8, font_ui(12),
                       SETTINGS_LABEL_TEXT);
    settings_add_label(parent, desc, x + 8, y + 27, font_ui(10),
                       SETTINGS_LABEL_DIM);

    AromaNode *sw = aroma_ui_switch(parent, x + SETTINGS_WIN_CONTENT_W - 62,
                                    y + 12, 46, 24, value,
                                    settings_switch_changed, binding);
    if (sw)
    {
        aroma_node_set_z_index(sw, 4);
        aroma_layout_note_placed(sw);
    }
}

static void settings_add_dropdown_row(AromaNode *parent, int row,
                                      const char *title, const char *desc,
                                      char **options, int option_count,
                                      int selected, SettingsDropdownKind kind)
{
    int x = SETTINGS_WIN_CONTENT_X;
    int y = SETTINGS_WIN_ROWS_Y + row * SETTINGS_WIN_ROW_H;
    settings_add_label(parent, title, x + 8, y + 8, font_ui(12),
                       SETTINGS_LABEL_TEXT);
    settings_add_label(parent, desc, x + 8, y + 27, font_ui(10),
                       SETTINGS_LABEL_DIM);

    SettingsDropdownBinding *binding = NULL;
    if (s_settings_dropdown_count < SETTINGS_DROPDOWN_MAX)
    {
        binding = &s_settings_dropdowns[s_settings_dropdown_count++];
        binding->kind = kind;
    }

    AromaNode *dd = aroma_ui_dropdown(
        parent, x + SETTINGS_WIN_CONTENT_W - 182, y + 9, 170, 32,
        options, option_count, settings_dropdown_changed, binding,
        font_ui(11));
    if (dd)
    {
        aroma_node_set_z_index(dd, 4);
        aroma_layout_note_placed(dd);
        aroma_dropdown_set_max_visible_rows(dd, 6);
        aroma_dropdown_set_selected(dd, selected);
    }
}

static void settings_sidebar_on_select(AromaNode *node, int index,
                                       void *user_data)
{
    (void)node;
    (void)user_data;
    if (index < 0 || index >= SETTING_CAT_COUNT)
        return;
    s_setting_category = index;
    s_setting_tab_scroll = 0;
    s_setting_row_hover = -1;
    if (s_settings_title_label)
        aroma_label_set_text(s_settings_title_label, k_setting_cat_labels[index]);
    if (s_settings_desc_label)
        aroma_label_set_text(s_settings_desc_label, k_setting_cat_desc[index]);
    settings_window_redraw();
}

static bool settings_window_header_drag(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_settings_window || !event || s_settings_window_closing)
        return false;
    if (event->event_type != EVENT_TYPE_MOUSE_CLICK)
        return false;
    int button = event->data.mouse.button;
    if (button == 2 || button == 3)
        return false;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (x >= 0 && x < SETTINGS_WIN_W - 58 &&
        y >= 0 && y < SETTINGS_WIN_HEADER_H)
    {
        aroma_ui_begin_window_drag(s_settings_window);
        return true;
    }
    return false;
}

static void draw_settings_window(void)
{
    if (!s_settings_window_canvas)
        return;

    aroma_canvas_reset(s_settings_window_canvas);
    AromaNode *c = s_settings_window_canvas;

    fill(c, 0, 0, SETTINGS_WIN_W, SETTINGS_WIN_H, 12, C_BG);
    fill(c, 0, 12, SETTINGS_WIN_W, SETTINGS_WIN_H - 12, 0, C_BG);

    fill(c, 0, 0, SETTINGS_WIN_W, SETTINGS_WIN_HEADER_H, 12, C_TITLE);
    fill(c, 0, 12, SETTINGS_WIN_W, SETTINGS_WIN_HEADER_H - 12, 0, C_TITLE);
    line(c, 0, SETTINGS_WIN_HEADER_H - 1, SETTINGS_WIN_W,
         SETTINGS_WIN_HEADER_H - 1, C_BORDER, 1);

    fill(c, 0, SETTINGS_WIN_HEADER_H, SETTINGS_WIN_SIDEBAR_W + 12,
         SETTINGS_WIN_H - SETTINGS_WIN_HEADER_H - SETTINGS_WIN_FOOTER_H,
         0, C_SIDEBAR);
    line(c, SETTINGS_WIN_SIDEBAR_W + 12, SETTINGS_WIN_HEADER_H,
         SETTINGS_WIN_SIDEBAR_W + 12,
         SETTINGS_WIN_H - SETTINGS_WIN_FOOTER_H, C_BORDER_SOFT, 1);

    int cat = s_setting_category;
    if (cat < 0 || cat >= SETTING_CAT_COUNT)
        cat = 0;
    int rows = k_setting_cat_rows[cat];
    for (int i = 0; i < rows; i++)
    {
        int ry = SETTINGS_WIN_ROWS_Y + i * SETTINGS_WIN_ROW_H;
        line(c, SETTINGS_WIN_CONTENT_X, ry + SETTINGS_WIN_ROW_H - 1,
             SETTINGS_WIN_CONTENT_X + SETTINGS_WIN_CONTENT_W,
             ry + SETTINGS_WIN_ROW_H - 1, C_BORDER_SOFT, 1);
    }

    line(c, 0, SETTINGS_WIN_H - SETTINGS_WIN_FOOTER_H, SETTINGS_WIN_W,
         SETTINGS_WIN_H - SETTINGS_WIN_FOOTER_H, C_BORDER, 1);
}

static void settings_window_redraw(void)
{
    draw_settings_window();
    if (s_settings_window_canvas)
        aroma_node_invalidate(s_settings_window_canvas);
    if (s_settings_window)
        aroma_ui_request_redraw(s_settings_window);
}

static void settings_window_finish_close(void)
{
    s_settings_window_opening = false;
    s_settings_window_closing = false;
    s_settings_window_anim_opacity = 0.0f;
    s_settings_visible = false;
    s_settings_close_requested = false;
    s_setting_row_hover = -1;
    s_settings_win_close_hover = false;
    s_setting_tab_scroll = 0;

    if (s_settings_window)
    {
        aroma_ui_destroy_window(s_settings_window);
        s_settings_window = NULL;
    }

    s_settings_window_canvas = NULL;
    s_settings_window_root = NULL;
    s_settings_sidebar = NULL;
    s_settings_title_label = NULL;
    s_settings_desc_label = NULL;
    s_settings_experimental_badge = NULL;
    s_settings_close_button = NULL;
    s_settings_label_count = 0;
    s_settings_dropdown_count = 0;

    draw_rail();
    draw_title();
    draw_all();
}

static void settings_window_close_finished(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_settings_window_closing)
        return;
    settings_window_finish_close();
}

static void settings_window_close(void)
{
    if (s_settings_window_closing)
        return;
    if (!s_settings_visible && !s_settings_window)
        return;

    s_settings_visible = false;
    s_settings_close_requested = false;
    s_setting_row_hover = -1;
    s_settings_win_close_hover = false;
    s_setting_tab_scroll = 0;

    if (!s_settings_window || !s_settings_window_root)
    {
        settings_window_finish_close();
        return;
    }

    /* A close during the entrance fade should start from the opacity that is
     * actually on screen, not from the configured target value. */
    if (s_settings_window_opening)
    {
        aroma_animation_stop(s_settings_window_root);
        s_settings_window_opening = false;
    }

    s_settings_window_closing = true;
    float start = s_settings_window_anim_opacity;
    if (start <= 0.001f)
    {
        settings_window_finish_close();
        return;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        s_settings_window_root, start, 0.0f, 150u,
        settings_window_fade_step, NULL);
    if (!anim)
    {
        settings_window_finish_close();
        return;
    }
    aroma_animation_set_visual(anim, false);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_QUAD);
    aroma_animation_set_on_complete(anim, settings_window_close_finished);
}

static void settings_window_begin_open_fade(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_settings_window || !s_settings_window_root ||
        !s_settings_window_opening)
        return;

    AromaAnimation *anim = aroma_animation_start_custom(
        s_settings_window_root, 0.0f, 1.0f, 220u,
        settings_window_open_fade_step, NULL);
    if (!anim)
    {
        s_settings_window_opening = false;
        s_settings_window_anim_opacity = 1.0f;
        settings_window_apply_opacity(settings_window_opacity());
        return;
    }
    aroma_animation_set_visual(anim, false);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_animation_set_on_complete(anim, settings_window_open_finished);
}

static void settings_window_open(void)
{
    if (s_settings_window_closing)
        settings_window_finish_close();
    if (s_settings_window && !s_settings_visible)
        settings_window_finish_close();
    if (s_settings_visible && s_settings_window)
    {
        settings_window_redraw();
        return;
    }

    s_settings_window = aroma_ui_create_window("Settings", SETTINGS_WIN_W,
                                               SETTINGS_WIN_H);
    if (!s_settings_window)
    {
        toast("Could not open Settings window");
        return;
    }

    s_settings_window_opening = false;
    s_settings_window_closing = false;
    s_settings_window_anim_opacity = 0.0f;
    aroma_ui_set_window_opacity(s_settings_window, 0.0f);
    aroma_ui_set_window_decorated(s_settings_window, false);

    s_settings_window_root = NULL;
    for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
    {
        if (g_windows[i].is_active && g_windows[i].window == s_settings_window)
        {
            s_settings_window_root = g_windows[i].root_node;
            break;
        }
    }
    if (!s_settings_window_root)
    {
        aroma_ui_destroy_window(s_settings_window);
        s_settings_window = NULL;
        toast("Could not open Settings window");
        return;
    }

    s_settings_label_count = 0;
    s_settings_dropdown_count = 0;

    s_settings_window_canvas = aroma_canvas_create(
        s_settings_window_root, 0, 0, SETTINGS_WIN_W, SETTINGS_WIN_H);
    if (!s_settings_window_canvas)
    {
        aroma_ui_destroy_window(s_settings_window);
        s_settings_window = NULL;
        s_settings_window_root = NULL;
        toast("Could not open Settings window");
        return;
    }
    aroma_node_set_z_index(s_settings_window_canvas, 0);
    aroma_layout_note_placed(s_settings_window_canvas);
    aroma_node_set_layout_fill(s_settings_window_canvas);
    subscribe_node(s_settings_window_canvas, EVENT_TYPE_MOUSE_CLICK,
                   settings_window_header_drag);

    AromaNode *header_title = settings_add_label(
        s_settings_window_root, "Settings", 48, 12, font_ui(16),
        SETTINGS_LABEL_TEXT);
    AromaNode *header_subtitle = settings_add_label(
        s_settings_window_root, "IDE preferences", 48, 35, font_ui(10),
        SETTINGS_LABEL_DIM);
    if (header_title)
        aroma_node_set_hit_test_cb(header_title, settings_no_hit, NULL);
    if (header_subtitle)
        aroma_node_set_hit_test_cb(header_subtitle, settings_no_hit, NULL);

    s_settings_close_button = aroma_ui_iconbutton(
        s_settings_window_root, AROMA_ICON_CLOSE, SETTINGS_WIN_W - 48, 17,
        30, ICON_BUTTON_STANDARD, settings_close_button_clicked, NULL,
        font_icon(16));
    if (s_settings_close_button)
    {
        aroma_node_set_z_index(s_settings_close_button, 5);
        aroma_layout_note_placed(s_settings_close_button);
        aroma_iconbutton_set_colors(s_settings_close_button, C_TITLE,
                                    C_TEXT_SOFT);
    }

    s_settings_sidebar = aroma_ui_sidebar_with_icons(
        s_settings_window_root, SETTINGS_WIN_SIDEBAR_X,
        SETTINGS_WIN_SIDEBAR_Y, SETTINGS_WIN_SIDEBAR_W,
        SETTINGS_WIN_SIDEBAR_H, (const char **)k_setting_cat_labels,
        (const char **)k_setting_cat_icons, SETTING_CAT_COUNT,
        settings_sidebar_on_select, NULL, font_ui(12), font_icon(18));
    if (!s_settings_sidebar)
    {
        settings_window_close();
        toast("Could not build Settings sidebar");
        return;
    }
    aroma_node_set_z_index(s_settings_sidebar, 3);
    aroma_layout_note_placed(s_settings_sidebar);
    aroma_sidebar_set_style(s_settings_sidebar, true, 14, 4, 10);
    int selected = s_setting_category;
    if (selected < 0 || selected >= SETTING_CAT_COUNT)
        selected = 0;
    aroma_sidebar_set_selected(s_settings_sidebar, selected);

    s_settings_title_label = settings_add_label(
        s_settings_window_root, k_setting_cat_labels[selected],
        SETTINGS_WIN_CONTENT_X, 84, font_ui(16), SETTINGS_LABEL_TEXT);
    s_settings_desc_label = settings_add_label(
        s_settings_window_root, k_setting_cat_desc[selected],
        SETTINGS_WIN_CONTENT_X, 109, font_ui(10), SETTINGS_LABEL_DIM);

    for (int i = 0; i < SETTING_CAT_COUNT; i++)
    {
        int height = k_setting_cat_rows[i] * SETTINGS_WIN_ROW_H;
        AromaNode *content = aroma_container_create(
            s_settings_window_root, SETTINGS_WIN_CONTENT_X,
            SETTINGS_WIN_ROWS_Y, SETTINGS_WIN_CONTENT_W, height);
        if (!content)
            continue;
        aroma_node_set_z_index(content, 1);
        aroma_layout_note_placed(content);

        if (i == SETTING_CAT_EDITOR)
        {
            settings_add_switch_row(content, 0, "Line numbers",
                                    "Show line numbers in the editor",
                                    s_setting_line_numbers,
                                    &s_setting_line_numbers);
            settings_add_switch_row(content, 1, "Minimap", "Show code minimap",
                                    s_setting_minimap, &s_setting_minimap);
            char *tab_options[] = {"2 spaces", "4 spaces", "8 spaces"};
            settings_add_dropdown_row(content, 2, "Tab width",
                                      "Spaces per indentation level",
                                      tab_options, 3,
                                      settings_tab_width_index(),
                                      SETTINGS_DD_TAB_WIDTH);
            char *size_options[] = {"10", "11", "12", "14", "16"};
            settings_add_dropdown_row(content, 3, "Font size",
                                      "Editor font size in px", size_options, 5,
                                      settings_font_size_index(),
                                      SETTINGS_DD_FONT_SIZE);
            settings_add_switch_row(content, 4, "Word wrap", "Wrap long lines",
                                    s_setting_word_wrap, &s_setting_word_wrap);
            settings_add_switch_row(content, 5, "Auto indent",
                                    "Automatic indentation",
                                    s_setting_auto_indent,
                                    &s_setting_auto_indent);
            settings_add_switch_row(content, 6, "Bracket matching",
                                    "Highlight matching brackets",
                                    s_setting_bracket_match,
                                    &s_setting_bracket_match);
        }
        else if (i == SETTING_CAT_THEME)
        {
            settings_add_switch_row(content, 0, "Dark theme",
                                    "Use dark color theme",
                                    s_setting_theme_dark,
                                    &s_setting_theme_dark);
            char *accent_options[] = {"Blue", "Purple", "Green", "Orange",
                                      "Red"};
            int accent = s_setting_accent;
            if (accent < 0)
                accent = 0;
            if (accent > 4)
                accent = 4;
            settings_add_dropdown_row(content, 1, "Accent color",
                                      "UI accent color", accent_options, 5,
                                      accent, SETTINGS_DD_ACCENT);
            char *font_options[] = {"Monospace", "Sans", "Serif"};
            int family = s_setting_font_family;
            if (family < 0)
                family = 0;
            if (family > 2)
                family = 2;
            settings_add_dropdown_row(content, 2, "Font family",
                                      "UI font family", font_options, 3, family,
                                      SETTINGS_DD_FONT_FAMILY);
        }
        else if (i == SETTING_CAT_WINDOW)
        {
            settings_add_switch_row(content, 0, "Transparency",
                                    "Make application windows translucent",
                                    s_setting_transparency,
                                    &s_setting_transparency);
            char *opacity_options[] = {"80%", "86%", "90%", "94%", "100%"};
            int opacity = s_setting_opacity_index;
            if (opacity < 0)
                opacity = 0;
            if (opacity > 4)
                opacity = 4;
            settings_add_dropdown_row(content, 1, "Window opacity",
                                      "Translucency used when transparency is on",
                                      opacity_options, 5, opacity,
                                      SETTINGS_DD_OPACITY);
            settings_add_switch_row(content, 2, "Experimental features",
                                    "Enable in-development UI effects and badges",
                                    s_setting_experimental,
                                    &s_setting_experimental);
        }
        else if (i == SETTING_CAT_OUTPUT)
        {
            settings_add_switch_row(content, 0, "Show build output",
                                    "Display build output in panel",
                                    s_setting_show_build_output,
                                    &s_setting_show_build_output);
            settings_add_switch_row(content, 1, "Show run output",
                                    "Display run output in panel",
                                    s_setting_show_run_output,
                                    &s_setting_show_run_output);
            settings_add_switch_row(content, 2, "Auto-scroll output",
                                    "Scroll output to bottom automatically",
                                    s_setting_auto_scroll_output,
                                    &s_setting_auto_scroll_output);
        }
        else if (i == SETTING_CAT_FILE)
        {
            settings_add_switch_row(content, 0, "Auto save",
                                    "Save files automatically",
                                    s_setting_auto_save, &s_setting_auto_save);
            settings_add_switch_row(content, 1, "Save on focus change",
                                    "Save when editor loses focus",
                                    s_setting_save_on_focus,
                                    &s_setting_save_on_focus);
            settings_add_switch_row(content, 2, "Trim trailing whitespace",
                                    "Remove trailing whitespace on save",
                                    s_setting_trim_trailing,
                                    &s_setting_trim_trailing);
            settings_add_switch_row(content, 3, "Final newline",
                                    "Ensure file ends with newline",
                                    s_setting_final_newline,
                                    &s_setting_final_newline);
        }
        else if (i == SETTING_CAT_KEYBOARD)
        {
            settings_add_label(content, "Keyboard shortcuts", 232, 158,
                               font_ui(12), SETTINGS_LABEL_TEXT);
            settings_add_label(content,
                               "Show the complete keyboard shortcuts reference",
                               232, 177, font_ui(10), SETTINGS_LABEL_DIM);
            AromaNode *button = aroma_ui_button_with_type(
                content, "View keybindings",
                SETTINGS_WIN_CONTENT_X + SETTINGS_WIN_CONTENT_W - 182, 160,
                170, 32, BUTTON_TYPE_TONAL, settings_keybindings_clicked,
                NULL, font_ui(11));
            if (button)
            {
                aroma_node_set_z_index(button, 4);
                aroma_layout_note_placed(button);
            }
        }
        else if (i == SETTING_CAT_ABOUT)
        {
            settings_add_label(content, "AromaUI Studio", 232, 158,
                               font_ui(13), SETTINGS_LABEL_TEXT);
            settings_add_label(content, "Alpha 0.0.1", 232, 180, font_ui(10),
                               SETTINGS_LABEL_DIM);
            settings_add_label(content, "Built with AromaUI", 232, 210,
                               font_ui(10), SETTINGS_LABEL_SOFT);
            settings_add_label(content, "Modern native interfaces, live in C",
                               232, 226, font_ui(10), SETTINGS_LABEL_FAINT);
        }

        AromaNode *content_nodes[1] = {content};
        aroma_sidebar_set_content(s_settings_sidebar, i, content_nodes, 1);
    }

    settings_add_label(s_settings_window_root, "Changes apply immediately.",
                       SETTINGS_WIN_CONTENT_X, SETTINGS_WIN_H - 32,
                       font_ui(10), SETTINGS_LABEL_FAINT);
    s_settings_experimental_badge = settings_add_label(
        s_settings_window_root, "EXPERIMENTAL",
        SETTINGS_WIN_W - 148, SETTINGS_WIN_H - 32, font_ui(9),
        SETTINGS_LABEL_ACCENT);
    if (s_settings_experimental_badge)
        aroma_node_set_hidden(s_settings_experimental_badge,
                              !s_setting_experimental);

    s_setting_row_hover = -1;
    s_settings_win_close_hover = false;
    s_setting_tab_scroll = 0;
    s_settings_visible = true;
    s_settings_close_requested = false;

    /* The settings window fades in through the Aroma animation system while
     * the main window stays at its configured opacity. */
    AromaWindow *main_window = aroma_ui_get_window_at(0);
    if (main_window)
        aroma_ui_set_window_opacity(main_window, settings_window_opacity());
    settings_apply_experimental();
    settings_window_redraw();

    s_settings_window_opening = true;
    settings_window_apply_opacity(0.0f);

    /* Hold the window at zero opacity through its first content render.  The
     * one-shot animation completes on the next tick, after the layout and
     * first paint have had time to finish, so the entrance fade starts with a
     * real frame instead of losing most of its duration to setup cost. */
    AromaAnimation *hold = aroma_animation_start_custom(
        s_settings_window_root, 0.0f, 0.0f, 1u, NULL, NULL);
    if (!hold)
    {
        settings_window_begin_open_fade(NULL, NULL);
        return;
    }
    aroma_animation_set_visual(hold, false);
    aroma_animation_set_on_complete(hold, settings_window_begin_open_fade);
}

static void settings_apply_ui_fonts(void)
{
    if (!s_assets)
        return;

    AromaFont **fonts = ui_font_set();
    for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
    {
        AromaFont *font = fonts ? fonts[i] : NULL;
        if (!font)
            font = s_assets->ui[i];
        if (font)
            IncenseRegisterFontSize(ide_ui_font_sizes[i], font);
    }
}

static void settings_apply_theme(void)
{
    s_theme_is_dark = s_setting_theme_dark;

    AromaTheme theme = s_theme_is_dark ? aroma_theme_create_material_black()
                                       : aroma_theme_create_default();
    theme.colors.primary = M3_PRIMARY;
    theme.colors.primary_dark = M3_PRIMARY_CONTAINER;
    theme.colors.primary_light = M3_ON_PRIMARY_CONTAINER;
    theme.colors.secondary = M3_SECONDARY_CONTAINER;
    theme.colors.background = M3_SURFACE;
    theme.colors.surface = M3_SURFACE_LOW;
    theme.colors.text_primary = M3_ON_SURFACE;
    theme.colors.text_secondary = M3_TEXT_DIM;
    theme.colors.border = M3_OUTLINE_VAR;
    theme.colors.error = M3_ERROR;
    aroma_ui_set_theme(&theme);
    s_ide_theme = theme;
    settings_refresh_label_colors();
    settings_apply_ui_fonts();
    apply_editor_palette();
    if (s_markdown_visible)
        markdown_preview_create();
}

static void settings_apply_editor(void)
{
    if (!s_editor)
        return;
    aroma_code_editor_set_font(s_editor, font_mono(s_setting_font_size));
    aroma_code_editor_set_line_numbers(s_editor, s_setting_line_numbers);
    aroma_code_editor_set_tab_width(s_editor, s_setting_tab_width);
    aroma_code_editor_set_word_wrap(s_editor, s_setting_word_wrap);
    aroma_code_editor_set_auto_indent(s_editor, s_setting_auto_indent);
    aroma_code_editor_set_bracket_match(s_editor, s_setting_bracket_match);
    aroma_code_editor_set_minimap(s_editor, s_setting_minimap);
}

static void settings_apply_all(void)
{
    settings_apply_theme();
    settings_apply_windows();
    settings_apply_experimental();
    settings_apply_editor();
    draw_all();
    if (s_settings_visible)
        settings_window_redraw();
}

static void settings_row_toggle(int category, int row)
{
    if (category == SETTING_CAT_EDITOR)
    {
        if (row == 0)
            s_setting_line_numbers = !s_setting_line_numbers;
        else if (row == 1)
            s_setting_minimap = !s_setting_minimap;
        else if (row == 4)
            s_setting_word_wrap = !s_setting_word_wrap;
        else if (row == 5)
            s_setting_auto_indent = !s_setting_auto_indent;
        else if (row == 6)
            s_setting_bracket_match = !s_setting_bracket_match;
    }
    else if (category == SETTING_CAT_THEME)
    {
        if (row == 0)
            s_setting_theme_dark = !s_setting_theme_dark;
    }
    else if (category == SETTING_CAT_WINDOW)
    {
        if (row == 0)
            s_setting_transparency = !s_setting_transparency;
        else if (row == 2)
            s_setting_experimental = !s_setting_experimental;
    }
    else if (category == SETTING_CAT_OUTPUT)
    {
        if (row == 0)
            s_setting_show_build_output = !s_setting_show_build_output;
        else if (row == 1)
            s_setting_show_run_output = !s_setting_show_run_output;
        else if (row == 2)
            s_setting_auto_scroll_output = !s_setting_auto_scroll_output;
    }
    else if (category == SETTING_CAT_FILE)
    {
        if (row == 0)
            s_setting_auto_save = !s_setting_auto_save;
        else if (row == 1)
            s_setting_save_on_focus = !s_setting_save_on_focus;
        else if (row == 2)
            s_setting_trim_trailing = !s_setting_trim_trailing;
        else if (row == 3)
            s_setting_final_newline = !s_setting_final_newline;
    }
    settings_apply_all();
}

static void settings_row_cycle(int category, int row)
{
    if (category == SETTING_CAT_EDITOR)
    {
        if (row == 2)
        {
            s_setting_tab_width += 2;
            if (s_setting_tab_width > 8)
                s_setting_tab_width = 2;
        }
        else if (row == 3)
        {
            static const int sizes[] = {10, 11, 12, 14, 16};
            int index = 2;
            for (int i = 0; i < 5; i++)
            {
                if (sizes[i] == s_setting_font_size)
                {
                    index = i;
                    break;
                }
            }
            s_setting_font_size = sizes[(index + 1) % 5];
        }
    }
    else if (category == SETTING_CAT_THEME)
    {
        if (row == 1)
        {
            s_setting_accent++;
            if (s_setting_accent > 4)
                s_setting_accent = 0;
        }
        else if (row == 2)
        {
            s_setting_font_family++;
            if (s_setting_font_family > 2)
                s_setting_font_family = 0;
        }
    }
    else if (category == SETTING_CAT_WINDOW)
    {
        if (row == 1)
        {
            s_setting_opacity_index++;
            if (s_setting_opacity_index > 4)
                s_setting_opacity_index = 0;
        }
    }
    settings_apply_all();
}

static bool settings_on_click(int x, int y)
{
    int sx = SIDEBAR_DRAW_X;
    int cat_w = s_sidebar_w / 2;
    int item_x = sx + cat_w + 20;

    int cy = TITLE_H + 80;
    int cat_h = 44;

                        
    if (x >= sx && x < sx + cat_w && y >= cy)
    {
        int cat = (y - cy) / cat_h;
        if (cat >= 0 && cat < SETTING_CAT_COUNT)
        {
            s_setting_category = cat;
            s_setting_row_hover = -1;
            s_setting_scroll = 0;
            draw_sidebar();
            return true;
        }
    }

                           
    int iy = TITLE_H + 80;
    int item_h = 46;
    int bottom = s_panel_visible ? panel_y : status_y;
    int visible = (bottom - iy) / item_h;
    if (visible < 1)
        visible = 1;

    const int *cat_rows = k_setting_cat_rows;

                                                       
    typedef struct
    {
        int cat;
        int row;
        bool is_header;
    } SettingEntry;
    static SettingEntry entries[64];
    int entry_count = 0;
    for (int c = 0; c < SETTING_CAT_COUNT; c++)
    {
        entries[entry_count].cat = c;
        entries[entry_count].row = -1;
        entries[entry_count].is_header = true;
        entry_count++;
        for (int r = 0; r < cat_rows[c]; r++)
        {
            entries[entry_count].cat = c;
            entries[entry_count].row = r;
            entries[entry_count].is_header = false;
            entry_count++;
        }
    }
    int total = entry_count;

                      
    int max_scroll = total - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (s_setting_scroll > max_scroll)
        s_setting_scroll = max_scroll;
    if (s_setting_scroll < 0)
        s_setting_scroll = 0;

    int item_y = iy;
    int entry_idx = 0;
    for (int i = s_setting_scroll; i < total; i++)
    {
        int c = entries[i].cat;
        int r = entries[i].row;
        bool is_header = entries[i].is_header;

        if (is_header)
        {
            item_y += 22;
            entry_idx++;
            continue;
        }

        int ry = item_y;
            if (y >= ry - 2 && y < ry + item_h && x >= item_x &&
                x < sx + s_sidebar_w - 40)
            {
                s_setting_row_hover = i;
                if (c == SETTING_CAT_KEYBOARD && r == 0)
                {
                    draw_sidebar();
                    return true;
                }
                if (c != SETTING_CAT_KEYBOARD)
                {
                    bool is_select = false;
                    if (c == SETTING_CAT_EDITOR && (r == 2 || r == 3))
                        is_select = true;
                    if (c == SETTING_CAT_THEME && (r == 1 || r == 2))
                    if (c == SETTING_CAT_WINDOW && r == 1)
                        is_select = true;
                    if (is_select)
                        settings_row_cycle(c, r);
                    else
                        settings_row_toggle(c, r);
                }
                return true;
            }
            item_y += item_h;
            entry_idx++;
        }
    return false;
}

static void draw_sidebar(void)
{
    if (!s_sidebar_canvas)
        return;
    aroma_canvas_reset(s_sidebar_canvas);
    fill(s_sidebar_canvas, SIDEBAR_DRAW_X, TITLE_H, s_sidebar_w,
         (s_panel_visible ? panel_y : status_y) - TITLE_H, 0, C_SIDEBAR);
    for (int i = 0; i < 4; i++)
        s_script_y[i] = -1;
    for (int i = 0; i < SEARCH_MAX_HITS; i++)
        s_search_y[i] = -1;
    for (int i = 0; i < 4; i++)
        s_run_y[i] = -1;

    draw_sidebar_header();
    switch (s_sidebar_mode)
    {
    case 0:
        draw_explorer();
        break;
    case 1:
        draw_search();
        break;
    default:
        break;
    }
}

static void draw_tabs(void)
{
    if (!s_tabs_canvas)
        return;
    aroma_canvas_reset(s_tabs_canvas);
    fill(s_tabs_canvas, EDITOR_X, TITLE_H, editor_w, TAB_H, 0, C_TAB_BG);
    AromaFont *ui12 = font_ui(12);

    int width = tab_width();
    int tabs_right = right_x - TAB_ACTION_W;
    for (int i = 0; i < s_tab_count; i++)
    {
        int x = EDITOR_X + i * width;
        if (x >= tabs_right)
            break;
        int draw_width = width;
        if (x + draw_width > tabs_right)
            draw_width = tabs_right - x;
        if (draw_width <= 0)
            break;
        int file_index = s_tabs[i];
        bool active = file_index == s_active_file;
        bool hovered = s_tab_hover == i;
        if (active && draw_width > 20)
        {
            fill(s_tabs_canvas, x + 10, TITLE_H + TAB_H - 3, draw_width - 20, 3, 2,
                 C_PRIMARY);
        }
        else if (hovered && draw_width > 4)
        {
            fill(s_tabs_canvas, x + 2, TITLE_H + 4, draw_width - 4, TAB_H - 8, 12,
                 C_HOVER_SOFT);
        }

        icon_center(s_tabs_canvas, file_icon(file_index), x + 14, TITLE_H + 18, 14,
                    file_icon_color(file_index));
        text_fit(s_tabs_canvas, base_name(ide_files[file_index].path), x + 28,
                 TITLE_H + 10, draw_width > 90 ? draw_width - 90 : 0,
                 active ? C_PRIMARY : C_TEXT_SOFT, ui12);

        if (active && draw_width >= 28)
        {
            icon_center(s_tabs_canvas, AROMA_ICON_CLOSE, x + draw_width - 18,
                        TITLE_H + 18, 12, C_TEXT_SOFT);
        }
        else if (hovered && s_tab_close_hover && i == s_tab_hover &&
                 draw_width >= 26)
        {
            fill(s_tabs_canvas, x + draw_width - 26, TITLE_H + 9, 18, 18, 9, C_HOVER);
            icon_center(s_tabs_canvas, AROMA_ICON_CLOSE, x + draw_width - 17,
                        TITLE_H + 18, 12, C_TEXT);
        }
        else if (ide_files[file_index].dirty)
        {
            fill(s_tabs_canvas, x + draw_width - 22, TITLE_H + 15, 7, 7, 4, C_PRIMARY);
        }
    }

    line(s_tabs_canvas, EDITOR_X, TITLE_H + TAB_H - 1, right_x,
         TITLE_H + TAB_H - 1, C_BORDER, 1);

    icon_center(s_tabs_canvas, AROMA_ICON_FULLSCREEN, right_x - 31,
                TITLE_H + 18, 14, C_TEXT_DIM);
    icon_center(s_tabs_canvas, AROMA_ICON_MORE_HORIZ, right_x - 14,
                TITLE_H + 18, 16, C_TEXT_DIM);
}

static void draw_breadcrumb(void)
{
    if (!s_breadcrumb_canvas)
        return;
    aroma_canvas_reset(s_breadcrumb_canvas);
    fill(s_breadcrumb_canvas, EDITOR_X, TITLE_H + TAB_H, editor_w, BREADCRUMB_H,
         0, C_BREADCRUMB);
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);

    if (s_active_file >= 0)
    {
        IdeFile *file = &ide_files[s_active_file];
        icon_center(s_breadcrumb_canvas, file_icon(s_active_file), EDITOR_X + 16,
                    TITLE_H + TAB_H + 14, 13, file_icon_color(s_active_file));
        int x = EDITOR_X + 30;
        const char *part = file->path;
        while (part && *part)
        {
            const char *slash = strchr(part, '/');
            int length = slash ? (int)(slash - part) : (int)strlen(part);
            if (length <= 0)
                break;
            char segment[80];
            int copy = length < (int)sizeof(segment) - 1 ? length : (int)sizeof(segment) - 1;
            memcpy(segment, part, (size_t)copy);
            segment[copy] = '\0';
            text(s_breadcrumb_canvas, segment, x, TITLE_H + TAB_H + 7,
                 slash ? C_TEXT_DIM : C_TEXT_SOFT, ui11);
            x += text_width(ui11, segment);
            if (slash)
            {
                icon_center(s_breadcrumb_canvas, AROMA_ICON_CHEVRON_RIGHT, x + 8,
                            TITLE_H + TAB_H + 14, 12, C_TEXT_FAINT);
                x += 17;
                part = slash + 1;
            }
            else
            {
                break;
            }
        }

        const char *label = language_label(file->language);
        int label_width = text_width(ui10, label);
        int design_reserved = s_designer_available ? 92 : 0;
        text(s_breadcrumb_canvas, label,
             right_x - 38 - design_reserved - label_width,
             TITLE_H + TAB_H + 8, C_TEXT_FAINT, ui10);
        if (s_designer_available)
        {
            const char *toggle = s_designer_visible ? "Code" : "Design";
            int toggle_width = text_width(ui10, toggle);
            text(s_breadcrumb_canvas, toggle, right_x - 34 - toggle_width,
                 TITLE_H + TAB_H + 8,
                 s_designer_visible ? C_PRIMARY : C_TEXT_SOFT, ui10);
            icon_center(s_breadcrumb_canvas,
                        s_designer_visible ? AROMA_ICON_CODE
                                           : AROMA_ICON_WIDGETS,
                        right_x - 20, TITLE_H + TAB_H + 14, 13,
                        s_designer_visible ? C_PRIMARY : C_TEXT_DIM);
        }
        else
        {
            icon_center(s_breadcrumb_canvas, AROMA_ICON_WIDGETS, right_x - 20,
                        TITLE_H + TAB_H + 14, 13, C_TEXT_DIM);
        }
    }

    line(s_breadcrumb_canvas, EDITOR_X, EDITOR_Y - 1, right_x, EDITOR_Y - 1,
         C_BORDER, 1);
}

#define OUTLINE_ROWS_TOP (TITLE_H + 74)
#define OUTLINE_ROW_H 20
#define OUTLINE_FOOTER_H 84

static int outline_rows_bottom(void)
{
    if (s_designer_visible)
        return status_y - OUTLINE_FOOTER_H;
    return status_y - 10;
}

static int outline_visible_rows(void)
{
    int rows = (outline_rows_bottom() - OUTLINE_ROWS_TOP) / OUTLINE_ROW_H;
    return rows > 1 ? rows : 1;
}

static int outline_row_count(void)
{
    if (s_designer_visible)
        return s_design_doc.node_count;
    return s_outline_count;
}

static void outline_clamp_scroll(void)
{
    int count = outline_row_count();
    int visible = outline_visible_rows();
    int max_scroll = count - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (s_outline_scroll > max_scroll)
        s_outline_scroll = max_scroll;
    if (s_outline_scroll < 0)
        s_outline_scroll = 0;
}

static void draw_outline(void)
{
    if (!s_outline_canvas || win_w - right_x <= 0)
        return;

    aroma_canvas_reset(s_outline_canvas);
    int w = win_w - right_x;
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    AromaFont *ui9 = font_ui(9);

    fill(s_outline_canvas, right_x, TITLE_H, w, status_y - TITLE_H, 0,
         C_SIDEBAR);

    /* Resize handle strip on the left edge */
    uint32_t handle_col = s_resizing_outline ? C_PRIMARY : C_BORDER;
    fill(s_outline_canvas, right_x, TITLE_H, 3, status_y - TITLE_H, 0, handle_col);

                
    icon_center(s_outline_canvas, AROMA_ICON_LIST, right_x + 16,
                TITLE_H + 15, 12, C_TEXT_DIM);
    text(s_outline_canvas, "OUTLINE", right_x + 28, TITLE_H + 9, C_TEXT, ui10);
    if (s_designer_visible)
    {
        text_fit(s_outline_canvas, "UI DESIGNER", right_x + 14,
                 TITLE_H + 26, w - 28, C_PRIMARY, ui9);
    }
    else if (s_active_file >= 0 && s_active_file < ide_file_count)
    {
        text_fit(s_outline_canvas, base_name(ide_files[s_active_file].path),
                 right_x + 14, TITLE_H + 26, w - 28, C_TEXT_FAINT, ui9);
    }
    line(s_outline_canvas, right_x + 10, TITLE_H + 44, right_x + w - 10,
         TITLE_H + 44, C_BORDER, 1);
    text(s_outline_canvas, s_designer_visible ? "LAYERS" : "SYMBOLS",
         right_x + 14, TITLE_H + 54, C_TEXT_DIM, ui9);

    outline_clamp_scroll();
    int count = outline_row_count();
    int visible = outline_visible_rows();
    int bottom = outline_rows_bottom();
    int row_y = OUTLINE_ROWS_TOP;

    if (count == 0)
    {
        icon_center(s_outline_canvas, s_designer_visible ? AROMA_ICON_WIDGETS
                                                         : AROMA_ICON_CODE,
                    right_x + w / 2, TITLE_H + 120, 18, C_TEXT_FAINT);
        text_fit(s_outline_canvas,
                 s_designer_visible ? "No widgets on this page"
                                    : "No symbols in this file",
                 right_x + 12, TITLE_H + 138, w - 24, C_TEXT_FAINT, ui10);
    }

    for (int row = s_outline_scroll; row < count && row_y < bottom; row++)
    {
        bool selected = false;
        bool hovered = row == s_outline_hover;
        const char *label = NULL;
        const char *icon = AROMA_ICON_CODE;
        char detail[64];
        detail[0] = '\0';
        int indent = 0;

        if (s_designer_visible)
        {
            const IdeDesignNode *node = &s_design_doc.nodes[row];
            selected = row == s_design_doc.selected;
            label = node->tag;
            indent = node->depth * 9;
            if (indent > 36)
                indent = 36;
            icon = (row == s_design_doc.root) ? AROMA_ICON_LAYERS
                                              : AROMA_ICON_WIDGETS;
        }
        else
        {
            const AromaCodeSymbol *symbol = &s_outline[row];
            label = symbol->name;
            snprintf(detail, sizeof(detail), "%d", symbol->line);
            icon = AROMA_ICON_CODE;
        }

        if (selected)
            fill(s_outline_canvas, right_x + 6, row_y - 2, w - 12,
                 OUTLINE_ROW_H, 8, C_SELECT);
        else if (hovered)
            fill(s_outline_canvas, right_x + 6, row_y - 2, w - 12,
                 OUTLINE_ROW_H, 3, C_HOVER_SOFT);

        if (label)
        {
            int text_x = right_x + 20 + indent;
            int right_pad = detail[0] ? 34 : 8;
            icon_center(s_outline_canvas, icon, right_x + 15 + indent,
                        row_y + 7, 11,
                        selected ? C_PRIMARY : C_TEXT_FAINT);
            text_fit(s_outline_canvas, label, text_x, row_y,
                     right_x + w - right_pad - text_x,
                     selected ? C_TEXT : C_TEXT_SOFT, ui10);
            if (detail[0])
                text(s_outline_canvas, detail, right_x + w - 26, row_y + 1,
                     C_TEXT_FAINT, ui9);
        }
        row_y += OUTLINE_ROW_H;
    }

                   
    if (count > visible)
    {
        int track_y = OUTLINE_ROWS_TOP - 2;
        int track_h = visible * OUTLINE_ROW_H;
        int thumb_h = track_h * visible / count;
        if (thumb_h < 24)
            thumb_h = 24;
        int max_scroll = count - visible;
        int thumb_y = track_y;
        if (max_scroll > 0)
            thumb_y += (track_h - thumb_h) * s_outline_scroll / max_scroll;
        fill(s_outline_canvas, right_x + w - 5, track_y, 3, track_h, 2,
             C_HOVER_SOFT);
        fill(s_outline_canvas, right_x + w - 5, thumb_y, 3, thumb_h, 2,
             C_TEXT_FAINT);
    }

                                                       
    if (s_designer_visible)
    {
        int footer_y = status_y - OUTLINE_FOOTER_H;
        line(s_outline_canvas, right_x + 10, footer_y, right_x + w - 10,
             footer_y, C_BORDER, 1);
        text(s_outline_canvas, "SELECTED", right_x + 14, footer_y + 10,
             C_TEXT_DIM, ui9);
        if (s_design_doc.selected > 0 &&
            s_design_doc.selected < s_design_doc.node_count)
        {
            const IdeDesignNode *node = &s_design_doc.nodes[s_design_doc.selected];
            char detail[64];
            text_fit(s_outline_canvas, node->tag, right_x + 14, footer_y + 25,
                     w - 28, C_PRIMARY, ui11);
            snprintf(detail, sizeof(detail), "%d, %d  ·  %dx%d", node->x,
                     node->y, node->width, node->height);
            text_fit(s_outline_canvas, detail, right_x + 14, footer_y + 42,
                     w - 28, C_TEXT_SOFT, ui9);
            text_fit(s_outline_canvas, "Double-click to edit properties",
                     right_x + 14, footer_y + 58, w - 28, C_TEXT_FAINT, ui9);
        }
        else
        {
            text_fit(s_outline_canvas, "Nothing selected", right_x + 14,
                     footer_y + 27, w - 28, C_TEXT_FAINT, ui10);
            text_fit(s_outline_canvas, "Click a layer to select it",
                     right_x + 14, footer_y + 46, w - 28, C_TEXT_FAINT, ui9);
        }
    }
        else
        {
            char detail[64];
            snprintf(detail, sizeof(detail), "%d symbol%s", count,
                     count == 1 ? "" : "s");
        text(s_outline_canvas, detail, right_x + 14, status_y - 22,
             C_TEXT_FAINT, ui9);
    }
}

static void draw_designer_tree(AromaNode *canvas, int x, int *y, int w, int depth)
{
    int indent = depth * 14;
    for (int i = 0; i < s_design_doc.node_count; i++)
    {
        if (s_design_doc.nodes[i].depth != depth)
            continue;
        bool is_root = i == s_design_doc.root;
        bool is_selected = i == s_design_doc.selected;
        const char *tag = s_design_doc.nodes[i].tag;
        const char *text_val = ide_design_attr(&s_design_doc, i, "text");
        char label[96];
        if (text_val && text_val[0])
            snprintf(label, sizeof(label), "%s · %s", tag, text_val);
        else
            snprintf(label, sizeof(label), "%s", tag);
        if (is_selected)
            fill(canvas, x - 4, *y - 2, w - 4, 18, 8, C_SELECT);
        text_fit(canvas, label, x + indent, *y, w - indent - 10,
                 is_selected ? C_TEXT : C_TEXT_SOFT, font_ui(10));
        if (is_root)
            text(canvas, "[root]", x + indent + text_width(font_ui(10), label) + 4,
                 *y, C_TEXT_FAINT, font_ui(9));
        *y += 18;
        if (*y > status_y - 30)
            return;
        draw_designer_tree(canvas, x, y, w, depth + 1);
    }
}

static void draw_problem_content(void)
{
    AromaNode *canvas = s_panel_draw_canvas;
    if (!canvas)
        return;
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    build_problem_list();
    int shown = s_problem_total;
    if (shown > PANEL_VISIBLE_ROWS)
        shown = PANEL_VISIBLE_ROWS;

    int px = s_panel_draw_px;
    if (shown == 0)
    {
        icon_center(canvas, AROMA_ICON_CHECK_CIRCLE, px + 24,
                    s_panel_draw_py + PANEL_HEADER_H + 38, 16, C_GREEN);
        text(canvas, "No problems detected in the workspace", px + 38,
             s_panel_draw_py + PANEL_HEADER_H + 28, C_TEXT_DIM, ui11);
        return;
    }

    int y = s_panel_draw_py + PANEL_HEADER_H + 8;
    for (int i = 0; i < shown; i++)
    {
        int file_index = s_problem_file[i];
        int diag_index = s_problem_diag[i];
        AromaCodeDiagnostic *diag = &ide_files[file_index].diagnostics[diag_index];
        if (s_panel_problem_hover == i)
            fill(canvas, px + 8, y - 2, s_panel_draw_w - 24, 20, 3,
                 C_HOVER_SOFT);

        const char *glyph = AROMA_ICON_INFO_OUTLINE;
        uint32_t color = C_CYAN;
        if (diag->severity == AROMA_CODE_DIAGNOSTIC_ERROR)
        {
            glyph = AROMA_ICON_ERROR;
            color = C_RED;
        }
        else if (diag->severity == AROMA_CODE_DIAGNOSTIC_WARNING)
        {
            glyph = AROMA_ICON_WARNING;
            color = C_YELLOW;
        }
        icon_center(canvas, glyph, px + 20, y + 8, 14, color);

        char location[64];
        snprintf(location, sizeof(location), "%s:%d", base_name(ide_files[file_index].path),
                 diag->line);
        int location_width = text_width(ui10, location);
        text(canvas, location, s_panel_draw_right_x - 26 - location_width, y + 1,
             C_TEXT_FAINT, ui10);
        text_fit(canvas, diag->message, px + 36, y,
                 s_panel_draw_right_x - px - 50 - location_width, C_TEXT_SOFT,
                 ui11);
        y += 21;
    }
}

static void draw_output_content(void)
{
    project_draw_output_panel();
}

static void draw_trace_content(void)
{
    AromaNode *canvas = s_panel_draw_canvas;
    if (!canvas)
        return;
    AromaFont *mono = font_mono(10);
    AromaFont *ui10 = font_ui(10);
    int count = s_log_count;
    int first = count > LOG_VISIBLE ? count - LOG_VISIBLE : 0;
    int px = s_panel_draw_px;
    int y = s_panel_draw_py + PANEL_HEADER_H + 8;
    for (int i = first; i < count; i++)
    {
        LogEntry *entry = &s_logs[i];
        text(canvas, entry->time_text, px + 12, y, C_TEXT_FAINT, mono);
        text_fit(canvas, entry->method, px + 72, y, 176, entry->color, ui10);
        text_fit(canvas, entry->message, px + 254, y,
                 s_panel_draw_right_x - px - 274, C_TEXT_DIM, ui10);
        y += 17;
    }
    if (count == 0)
        text(canvas, "Waiting for language server messages...", px + 12,
             s_panel_draw_py + PANEL_HEADER_H + 20, C_TEXT_DIM, ui10);
}

static void draw_panel(void)
{
    AromaNode *canvas = panel_get_canvas();
    if (!canvas)
        return;
    aroma_canvas_reset(canvas);
    if (!s_panel_visible)
        return;

                                                                           
                                                                            
    int pw = s_panel_detached ? s_panel_detached_w : panel_w;
    int ph = s_panel_detached ? s_panel_detached_h : panel_h;
    if (pw <= 0)
        pw = panel_w;
    if (ph <= 0)
        ph = panel_h;
    int px = s_panel_detached ? 0 : SIDEBAR_X;
    int py = s_panel_detached ? 0 : panel_y;
    int right_x = px + pw;

    s_panel_draw_canvas = canvas;
    s_panel_draw_px = px;
    s_panel_draw_py = py;
    s_panel_draw_right_x = right_x;
    s_panel_draw_w = pw;
    s_panel_draw_h = ph;

    fill(canvas, px, py, pw, ph, 0, C_PANEL);
    fill(canvas, px, py, pw, PANEL_HEADER_H, 0, M3_SURFACE_LOW);

    AromaFont *ui11 = font_ui(11);
    int problems = 0;
    count_diagnostics(NULL, NULL, &problems);

    int tab_bar_y = py + 4;
    int tab_text_y = py + 8;

    for (int i = 0; i < 3; i++)
    {
        char label[48];
        if (i == 0)
            snprintf(label, sizeof(label), "%s  %d", k_panel_tabs[i], problems);
        else
            snprintf(label, sizeof(label), "%s", k_panel_tabs[i]);
        int tab_x = px + 8 + i * 110;
        bool active = s_panel_tab == i;
        int width = text_width(ui11, k_panel_tabs[i]);
        if (s_panel_tab_hover == i && !active)
            fill(canvas, tab_x - 5, tab_bar_y, width + 10, 20, 3,
                 C_HOVER_SOFT);
        text(canvas, label, tab_x, tab_text_y,
             active ? C_TEXT : C_TEXT_DIM, ui11);
        if (active)
            fill(canvas, tab_x, py + PANEL_HEADER_H - 3,
                 width, 2, 0, C_PRIMARY);
    }

    if (s_panel_icon_hover == 1)
        fill(canvas, right_x - 56, tab_bar_y, 20, 20, 4, C_HOVER);
    if (s_panel_icon_hover == 2)
        fill(canvas, right_x - 30, tab_bar_y, 20, 20, 4, C_HOVER);
    icon_center(canvas, AROMA_ICON_REFRESH, right_x - 46, tab_text_y + 6,
                14, C_TEXT_DIM);
    if (s_panel_detached)
        icon_center(canvas, AROMA_ICON_OPEN_IN_NEW, right_x - 20, tab_text_y + 6,
                    12, C_TEXT_DIM);
    else
        icon_center(canvas, AROMA_ICON_OPEN_WITH, right_x - 20, tab_text_y + 6,
                    12, C_TEXT_DIM);

    line(canvas, px, py + PANEL_HEADER_H - 1, right_x,
         py + PANEL_HEADER_H - 1, C_BORDER, 1);

    switch (s_panel_tab)
    {
    case 0:
        draw_problem_content();
        break;
    case 1:
        draw_output_content();
        break;
    case 2:
        draw_trace_content();
        break;
    default:
        break;
    }

                                              
    if (!s_panel_detached && !s_menu_hover)
        fill(canvas, SIDEBAR_X, panel_y - 4, panel_w, 4, 0,
             s_resizing_panel ? C_PRIMARY : C_BORDER);

    if (s_panel_detached)
    {
        aroma_node_invalidate(s_panel_detached_canvas);
        aroma_ui_request_redraw(s_panel_detached_window);
    }
}

static AromaNode *panel_get_canvas(void)
{
    if (s_panel_detached && s_panel_detached_canvas)
        return s_panel_detached_canvas;
    return s_panel_canvas;
}

static AromaNode *window_root_node(AromaWindow *window)
{
    if (!window)
        return NULL;
    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;
    for (int i = 0; i < g_window_count; i++)
    {
        if (g_windows[i].window == window)
            return g_windows[i].root_node;
    }
    return NULL;
}

                                                                       
                                                                           
                                                                      
                                                     
static void ide_refit_after_dock_change(void)
{
    ide_layout_recalc();
    ide_sync_layout_nodes();
    if (s_editor)
    {
        int ed_w = editor_w;
        aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y, ed_w,
                                     editor_h);
    }
    markdown_preview_fit();
    if (s_editor_bg)
        aroma_node_invalidate(s_editor_bg);
    draw_all();
}

static void panel_restore_attached(bool destroy_window, bool announce)
{
                                                                              
                                 
    s_panel_detached = false;
    s_panel_detached_canvas = NULL;
    s_panel_detached_w = 0;
    s_panel_detached_h = 0;

    if (destroy_window && s_panel_detached_window)
        aroma_ui_destroy_window(s_panel_detached_window);
    s_panel_detached_window = NULL;

    if (s_panel_canvas)
        aroma_node_set_hidden(s_panel_canvas, !s_panel_visible);
    if (s_panel_bg)
        aroma_node_set_hidden(s_panel_bg, !s_panel_visible);
    if (s_div_panel_top)
        aroma_node_set_hidden(s_div_panel_top, !s_panel_visible);

    s_panel_draw_canvas = s_panel_canvas;
    ide_refit_after_dock_change();
    if (s_panel_visible && s_panel_canvas)
        aroma_ui_set_focused_node(s_panel_canvas);
    if (announce)
        toast("Panel docked");
}

static void panel_detach(void)
{
    if (s_panel_detached)
        return;

    if (!s_panel_visible)
        s_panel_visible = true;

                                                                           
                                          
    int detach_w = panel_w < 520 ? 520 : panel_w;
    int detach_h = panel_h < 260 ? 260 : panel_h;

                                                    
    if (s_panel_canvas)
        aroma_node_set_hidden(s_panel_canvas, true);
    if (s_panel_bg)
        aroma_node_set_hidden(s_panel_bg, true);
    if (s_div_panel_top)
        aroma_node_set_hidden(s_div_panel_top, true);

    s_panel_detached_window = aroma_ui_create_window("Panel", detach_w, detach_h);
    AromaNode *root = window_root_node(s_panel_detached_window);
    if (!root)
    {
        if (s_panel_detached_window)
            aroma_ui_destroy_window(s_panel_detached_window);
        s_panel_detached_window = NULL;
        if (s_panel_canvas)
            aroma_node_set_hidden(s_panel_canvas, !s_panel_visible);
        if (s_panel_bg)
            aroma_node_set_hidden(s_panel_bg, !s_panel_visible);
        if (s_div_panel_top)
            aroma_node_set_hidden(s_div_panel_top, !s_panel_visible);
        toast("Could not detach the panel");
        return;
    }

    s_panel_detached_canvas = aroma_canvas_create(root, 0, 0, detach_w,
                                                  detach_h);
    if (!s_panel_detached_canvas)
    {
        aroma_ui_destroy_window(s_panel_detached_window);
        s_panel_detached_window = NULL;
        if (s_panel_canvas)
            aroma_node_set_hidden(s_panel_canvas, false);
        if (s_panel_bg)
            aroma_node_set_hidden(s_panel_bg, false);
        if (s_div_panel_top)
            aroma_node_set_hidden(s_div_panel_top, !s_panel_visible);
        toast("Could not detach the panel");
        return;
    }

                                                                             
    aroma_node_set_layout_fill(s_panel_detached_canvas);
    subscribe_node(s_panel_detached_canvas, EVENT_TYPE_KEY_PRESS, panel_on_key);
    subscribe_node(s_panel_detached_canvas, EVENT_TYPE_MOUSE_MOVE, panel_on_move);
    subscribe_node(s_panel_detached_canvas, EVENT_TYPE_MOUSE_CLICK, panel_on_click);
    subscribe_node(s_panel_detached_canvas, EVENT_TYPE_MOUSE_EXIT, panel_on_exit);
    subscribe_node(root, EVENT_TYPE_KEY_PRESS, panel_on_key);
    subscribe_node(root, EVENT_TYPE_WINDOW_RESIZE, panel_detached_on_resize);
    subscribe_node(root, EVENT_TYPE_FOCUS_GAINED, panel_detached_on_focus_gained);

    s_panel_detached = true;
    s_panel_detached_w = detach_w;
    s_panel_detached_h = detach_h;
    s_panel_draw_canvas = s_panel_detached_canvas;

    ide_refit_after_dock_change();
    aroma_ui_set_focused_node(s_panel_detached_canvas);
    aroma_node_invalidate(s_panel_detached_canvas);
    aroma_ui_request_redraw(s_panel_detached_window);
    aroma_ui_request_frame();
    toast("Panel detached - use the dock icon to reattach");
}

                                                                           
                                                                             
                                                         
static bool panel_detached_on_resize(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_panel_detached || !s_panel_detached_canvas)
        return false;
    panel_sync_detached_size();
    aroma_ui_set_focused_node(s_panel_detached_canvas);
    return true;
}

                                                                        
                                          
static bool panel_detached_on_focus_gained(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (!s_panel_detached || !s_panel_detached_canvas)
        return false;
    aroma_ui_set_focused_node(s_panel_detached_canvas);
    return true;
}

static void panel_attach(void)
{
    if (!s_panel_detached)
        return;
    panel_restore_attached(true, true);
}

static void panel_toggle_detach(void)
{
    if (s_panel_detached)
        panel_attach();
    else
        panel_detach();
}

                                                                           
                                                                          
                                           
static void panel_sync_detached_size(void)
{
    if (!s_panel_detached || !s_panel_detached_canvas)
        return;
    AromaRect *r = aroma_node_get_rect(s_panel_detached_canvas);
    if (!r || r->width <= 0 || r->height <= 0)
        return;
    if (r->width == s_panel_detached_w && r->height == s_panel_detached_h)
        return;
    s_panel_detached_w = r->width;
    s_panel_detached_h = r->height;
    if (s_panel_visible)
        draw_panel();
    if (s_panel_detached_window)
        aroma_ui_request_redraw(s_panel_detached_window);
}

                                                                         
                                                                               
static bool sidebar_detached_on_resize(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_sidebar_detached || !s_sidebar_detached_canvas)
        return false;
    AromaRect *r = aroma_node_get_rect(s_sidebar_detached_canvas);
    if (r && r->width > 0 && r->height > 0)
    {
        s_sidebar_w = r->width;
        ide_layout_recalc();
        ide_sync_layout_nodes();
    }
    aroma_ui_set_focused_node(s_sidebar_detached_canvas);
    draw_sidebar();
    return true;
}

static bool sidebar_detached_on_focus_gained(AromaEvent *event,
                                             void *user_data)
{
    (void)event;
    (void)user_data;
    if (!s_sidebar_detached || !s_sidebar_detached_canvas)
        return false;
    aroma_ui_set_focused_node(s_sidebar_detached_canvas);
    return true;
}

static AromaNode *sidebar_get_canvas(void)
{
    if (s_sidebar_detached && s_sidebar_detached_canvas)
        return s_sidebar_detached_canvas;
    return s_sidebar_canvas;
}

static void sidebar_restore_attached(bool destroy_window, bool announce)
{
    s_sidebar_detached = false;

                                                                       
                                                     
    if (s_sidebar_canvas_saved)
    {
        s_sidebar_canvas = s_sidebar_canvas_saved;
        s_sidebar_canvas_saved = NULL;
    }

    if (destroy_window && s_sidebar_detached_window)
        aroma_ui_destroy_window(s_sidebar_detached_window);
    s_sidebar_detached_window = NULL;
    s_sidebar_detached_canvas = NULL;

    if (s_sidebar_canvas)
        aroma_node_set_hidden(s_sidebar_canvas, false);
    if (s_card_sidebar_bg)
        aroma_node_set_hidden(s_card_sidebar_bg, false);
    if (s_div_rail_right)
        aroma_node_set_hidden(s_div_rail_right, false);

    ide_refit_after_dock_change();
    if (announce)
        toast("Sidebar docked");
}

static void sidebar_detach(void)
{
    if (s_sidebar_detached)
        return;

    s_sidebar_canvas_saved = s_sidebar_canvas;
    if (!s_sidebar_canvas_saved)
        return;

    if (s_sidebar_canvas_saved)
        aroma_node_set_hidden(s_sidebar_canvas_saved, true);
    if (s_card_sidebar_bg)
        aroma_node_set_hidden(s_card_sidebar_bg, true);
    if (s_div_rail_right)
        aroma_node_set_hidden(s_div_rail_right, true);

    int detach_h = win_h - STATUS_H;
    if (detach_h < 420)
        detach_h = 420;

    s_sidebar_detached_window = aroma_ui_create_window("Sidebar", s_sidebar_w,
                                                       detach_h);
    AromaNode *root = window_root_node(s_sidebar_detached_window);
    if (!root)
    {
        if (s_sidebar_detached_window)
            aroma_ui_destroy_window(s_sidebar_detached_window);
        s_sidebar_detached_window = NULL;
        s_sidebar_canvas = s_sidebar_canvas_saved;
        s_sidebar_canvas_saved = NULL;
        if (s_sidebar_canvas)
            aroma_node_set_hidden(s_sidebar_canvas, false);
        if (s_card_sidebar_bg)
            aroma_node_set_hidden(s_card_sidebar_bg, false);
        if (s_div_rail_right)
            aroma_node_set_hidden(s_div_rail_right, false);
        toast("Could not detach the sidebar");
        return;
    }

    s_sidebar_detached_canvas = aroma_canvas_create(root, 0, 0, s_sidebar_w,
                                                    detach_h);
    if (!s_sidebar_detached_canvas)
    {
        aroma_ui_destroy_window(s_sidebar_detached_window);
        s_sidebar_detached_window = NULL;
        s_sidebar_canvas = s_sidebar_canvas_saved;
        s_sidebar_canvas_saved = NULL;
        if (s_sidebar_canvas)
            aroma_node_set_hidden(s_sidebar_canvas, false);
        if (s_card_sidebar_bg)
            aroma_node_set_hidden(s_card_sidebar_bg, false);
        if (s_div_rail_right)
            aroma_node_set_hidden(s_div_rail_right, false);
        toast("Could not detach the sidebar");
        return;
    }

    aroma_node_set_layout_fill(s_sidebar_detached_canvas);
    subscribe_node(s_sidebar_detached_canvas, EVENT_TYPE_MOUSE_MOVE,
                   sidebar_on_move);
    subscribe_node(s_sidebar_detached_canvas, EVENT_TYPE_MOUSE_CLICK,
                   sidebar_on_click);
    subscribe_node(s_sidebar_detached_canvas, EVENT_TYPE_MOUSE_EXIT,
                   sidebar_on_exit);
    subscribe_node(s_sidebar_detached_canvas, EVENT_TYPE_MOUSE_SCROLL,
                   sidebar_on_scroll);
    subscribe_node(root, EVENT_TYPE_WINDOW_RESIZE, sidebar_detached_on_resize);
    subscribe_node(root, EVENT_TYPE_FOCUS_GAINED, sidebar_detached_on_focus_gained);

    s_sidebar_canvas = s_sidebar_detached_canvas;
    s_sidebar_detached = true;

    ide_refit_after_dock_change();
    aroma_ui_set_focused_node(s_sidebar_detached_canvas);
    aroma_node_invalidate(s_sidebar_detached_canvas);
    aroma_ui_request_redraw(s_sidebar_detached_window);
    aroma_ui_request_frame();
    toast("Sidebar detached - use the dock icon to reattach");
}

static void sidebar_attach(void)
{
    if (!s_sidebar_detached)
        return;
    sidebar_restore_attached(true, true);
}

static void sidebar_toggle_detach(void)
{
    if (s_sidebar_detached)
        sidebar_attach();
    else
        sidebar_detach();
}

/* Kept only so markup that references the "new_note" callback still loads. */
static void preview_new_note(void *user_data)
{
    (void)user_data;
}

static void markdown_preview_destroy(void)
{
    if (s_markdown_webview)
    {
        __destroy_node(s_markdown_webview);
        s_markdown_webview = NULL;
    }
    s_markdown_visible = false;
    s_md_x = s_md_y = s_md_w = s_md_h = -1;
    if (s_editor && !s_designer_visible)
    {
        aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y,
                                     editor_w, editor_h);
        aroma_node_set_hidden(s_editor, false);
    }
}

static void ide_window_closed(AromaWindow *window, void *user_data)
{
    (void)user_data;

    if (window == s_changelog_window)
    {
        s_changelog_window = NULL;
        s_changelog_visible = false;
        s_home_dirty = true;
        ide_home_draw();
        aroma_node_invalidate(s_home_canvas);
    }
    if (window == s_settings_window)
    {
        s_settings_window = NULL;
        s_settings_visible = false;
        s_settings_close_requested = false;
        s_settings_window_opening = false;
        s_settings_window_closing = false;
        s_settings_window_anim_opacity = 0.0f;
        s_settings_window_canvas = NULL;
        s_settings_window_root = NULL;
        s_settings_sidebar = NULL;
        s_settings_title_label = NULL;
        s_settings_desc_label = NULL;
        s_settings_experimental_badge = NULL;
        s_settings_close_button = NULL;
        s_settings_label_count = 0;
        s_settings_dropdown_count = 0;
        draw_rail();
        draw_title();
        draw_all();
    }
    if (window == s_panel_detached_window)
    {
        panel_restore_attached(false, true);
    }
    if (window == s_sidebar_detached_window)
    {
        sidebar_restore_attached(false, true);
    }
}

static void draw_status(void)
{
    if (!s_status_canvas)
        return;
    aroma_canvas_reset(s_status_canvas);
    fill(s_status_canvas, 0, status_y, win_w, STATUS_H, 0, M3_SURFACE_CONTAINER);
    AromaFont *ui11 = font_ui(11);
    AromaFont *mono = font_mono(11);
    int center_y = status_y + STATUS_H / 2;
    int text_y = status_y + 9;


    text(s_status_canvas, "0", 84, text_y, C_STATUS_SOFT, mono);
    icon_center(s_status_canvas, AROMA_ICON_KEYBOARD_ARROW_UP, 96, center_y, 11,
                C_STATUS_SOFT);
    text(s_status_canvas, "1", 106, text_y, C_STATUS_SOFT, mono);

    int errors = 0;
    int warnings = 0;
    count_diagnostics(&errors, &warnings, NULL);
    icon_center(s_status_canvas, AROMA_ICON_ERROR, 134, center_y, 12, C_STATUS_TEXT);
    char error_text[16];
    snprintf(error_text, sizeof(error_text), "%d", errors);
    text(s_status_canvas, error_text, 146, text_y, C_STATUS_TEXT, mono);
    icon_center(s_status_canvas, AROMA_ICON_WARNING, 170, center_y, 12, C_STATUS_TEXT);
    char warning_text[16];
    snprintf(warning_text, sizeof(warning_text), "%d", warnings);
    text(s_status_canvas, warning_text, 182, text_y, C_STATUS_TEXT, mono);

    icon_center(s_status_canvas, AROMA_ICON_CHECK_CIRCLE, 214, center_y, 12,
                C_STATUS_TEXT);
    text(s_status_canvas, s_lsp_connected ? "LSP: Connected" : "LSP: Starting",
         226, text_y, C_STATUS_TEXT, ui11);

    char cursor_text[48];
    snprintf(cursor_text, sizeof(cursor_text), "Ln %d, Col %d", s_line, s_column);
    char selection_text[32];
    selection_text[0] = '\0';
    if (s_selection_chars > 0)
        snprintf(selection_text, sizeof(selection_text), "Sel %d", s_selection_chars);

    int right = win_w - 96;
    int width = text_width(ui11, "LF");
    right -= width;
    text(s_status_canvas, "LF", right, text_y, C_STATUS_SOFT, ui11);
    right -= 16;

    width = text_width(ui11, "UTF-8");
    right -= width;
    text(s_status_canvas, "UTF-8", right, text_y, C_STATUS_SOFT, ui11);
    right -= 16;

    char tab_text[32];
    snprintf(tab_text, sizeof(tab_text), "Spaces: %d", s_setting_tab_width);
    width = text_width(ui11, tab_text);
    right -= width;
    text(s_status_canvas, tab_text, right, text_y, C_STATUS_SOFT, ui11);
    right -= 16;

    if (selection_text[0])
    {
        width = text_width(mono, selection_text);
        right -= width;
        text(s_status_canvas, selection_text, right, text_y, C_STATUS_SOFT, mono);
        right -= 16;
    }

    width = text_width(mono, cursor_text);
    right -= width;
    text(s_status_canvas, cursor_text, right, text_y, C_STATUS_TEXT, mono);

    /* Anchor the language label to the left edge of the trailing cluster so a
     * long file-type name can never run into the tab/encoding fields. */
    const char *language = s_active_file >= 0
                               ? language_label(ide_files[s_active_file].language)
                               : "AromaUI";
    int language_width = text_width(ui11, language);
    int language_x = right - 16 - language_width;
    int center_right = right_x;
    if (language_x >= 340 + 40)
    {
        text(s_status_canvas, language, language_x, text_y, C_STATUS_SOFT, ui11);
        if (language_x - 12 < center_right)
            center_right = language_x - 12;
    }
    else
    {
        /* Too tight for the label; drop it instead of drawing over the
         * cursor position or the centered project summary. */
        center_right = right - 12;
    }
    if (center_right < 340)
        center_right = 340;

    char status_center[256];
    if (s_project.target == 1)
        snprintf(status_center, sizeof(status_center),
                 "AromaUI Studio  ·  %s  ·  Android: %s  ·  %d files",
                 ide_workspace_project_name(),
                 s_project.android_serial[0] ? s_project.android_serial
                                             : "no device",
                 ide_file_count);
    else
        snprintf(status_center, sizeof(status_center),
                 "AromaUI Studio  ·  %s  ·  Linux  ·  %d files",
                 ide_workspace_project_name(), ide_file_count);
    if (center_right > 340)
        text_fit(s_status_canvas, status_center, 340, text_y,
                 center_right - 340, C_STATUS_SOFT, ui11);
    if (s_status_hover == 1)
        fill(s_status_canvas, win_w - 63, status_y + 5, 26, 22, 4,
             M3_PRIMARY_CONTAINER);
    icon_center(s_status_canvas, AROMA_ICON_DASHBOARD, win_w - 50, center_y, 14,
                C_STATUS_TEXT);
    icon_center(s_status_canvas, AROMA_ICON_ADD_ALERT, win_w - 22, center_y, 13,
                C_STATUS_TEXT);
}

static int menu_item_y(int index)
{
    int y = MENU_panel_y + 6;
    for (int i = 0; i < index; i++)
    {
        y += MENU_ITEM_H;
        if (i == 3)
            y += 8;
    }
    return y;
}

static int menu_panel_x(void)
{
    int x = title_menu_x(s_menu_index) - 4;
    if (x + MENU_panel_w > win_w - 10)
        x = win_w - MENU_panel_w - 10;
    if (x < 8)
        x = 8;
    return x;
}

static int s_dialog_x;
static int s_dialog_y;
static int s_dialog_w;
static int s_dialog_h;

static int s_wiz_x;
static int s_wiz_y;
static int s_wiz_w;
static int s_wiz_h;

#define WIZ_STEPPER_Y (s_wiz_y + 72)
#define WIZ_CONTENT_Y (s_wiz_y + 152)
#define WIZ_FOOTER_Y (s_wiz_y + s_wiz_h - 58)

                                                                        
                                                                        
                                                                        
#define PROPS_ROW_H 22
#define PROPS_HEADER_H 74
#define PROPS_FOOTER_H 44
#define PROPS_VALUE_X 220

static void dialog_reset_geometry(void)
{
    s_dialog_w = 592;
    s_dialog_h = 194;
    s_dialog_x = (win_w - s_dialog_w) / 2;
    s_dialog_y = (win_h - s_dialog_h) / 2;
}

static int props_count(void)
{
    int sel = s_design_doc.selected;
    if (sel < 0 || sel >= s_design_doc.node_count)
        return 0;
    const WidgetPropMap *wmap = widget_prop_find(s_design_doc.nodes[sel].tag);
    int n = 0;
    for (int i = 0; i < wmap->count; i++)
        if (prop_schema_find_index(wmap->keys[i]) >= 0)
            n++;
    return n;
}

                                                         
static int props_schema_at(int row)
{
    int sel = s_design_doc.selected;
    if (row < 0 || sel < 0 || sel >= s_design_doc.node_count)
        return -1;
    const WidgetPropMap *wmap = widget_prop_find(s_design_doc.nodes[sel].tag);
    int n = 0;
    for (int i = 0; i < wmap->count; i++)
    {
        int idx = prop_schema_find_index(wmap->keys[i]);
        if (idx < 0)
            continue;
        if (n == row)
            return idx;
        n++;
    }
    return -1;
}

static void props_update_geometry(void)
{
    int rows = props_count();
    (void)rows;
    s_dialog_w = win_w < 380 ? win_w : 360;
    s_dialog_h = win_h - TITLE_H - STATUS_H;
    if (s_dialog_h < 194)
        s_dialog_h = 194;
    s_dialog_x = win_w - s_dialog_w;
    s_dialog_y = TITLE_H;
}

static int props_visible_rows(void)
{
    int v = (s_dialog_h - PROPS_HEADER_H - PROPS_FOOTER_H) / PROPS_ROW_H;
    return v < 1 ? 1 : v;
}

static void props_clamp_scroll(void)
{
    int count = props_count();
    int vis = props_visible_rows();
    if (s_dialog_prop_sel >= count)
        s_dialog_prop_sel = count - 1;
    if (s_dialog_prop_sel < 0)
        s_dialog_prop_sel = 0;
    if (s_dialog_prop_sel < s_dialog_prop_scroll)
        s_dialog_prop_scroll = s_dialog_prop_sel;
    if (s_dialog_prop_sel >= s_dialog_prop_scroll + vis)
        s_dialog_prop_scroll = s_dialog_prop_sel - vis + 1;
    if (s_dialog_prop_scroll > count - vis)
        s_dialog_prop_scroll = count - vis;
    if (s_dialog_prop_scroll < 0)
        s_dialog_prop_scroll = 0;
}

static const char *const *props_options(int schema_idx, int *count)
{
    *count = 0;
    int sel = s_design_doc.selected;
    if (schema_idx < 0 || sel < 0 || sel >= s_design_doc.node_count)
        return NULL;
    return prop_dropdown_options(s_design_doc.nodes[sel].tag,
                                 k_prop_schema_all[schema_idx].key, count);
}

                                                                           
static bool props_apply(int schema_idx, const char *value)
{
    int index = s_design_doc.selected;
    if (schema_idx < 0 || index < 0 || index >= s_design_doc.node_count)
        return false;
    const PropSchemaEntry *schema = &k_prop_schema_all[schema_idx];
    if (!value)
        value = "";

    bool has_unit = false;
    if (schema->is_numeric)
    {
        if (!value[0])
        {
            snprintf(s_dialog_error, sizeof(s_dialog_error),
                     "%s needs a number", schema->label);
            return false;
        }
        char *end = NULL;
        strtod(value, &end);
        if (end == value || (*end && strcmp(end, "%") != 0 &&
                             strcmp(end, "px") != 0))
        {
            snprintf(s_dialog_error, sizeof(s_dialog_error),
                     "%s must be a number", schema->label);
            return false;
        }
        has_unit = *end != '\0';
    }

    const char *current = ide_design_attr(&s_design_doc, index, schema->key);
    if (current && strcmp(current, value) == 0)
    {
        s_dialog_error[0] = '\0';
        return true;                                              
    }

    bool quoted = !schema->is_numeric || has_unit;
    if (!ide_design_set_attr(&s_design_doc, index, schema->key, value, quoted))
    {
        snprintf(s_dialog_error, sizeof(s_dialog_error),
                 "Could not set %s (too many properties?)", schema->key);
        return false;
    }
    ide_design_layout(&s_design_doc);
    s_dialog_error[0] = '\0';
    designer_after_mutation();
    return true;
}

static void props_cycle(int schema_idx, int dir)
{
    int count = 0;
    const char *const *opts = props_options(schema_idx, &count);
    if (!opts || count <= 0)
        return;
    const char *cur = ide_design_attr(&s_design_doc, s_design_doc.selected,
                                      k_prop_schema_all[schema_idx].key);
    int at = -1;
    for (int i = 0; cur && i < count; i++)
        if (strcmp(cur, opts[i]) == 0)
            at = i;
    int next = at < 0 ? (dir > 0 ? 0 : count - 1)
                      : (at + dir + count) % count;
    props_apply(schema_idx, opts[next]);
}

static void props_begin_edit(int row)
{
    int schema_idx = props_schema_at(row);
    if (schema_idx < 0)
        return;
    int count = 0;
    if (props_options(schema_idx, &count) && count > 0)
    {
        props_cycle(schema_idx, 1);                                        
        return;
    }
    const char *cur = ide_design_attr(&s_design_doc, s_design_doc.selected,
                                      k_prop_schema_all[schema_idx].key);
    snprintf(s_dialog_prop_value, sizeof(s_dialog_prop_value), "%s",
             cur ? cur : "");
    s_dialog_prop_edit_row = row;
    s_dialog_error[0] = '\0';
}

static bool props_on_key(uint32_t key)
{
    bool enter = key == 13 || key == 10 || key == 0xFF0D || key == 0xFF8D;
    bool esc = key == 27 || key == 0xFF1B;
    bool bs = key == 8 || key == 0xFF08 || key == 0x7F;
    int sel = s_design_doc.selected;

                                                                            
                                                                         
    if (esc)
    {
        close_dialog();
        return true;
    }

    if (!s_designer_visible || sel < 0 || sel >= s_design_doc.node_count)
    {
        if (enter)
            close_dialog();
        return true;
    }

    if (s_dialog_prop_edit_row >= 0)
    {
        if (enter)
        {
            properties_dialog_submit();
            return true;
        }
        else if (bs)
        {
            size_t len = strlen(s_dialog_prop_value);
            if (len > 0)
                s_dialog_prop_value[len - 1] = '\0';
            s_dialog_error[0] = '\0';
        }
        else if (key >= 32 && key <= 126)
        {
            size_t len = strlen(s_dialog_prop_value);
            if (len + 1 < sizeof(s_dialog_prop_value) &&
                len + 1 < IDE_DESIGN_VALUE_MAX)
            {
                s_dialog_prop_value[len] = (char)key;
                s_dialog_prop_value[len + 1] = '\0';
            }
            s_dialog_error[0] = '\0';
        }
        draw_dialog();
        return true;
    }

    int count = props_count();
    int vis = props_visible_rows();
    int schema_idx = props_schema_at(s_dialog_prop_sel);
    if (key == 0xFF52)
        s_dialog_prop_sel--;
    else if (key == 0xFF54 || key == 9 || key == 0xFF09)
        s_dialog_prop_sel++;
    else if (key == 0xFF55)
        s_dialog_prop_sel -= vis;
    else if (key == 0xFF56)
        s_dialog_prop_sel += vis;
    else if (key == 0xFF50)
        s_dialog_prop_sel = 0;
    else if (key == 0xFF57)
        s_dialog_prop_sel = count - 1;
    else if (key == 0xFF51 || key == 0xFF53)
        props_cycle(schema_idx, key == 0xFF53 ? 1 : -1);
    else if (enter)
        props_begin_edit(s_dialog_prop_sel);
    props_clamp_scroll();
    draw_dialog();
    return true;
}

static bool props_hit_test(AromaNode *node, int x, int y, void *user_data)
{
    (void)node;
    (void)user_data;
    /* Only the docked panel belongs to the properties overlay. Clicks
       elsewhere must reach the editor or designer underneath it. */
    props_update_geometry();
    return x >= s_dialog_x && x < s_dialog_x + s_dialog_w &&
           y >= s_dialog_y && y < s_dialog_y + s_dialog_h;
}

static bool props_on_click(int x, int y)
{
    /* ide_layout_recalc() restores the centered modal rect each frame, so
       refresh the docked properties panel bounds before hit-testing. */
    props_update_geometry();
    if (x < s_dialog_x || x >= s_dialog_x + s_dialog_w || y < s_dialog_y ||
        y >= s_dialog_y + s_dialog_h)
    {
        return false;
    }
    if (y < s_dialog_y + PROPS_HEADER_H &&
        x >= s_dialog_x + s_dialog_w - 42)
    {
        close_dialog();
        return true;
    }
    aroma_ui_set_focused_node(s_dialog_canvas);
    int y0 = s_dialog_y + PROPS_HEADER_H;
    int vis = props_visible_rows();
    if (y < y0 || y >= y0 + vis * PROPS_ROW_H)
        return true;
    int row = s_dialog_prop_scroll + (y - y0) / PROPS_ROW_H;
    if (row >= props_count())
        return true;

    if (s_dialog_prop_edit_row >= 0 && s_dialog_prop_edit_row != row)
    {
        properties_dialog_submit();
        if (s_dialog_prop_edit_row >= 0)
            return true;                                  
    }
    if (s_dialog_prop_edit_row == row)
        return true;
    s_dialog_prop_sel = row;
    props_begin_edit(row);
    props_clamp_scroll();
    draw_dialog();
    return true;
}

static bool props_on_scroll(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_dialog_open || !event || event->event_type != EVENT_TYPE_MOUSE_SCROLL)
        return false;
    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
        props_update_geometry();
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (x < s_dialog_x || x >= s_dialog_x + s_dialog_w ||
        y < s_dialog_y || y >= s_dialog_y + s_dialog_h)
        return false;

    float steps = event->data.mouse.scroll_y;
    if (steps > -1.0f && steps < 1.0f)
        steps = steps < 0.0f ? -1.0f : (steps > 0.0f ? 1.0f : 0.0f);

    if (s_dialog_mode == IDE_DIALOG_ANDROID_DEVICES)
    {
        s_android_device_scroll -= (int)steps * 2;
        android_dev_clamp_scroll();
        draw_dialog();
        return true;
    }
    if (s_dialog_mode != IDE_DIALOG_PROPERTIES)
        return false;

    s_dialog_prop_scroll -= (int)steps * 3;
    int count = props_count();
    int max_scroll = count - props_visible_rows();
    if (s_dialog_prop_scroll < 0)
        s_dialog_prop_scroll = 0;
    if (s_dialog_prop_scroll > max_scroll)
        s_dialog_prop_scroll = max_scroll;
    props_clamp_scroll();
    draw_dialog();
    return true;
}

static void close_dialog(void)
{
    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
    {
        destroy_prop_widgets();
        s_prop_last_selected = -1;
        s_prop_last_panel_y = 0;
        s_prop_edit_active = false;
        s_prop_dropdown_open = false;
    }
    s_dialog_open = false;
    s_dialog_error[0] = '\0';
    wizard_close_widgets();
    if (s_dialog_canvas)
    {
        aroma_node_set_hit_test_cb(s_dialog_canvas, NULL, NULL);
        aroma_node_set_hidden(s_dialog_canvas, true);
    }
    draw_dialog();
    if (s_designer_visible && s_designer_overlay)
        aroma_ui_set_focused_node(s_designer_overlay);
    else if (s_designer_visible && s_designer_canvas)
        aroma_ui_set_focused_node(s_designer_canvas);
    else if (s_editor)
        aroma_code_editor_focus(s_editor);
}

static void open_dialog(IdeDialogMode mode, const char *initial)
{
    s_dialog_mode = mode;
    s_dialog_open = true;
    s_dialog_error[0] = '\0';
    snprintf(s_dialog_text, sizeof(s_dialog_text), "%s", initial ? initial : "");
    if (mode == IDE_DIALOG_NEW_FILE && !s_dialog_text[0])
        snprintf(s_dialog_text, sizeof(s_dialog_text), "ui/new_screen.aroma");
    s_dialog_length = strlen(s_dialog_text);
    if (mode == IDE_DIALOG_NEW_PROJECT)
        wizard_open();
    if (mode == IDE_DIALOG_COMMAND_PALETTE)
        cmd_palette_init();
    if (mode == IDE_DIALOG_PROPERTIES)
    {
        s_dialog_text[0] = '\0';
        s_dialog_length = 0;
        s_dialog_prop_scroll = 0;
        s_dialog_prop_sel = 0;
        s_dialog_prop_edit_row = -1;
        s_dialog_prop_value[0] = '\0';
        s_prop_last_panel_y = 0;
        props_update_geometry();
    }
    else if (mode == IDE_DIALOG_ANDROID_DEVICES)
    {
        s_dialog_w = win_w - 160;
        if (s_dialog_w > 700)
            s_dialog_w = 700;
        if (s_dialog_w < 480)
            s_dialog_w = win_w - 40;
        s_dialog_h = win_h - 140;
        if (s_dialog_h > 560)
            s_dialog_h = 560;
        if (s_dialog_h < 380)
            s_dialog_h = win_h - 60;
        s_dialog_x = (win_w - s_dialog_w) / 2;
        s_dialog_y = (win_h - s_dialog_h) / 2;
        s_android_device_scroll = 0;
        s_android_device_hover = -1;
    }
    else if (mode != IDE_DIALOG_NEW_PROJECT)
    {
        dialog_reset_geometry();
    }
    if (s_dialog_canvas)
    {
        aroma_node_set_hit_test_cb(s_dialog_canvas,
                                   mode == IDE_DIALOG_PROPERTIES
                                       ? props_hit_test
                                       : NULL,
                                   NULL);
        aroma_node_set_hidden(s_dialog_canvas, false);
        if (mode != IDE_DIALOG_PROPERTIES)
            aroma_ui_set_focused_node(s_dialog_canvas);
    }
    if (mode == IDE_DIALOG_PROPERTIES && s_designer_visible &&
        s_designer_canvas)
        aroma_ui_set_focused_node(s_designer_canvas);
    draw_dialog();
}

static void dialog_submit(void)
{
    char value[IDE_PATH_MAX];
    snprintf(value, sizeof(value), "%s", s_dialog_text);
    char *start = value;
    while (*start == ' ' || *start == '\t')
        start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    if (!start[0])
    {
        snprintf(s_dialog_error, sizeof(s_dialog_error), "Enter a value first");
        draw_dialog();
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_SEARCH)
    {
        workspace_search(start);
        s_sidebar_mode = 1;
        close_dialog();
        draw_sidebar();
        draw_rail();
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_NEW_FILE)
    {
        int index = ide_workspace_create_file(start);
        if (index < 0)
        {
            snprintf(s_dialog_error, sizeof(s_dialog_error), "%s",
                     ide_workspace_last_error());
            draw_dialog();
            return;
        }
        tree_build();
        workspace_search(s_search_query);
        close_dialog();
        ide_open_file(index, false);
        log_add("workspace/applyEdit", "Created %s", ide_files[index].path);
        toast("Created %s", ide_files[index].path);
        draw_all();
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_OPEN_FILE)
    {
        int index = ide_workspace_file_by_path(start);
        if (index < 0)
        {
            for (int i = 0; i < ide_file_count; i++)
            {
                if (strcmp(ide_files[i].name, start) == 0)
                {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0)
        {
            snprintf(s_dialog_error, sizeof(s_dialog_error),
                     "No file matches that path in this project");
            draw_dialog();
            return;
        }
        close_dialog();
        ide_open_file(index, false);
        draw_all();
    }

    if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
    {
                                              
        char name[PROJECT_NAME_MAX];
        char path[PROJECT_PATH_MAX];
        name[0] = '\0';
        path[0] = '\0';

        const char *pipe = strchr(start, '|');
        if (pipe)
        {
            size_t nlen = (size_t)(pipe - start);
            if (nlen >= sizeof(name))
                nlen = sizeof(name) - 1;
            memcpy(name, start, nlen);
            name[nlen] = '\0';
            snprintf(path, sizeof(path), "%s", pipe + 1);
        }
        else
        {
            size_t nlen = strlen(start);
            if (nlen >= sizeof(name))
                nlen = sizeof(name) - 1;
            memcpy(name, start, nlen);
            name[nlen] = '\0';
            snprintf(path, sizeof(path), "%s", ide_workspace_root);
        }

        if (s_home_mode)
        {
            if (!project_create_extended(name, path, 1, true, true))
            {
                snprintf(s_dialog_error, sizeof(s_dialog_error),
                         "Could not create project %s", name);
                draw_dialog();
                return;
            }
            snprintf(s_home_project_path, sizeof(s_home_project_path), "%s/%s", path, name);
            s_home_mode = false;
            close_dialog();
            return;
        }

        if (project_create(name, path))
        {
            close_dialog();
            draw_all();
            project_show_in_sidebar();
        }
        else
        {
            int n = snprintf(s_dialog_error, sizeof(s_dialog_error),
                             "Failed to create project: ");
            if (n > 0 && (size_t)n < sizeof(s_dialog_error))
            {
                size_t rem = sizeof(s_dialog_error) - (size_t)n - 1;
                size_t cpy = strlen(name) < rem ? strlen(name) : rem;
                memcpy(s_dialog_error + n, name, cpy);
                s_dialog_error[n + cpy] = '\0';
            }
            draw_dialog();
        }
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_OPEN_PROJECT)
    {
        if (s_home_mode)
        {
            char json_path[PROJECT_PATH_MAX + 16];
            snprintf(json_path, sizeof(json_path), "%s/aroma.json", start);
            if (access(json_path, R_OK) != 0)
            {
                snprintf(s_dialog_error, sizeof(s_dialog_error),
                         "No aroma.json project file found in that folder");
                draw_dialog();
                return;
            }
            snprintf(s_home_project_path, sizeof(s_home_project_path), "%s", start);
            s_home_mode = false;

                                                                                
            snprintf(s_home_recent_project_path, sizeof(s_home_recent_project_path),
                     "%s", start);
            snprintf(s_home_recent_project_name, sizeof(s_home_recent_project_name),
                     "%s", base_name(start));
            s_home_has_recent_project = true;
            ide_home_set_recent_project(start);

            close_dialog();
            return;
        }
        if (project_open(start))
        {
            close_dialog();
            draw_all();
            project_show_in_sidebar();
        }
        else
        {
            draw_dialog();
        }
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_COMMAND_PALETTE)
    {
        close_dialog();
        if (strcmp(start, "build") == 0 || strcmp(start, "> build") == 0)
        {
            project_build();
            toast("Build started");
        }
        else if (strcmp(start, "run") == 0 || strcmp(start, "> run") == 0)
        {
            project_run();
            toast("Run started");
        }
        else if (strcmp(start, "new file") == 0 || strcmp(start, "> new file") == 0)
        {
            open_dialog(IDE_DIALOG_NEW_FILE, "");
        }
        else if (strcmp(start, "open file") == 0 || strcmp(start, "> open file") == 0)
        {
            open_dialog(IDE_DIALOG_OPEN_FILE, "");
        }
        else if (strcmp(start, "new project") == 0 ||
                 strcmp(start, "> new project") == 0)
        {
            open_dialog(IDE_DIALOG_NEW_PROJECT, "");
        }
        else if (strcmp(start, "open project") == 0 ||
                 strcmp(start, "> open project") == 0)
        {
            open_dialog(IDE_DIALOG_OPEN_PROJECT, "");
        }
        else
        {
            int n = snprintf(s_dialog_error, sizeof(s_dialog_error),
                             "Unknown command: ");
            if (n > 0 && (size_t)n < sizeof(s_dialog_error))
            {
                size_t rem = sizeof(s_dialog_error) - (size_t)n - 1;
                size_t cpy = strlen(start) < rem ? strlen(start) : rem;
                memcpy(s_dialog_error + n, start, cpy);
                s_dialog_error[n + cpy] = '\0';
            }
            draw_dialog();
            return;
        }
        return;
    }

    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
    {
        properties_dialog_submit();
        return;
    }
}

static void properties_dialog_submit(void)
{
    if (!s_designer_visible || s_design_doc.selected < 0 ||
        s_design_doc.selected >= s_design_doc.node_count)
    {
        close_dialog();
        return;
    }
    if (s_dialog_prop_edit_row >= 0)
    {
        int schema_idx = props_schema_at(s_dialog_prop_edit_row);
        if (schema_idx >= 0 && props_apply(schema_idx, s_dialog_prop_value))
            s_dialog_prop_edit_row = -1;
    }
    draw_dialog();
}

static void draw_properties_dialog(void)
{
    if (!s_dialog_canvas)
        return;
    aroma_canvas_reset(s_dialog_canvas);
    if (!s_dialog_open)
        return;

    props_update_geometry();
    props_clamp_scroll();

    AromaFont *ui12 = font_ui(12);
    AromaFont *ui10 = font_ui(10);

    fill(s_dialog_canvas, s_dialog_x - 4, s_dialog_y, s_dialog_w + 4,
         s_dialog_h, 0, C_PANEL);
    line(s_dialog_canvas, s_dialog_x, s_dialog_y, s_dialog_x,
         s_dialog_y + s_dialog_h, C_BORDER, 1);
    fill(s_dialog_canvas, s_dialog_x, s_dialog_y, s_dialog_w, PROPS_HEADER_H,
         0, M3_SURFACE_LOW);

    text(s_dialog_canvas, "PROPERTIES", s_dialog_x + 18, s_dialog_y + 22,
         C_TEXT, ui12);
    icon_center(s_dialog_canvas, AROMA_ICON_CLOSE, s_dialog_x + s_dialog_w - 20,
                s_dialog_y + 20, 14, C_TEXT_DIM);

    int index = s_design_doc.selected;
    if (index < 0 || index >= s_design_doc.node_count)
    {
        text(s_dialog_canvas, "No widget selected", s_dialog_x + 18,
             s_dialog_y + 44, C_TEXT_DIM, ui10);
        text(s_dialog_canvas, "Select a widget in the designer",
             s_dialog_x + 18, s_dialog_y + 60, C_TEXT_FAINT, ui10);
        text(s_dialog_canvas, "Press Escape to close", s_dialog_x + 18,
             s_dialog_y + s_dialog_h - 30, C_TEXT_FAINT, ui10);
        return;
    }

    IdeDesignNode *node = &s_design_doc.nodes[index];
    text(s_dialog_canvas, node->tag, s_dialog_x + 18, s_dialog_y + 44,
         C_TEXT_SOFT, ui10);

    int prop_count = props_count();
    int visible_rows = props_visible_rows();
    int y0 = s_dialog_y + PROPS_HEADER_H;
    int value_w = s_dialog_w - PROPS_VALUE_X - 44;

    for (int i = 0; i < visible_rows; i++)
    {
        int row = s_dialog_prop_scroll + i;
        if (row >= prop_count)
            break;
        int schema_idx = props_schema_at(row);
        if (schema_idx < 0)
            continue;
        const PropSchemaEntry *schema = &k_prop_schema_all[schema_idx];
        const char *cur_val = ide_design_attr(&s_design_doc, index, schema->key);
        if (!cur_val)
            cur_val = "";
        bool editing = s_dialog_prop_edit_row == row;
        bool selected = s_dialog_prop_sel == row;
        int ry = y0 + i * PROPS_ROW_H;

        if (selected)
            fill(s_dialog_canvas, s_dialog_x + 20, ry, s_dialog_w - 40,
                 PROPS_ROW_H - 2, 8, C_SELECT);
        if (editing)
            fill(s_dialog_canvas, s_dialog_x + PROPS_VALUE_X - 6, ry + 1,
                 value_w + 12, PROPS_ROW_H - 4, 3, M3_SURFACE);

        text(s_dialog_canvas, schema->label ? schema->label : schema->key,
             s_dialog_x + 32, ry + 5, selected ? C_TEXT : C_TEXT_SOFT, ui10);
        if (editing)
        {
            char shown[IDE_DESIGN_VALUE_MAX + 4];
            snprintf(shown, sizeof(shown), "%s_", s_dialog_prop_value);
            text_fit(s_dialog_canvas, shown, s_dialog_x + PROPS_VALUE_X,
                     ry + 5, value_w, C_TEXT, ui10);
        }
        else
            text_fit(s_dialog_canvas, cur_val[0] ? cur_val : "-",
                     s_dialog_x + PROPS_VALUE_X, ry + 5, value_w,
                     cur_val[0] ? C_TEXT : C_TEXT_FAINT, ui10);

        int opt_count = 0;
        if (props_options(schema_idx, &opt_count) && opt_count > 0)
            text(s_dialog_canvas, "<  >", s_dialog_x + s_dialog_w - 52,
                 ry + 5, C_TEXT_DIM, ui10);
    }

    if (prop_count > visible_rows)
    {
        int track_h = visible_rows * PROPS_ROW_H;
        int thumb_h = track_h * visible_rows / prop_count;
        if (thumb_h < 12)
            thumb_h = 12;
        int travel = track_h - thumb_h;
        int max_scroll = prop_count - visible_rows;
        int thumb_y = y0 + (max_scroll > 0
                                ? s_dialog_prop_scroll * travel / max_scroll
                                : 0);
        fill(s_dialog_canvas, s_dialog_x + s_dialog_w - 10, thumb_y, 4,
             thumb_h, 2, C_PRIMARY);
    }

    text(s_dialog_canvas,
         s_dialog_prop_edit_row >= 0
             ? "Type a value   Enter: apply   Esc: cancel"
             : "Up/Down: select   Enter/click: edit   Left/Right: options   Esc: close",
         s_dialog_x + 18, s_dialog_y + s_dialog_h - 32, C_TEXT_FAINT, ui10);
    if (s_dialog_error[0])
        text_fit(s_dialog_canvas, s_dialog_error, s_dialog_x + 18,
                 s_dialog_y + s_dialog_h - 16, s_dialog_w - 36, M3_PINK,
                 ui10);
}

static bool wizard_mkdir_p(const char *path)
{
    if (!path || !path[0])
        return false;
    char buffer[PROJECT_PATH_MAX];
    if (snprintf(buffer, sizeof(buffer), "%s", path) >= (int)sizeof(buffer))
        return false;
    for (char *p = buffer + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(buffer, 0755) == 0 || errno == EEXIST;
}

static bool wizard_write_file(const char *root, const char *relative,
                              const char *contents)
{
    char path[PROJECT_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", root, relative) >=
        (int)sizeof(path))
        return false;
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    size_t length = strlen(contents);
    bool ok = fwrite(contents, 1, length, file) == length;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static void wizard_cmake_identifier(const char *name, char *out,
                                    size_t out_size)
{
    size_t used = 0;
    for (const char *p = name; *p && used + 1 < out_size; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_' || c == '-')
            out[used++] = (char)c;
        else
            out[used++] = '_';
    }
    if (used == 0 && out_size > 1)
        out[used++] = 'a';
    out[used] = '\0';
}

static void wizard_default_markup(int template_id, const char *title,
                                  char *out, size_t out_size)
{
    if (template_id == 1)
    {
        snprintf(out, out_size,
                 "Window {\n"
                 "    width: 800\n"
                 "    height: 600\n"
                 "    title: \"%s\"\n"
                 "\n"
                 "    Card {\n"
                 "        x: 0\n"
                 "        y: 0\n"
                 "        width: 228\n"
                 "        height: 600\n"
                 "        type: filled\n"
                 "        color: \"#171A21\"\n"
                 "        corner_radius: 0\n"
                 "\n"
                 "        Label { text: \"%s\" x: 24 y: 28 style: \"large\" }\n"
                 "        Label { text: \"NOTES\" x: 24 y: 62 style: \"small\" }\n"
                 "        Button {\n"
                 "            text: \"New note\"\n"
                 "            x: 24\n"
                 "            y: 94\n"
                 "            width: 180\n"
                 "            height: 44\n"
                 "            type: filled\n"
                 "            on_click: \"new_note\"\n"
                 "        }\n"
                 "        Label { text: \"Welcome\" x: 24 y: 172 style: \"medium\" }\n"
                 "        Label { text: \"Drag a Label or Button onto this page.\" x: 24 y: 198 style: \"small\" }\n"
                 "    }\n"
                 "\n"
                 "    Label { text: \"Today\" x: 260 y: 32 style: \"large\" }\n"
                 "    Card {\n"
                 "        x: 260\n"
                 "        y: 80\n"
                 "        width: 500\n"
                 "        height: 150\n"
                 "        type: elevated\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Your first card\" x: 22 y: 20 style: \"large\" }\n"
                 "        Label { text: \"Edit the markup in Code view or drop widgets here.\" x: 22 y: 56 style: \"small\" }\n"
                 "    }\n"
                 "    Card {\n"
                 "        x: 260\n"
                 "        y: 250\n"
                 "        width: 500\n"
                 "        height: 120\n"
                 "        type: outlined\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Next steps\" x: 22 y: 20 style: \"medium\" }\n"
                 "        Label { text: \"Press F7 to build and F5 to run the project.\" x: 22 y: 52 style: \"small\" }\n"
                 "    }\n"
                 "    Label { id: \"status\" text: \"Ready\" x: 260 y: 410 style: \"small\" }\n"
                 "}\n",
                 title, title);
        return;
    }
    if (template_id == 2)
    {
        snprintf(out, out_size,
                 "Window {\n"
                 "    width: 900\n"
                 "    height: 620\n"
                 "    title: \"%s\"\n"
                 "\n"
                 "    Card {\n"
                 "        x: 0\n"
                 "        y: 0\n"
                 "        width: 900\n"
                 "        height: 64\n"
                 "        type: filled\n"
                 "        color: \"#171A21\"\n"
                 "        corner_radius: 0\n"
                 "\n"
                 "        Label { text: \"%s\" x: 28 y: 22 style: \"large\" }\n"
                 "    }\n"
                 "    Label { text: \"OVERVIEW\" x: 28 y: 92 style: \"small\" }\n"
                 "    Card {\n"
                 "        x: 28\n"
                 "        y: 122\n"
                 "        width: 260\n"
                 "        height: 120\n"
                 "        type: elevated\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Active users\" x: 20 y: 18 style: \"small\" }\n"
                 "        Label { text: \"12,480\" x: 20 y: 48 style: \"large\" }\n"
                 "    }\n"
                 "    Card {\n"
                 "        x: 304\n"
                 "        y: 122\n"
                 "        width: 260\n"
                 "        height: 120\n"
                 "        type: elevated\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Sessions\" x: 20 y: 18 style: \"small\" }\n"
                 "        Label { text: \"38,921\" x: 20 y: 48 style: \"large\" }\n"
                 "    }\n"
                 "    Card {\n"
                 "        x: 580\n"
                 "        y: 122\n"
                 "        width: 260\n"
                 "        height: 120\n"
                 "        type: outlined\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Conversion\" x: 20 y: 18 style: \"small\" }\n"
                 "        Label { text: \"4.8%%\" x: 20 y: 48 style: \"large\" }\n"
                 "    }\n"
                 "    Card {\n"
                 "        x: 28\n"
                 "        y: 274\n"
                 "        width: 812\n"
                 "        height: 270\n"
                 "        type: outlined\n"
                 "        corner_radius: 12\n"
                 "\n"
                 "        Label { text: \"Activity\" x: 24 y: 20 style: \"medium\" }\n"
                 "        Canvas { x: 24 y: 58 width: 764 height: 180 }\n"
                 "    }\n"
                 "}\n",
                 title, title);
        return;
    }
    snprintf(out, out_size,
             "Window {\n"
             "    width: 800\n"
             "    height: 600\n"
             "    title: \"%s\"\n"
             "\n"
             "    Label { text: \"%s\" x: 32 y: 28 style: \"large\" }\n"
             "    Label { text: \"A blank AromaUI screen, ready for the designer.\" x: 32 y: 64 style: \"small\" }\n"
             "    Button { text: \"Get started\" x: 32 y: 104 width: 160 height: 44 type: filled }\n"
             "}\n",
             title, title);
}

static void wizard_default_main(char *out, size_t out_size)
{
    snprintf(out, out_size,
             "#include <aroma.h>\n"
             "#include <aroma_incense_loader.h>\n"
             "#include <unistd.h>\n"
             "#include <stdio.h>\n"
             "\n"
             "static AromaFont *g_font;\n"
             "static AromaFont *g_icon_font;\n"
             "static IncenseRegistry *g_registry;\n"
             "\n"
             "static void new_note(void *user_data)\n"
             "{\n"
             "    (void)user_data;\n"
             "    AromaNode *status = g_registry ? IncenseFindWidgetQuiet(g_registry, \"status\") : NULL;\n"
             "    if (status)\n"
             "        aroma_label_set_text(status, \"New note added\");\n"
             "}\n"
             "\n"
             "int main(void)\n"
             "{\n"
             "    aroma_ui_init();\n"
             "    AromaTheme theme = aroma_theme_create_material_black();\n"
             "    theme.colors.primary = M3_PRIMARY;\n"
             "    theme.colors.background = M3_SURFACE;\n"
             "    theme.colors.surface = M3_SURFACE_LOW;\n"
             "    theme.colors.text_primary = M3_ON_SURFACE;\n"
             "    theme.colors.text_secondary = M3_TEXT_DIM;\n"
             "    theme.colors.border = M3_SURFACE_HIGH;\n"
             "    aroma_ui_set_theme(&theme);\n"
             "\n"
             "    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);\n"
             "    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);\n"
             "    if (!g_font || !g_icon_font)\n"
             "        return 1;\n"
             "\n"
             "    IncenseRegisterCallback(\"new_note\", INCENSE_CALLBACK_VOID_PTR, new_note, NULL);\n"
             "    AromaWindow *window = IncenseLoadFileEx(\"ui/app.aroma\", g_font, g_icon_font, &g_registry);\n"
             "    if (!window)\n"
             "    {\n"
             "        int count = 0;\n"
             "        const IncenseError *errors = IncenseGetErrors(&count);\n"
             "        for (int i = 0; i < count; i++)\n"
             "            fprintf(stderr, \"line %%d: %%s\\n\", errors[i].line, errors[i].message);\n"
             "        return 1;\n"
             "    }\n"
             "\n"
             "    while (aroma_ui_is_running())\n"
             "    {\n"
             "        aroma_ui_process_events();\n"
             "        aroma_ui_render(window);\n"
             "        usleep(16000);\n"
             "    }\n"
             "\n"
             "    IncenseFreeRegistry(g_registry);\n"
             "    aroma_font_destroy(g_font);\n"
             "    aroma_font_destroy(g_icon_font);\n"
             "    aroma_ui_destroy_window(window);\n"
             "    aroma_ui_shutdown();\n"
             "    return 0;\n"
             "}\n");
}

static const char *const k_wizard_template_titles[3] = {
    "Empty canvas", "Notes app", "Dashboard"};
static const char *const k_wizard_template_descriptions[3] = {
    "A blank AromaUI window with a headline and one button.",
    "Sidebar, note cards and a working new-note callback.",
    "Metric cards, a chart canvas and a top app bar."};
static const char *k_wizard_step_labels[4] = {
    "Details", "Template", "Options", "Review"};

static void wizard_redraw(void *user_data)
{
    (void)user_data;
    draw_dialog();
}

static void wizard_create_stepper(void)
{
    AromaNode *host = s_root ? s_root : s_home_host;
    if (!host)
        return;
    if (!s_wizard_stepper)
    {
        s_wizard_stepper = aroma_stepper_create_steps(
            host, s_wiz_x + 32, WIZ_STEPPER_Y, s_wiz_w - 64, 56,
            k_wizard_step_labels, 4);
        if (!s_wizard_stepper)
            return;
        aroma_node_set_z_index(s_wizard_stepper, 186);
        aroma_stepper_set_font(s_wizard_stepper, font_ui(11));
        aroma_stepper_setup_events(s_wizard_stepper, wizard_redraw, NULL);
        aroma_stepper_set_on_change(s_wizard_stepper, wizard_stepper_changed,
                                    NULL);
    }
    aroma_node_set_hidden(s_wizard_stepper, false);
    aroma_stepper_set_step_index(s_wizard_stepper, s_wizard_step);
    aroma_node_invalidate(s_wizard_stepper);
}

static void wizard_close_widgets(void)
{
    if (s_wizard_stepper)
        aroma_node_set_hidden(s_wizard_stepper, true);
}

static void wizard_trim(char *value)
{
    if (!value)
        return;
    char *start = value;
    while (*start == ' ' || *start == '\t')
        start++;
    if (start != value)
        memmove(value, start, strlen(start) + 1);
    size_t length = strlen(value);
    while (length > 0 &&
           (value[length - 1] == ' ' || value[length - 1] == '\t' ||
            value[length - 1] == '\r' || value[length - 1] == '\n'))
        value[--length] = '\0';
}

static bool wizard_validate_current(void)
{
    s_wizard_error[0] = '\0';
    if (s_wizard_step == 0)
    {
        wizard_trim(s_wizard_name);
        wizard_trim(s_wizard_path);
        if (!s_wizard_name[0])
        {
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "Enter a project name");
            return false;
        }
        if (strchr(s_wizard_name, '/') || strchr(s_wizard_name, '\\') ||
            strcmp(s_wizard_name, ".") == 0 ||
            strcmp(s_wizard_name, "..") == 0)
        {
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "The project name cannot contain path separators");
            return false;
        }
        if (!s_wizard_path[0])
        {
            snprintf(s_wizard_path, sizeof(s_wizard_path), "%s",
                     ide_workspace_root);
        }
        if (access(s_wizard_path, W_OK) != 0)
        {
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "The parent folder is not writable");
            return false;
        }
        char target[PROJECT_PATH_MAX];
        if (snprintf(target, sizeof(target), "%s/%s", s_wizard_path,
                     s_wizard_name) >= (int)sizeof(target))
        {
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "The project path is too long");
            return false;
        }
        if (access(target, F_OK) == 0)
        {
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "That folder already exists: %s", target);
            return false;
        }
        return true;
    }
    if (s_wizard_step == 1)
        return s_wizard_template >= 0 && s_wizard_template < 3;
    return true;
}

static void wizard_set_step(int step)
{
    if (step < 0)
        step = 0;
    if (step > 3)
        step = 3;
    s_wizard_step = step;
    if (s_wizard_stepper)
        aroma_stepper_set_step_index(s_wizard_stepper, step);
    draw_dialog();
}

static void wizard_next(void)
{
    if (!wizard_validate_current())
    {
        draw_dialog();
        return;
    }
    if (s_wizard_step < 3)
        wizard_set_step(s_wizard_step + 1);
    else
        wizard_create_project();
}

static void wizard_back(void)
{
    if (s_wizard_step > 0)
        wizard_set_step(s_wizard_step - 1);
}

static void wizard_stepper_changed(AromaNode *node, int value, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!s_dialog_open || s_dialog_mode != IDE_DIALOG_NEW_PROJECT)
        return;
    if (value == s_wizard_step)
        return;
    if (value > s_wizard_step && !wizard_validate_current())
    {
        if (s_wizard_stepper)
            aroma_stepper_set_step_index(s_wizard_stepper, s_wizard_step);
        draw_dialog();
        return;
    }
    s_wizard_step = value;
    if (s_wizard_stepper)
        aroma_stepper_set_step_index(s_wizard_stepper, value);
    draw_dialog();
}

static void wizard_open(void)
{
    s_wizard_step = 0;
    s_wizard_focus = 0;
    s_wizard_template = 1;
    s_wizard_hover = -1;
    s_wizard_with_cmake = true;
    s_wizard_with_readme = true;
    s_wizard_open_after = true;
    s_wizard_name[0] = '\0';
    s_wizard_error[0] = '\0';
    snprintf(s_wizard_path, sizeof(s_wizard_path), "%s", ide_workspace_root);
    wizard_create_stepper();
}

static void wizard_browse_folder(void)
{
    char *selected = NULL;
    AromaFileDialogResult result = aroma_file_dialog_pick_folder(
        "Choose the parent folder", s_wizard_path[0] ? s_wizard_path
                                                     : ide_workspace_root,
        &selected);
    if (result == AROMA_FILE_DIALOG_OK && selected && selected[0])
    {
        snprintf(s_wizard_path, sizeof(s_wizard_path), "%s", selected);
        s_wizard_error[0] = '\0';
    }
    else if (result == AROMA_FILE_DIALOG_ERROR)
    {
        snprintf(s_wizard_error, sizeof(s_wizard_error), "%s",
                 aroma_file_dialog_last_error());
    }
    aroma_file_dialog_free(selected);
    s_wizard_focus = 1;
    draw_dialog();
}

static void wizard_append_char(char *buffer, size_t size, char c)
{
    size_t length = strlen(buffer);
    if (length + 1 >= size)
        return;
    if (c < 32 || c > 126)
        return;
    buffer[length] = c;
    buffer[length + 1] = '\0';
}

static bool wizard_on_key(AromaEvent *event)
{
    if (!event || event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;
    uint32_t key = event->data.key.key_code;
    if (key == 27 || key == 0xFF1B)
    {
        close_dialog();
        return true;
    }
    if (key == 9)
    {
        s_wizard_focus = (s_wizard_focus + 1) % 2;
        draw_dialog();
        return true;
    }
    if (key == 13 || key == 10 || key == 0xFF0D)
    {
        wizard_next();
        return true;
    }
    char *field = s_wizard_focus == 0 ? s_wizard_name : s_wizard_path;
    size_t field_size = s_wizard_focus == 0 ? sizeof(s_wizard_name)
                                            : sizeof(s_wizard_path);
    if (key == 8 || key == 0xFF08 || key == 0x007F)
    {
        size_t length = strlen(field);
        if (length > 0)
            field[length - 1] = '\0';
        s_wizard_error[0] = '\0';
        draw_dialog();
        return true;
    }
    if (key >= 32 && key <= 126)
    {
        wizard_append_char(field, field_size, (char)key);
        s_wizard_error[0] = '\0';
        draw_dialog();
    }
    return true;
}

static void wizard_draw_button(AromaNode *canvas, int x, int y, int w, int h,
                               const char *label, bool primary, int hover_id)
{
    uint32_t bg = primary ? C_PRIMARY : M3_SURFACE_HIGH;
    uint32_t border = primary ? C_PRIMARY : C_BORDER_SOFT;
    uint32_t fg = primary ? M3_ON_PRIMARY : C_TEXT_SOFT;
    if (s_wizard_hover == hover_id)
    {
        bg = primary ? M3_PRIMARY : M3_SURFACE_HIGHEST;
        border = primary ? M3_PRIMARY : M3_OUTLINE_VAR;
    }
    (void)border;
    fill(canvas, x, y, w, h, h / 2, bg);
    AromaFont *font = font_ui(11);
    int tw = text_width(font, label);
    text(canvas, label, x + (w - tw) / 2, y + (h - 14) / 2, fg, font);
}

static void wizard_draw_checkbox(AromaNode *canvas, int x, int y, bool checked,
                                 const char *label)
{
    fill(canvas, x, y, 18, 18, 4, checked ? C_PRIMARY : M3_SURFACE_CONTAINER);
    if (checked)
        icon_center(canvas, AROMA_ICON_CHECK, x + 9, y + 9, 12, M3_ON_PRIMARY);
    else
        fill(canvas, x, y, 18, 1, 0, C_BORDER_SOFT);
    text(canvas, label, x + 28, y + 1, C_TEXT_SOFT, font_ui(11));
}

static void wizard_draw_input(AromaNode *canvas, int x, int y, int w, int h,
                              const char *value, const char *placeholder,
                              bool focused)
{
    fill(canvas, x, y, w, h, 6, M3_SURFACE_CONTAINER);
    fill(canvas, x, y, w, 1, 0, focused ? C_PRIMARY : C_BORDER_SOFT);
    if (focused)
        fill(canvas, x, y, 2, h, 0, C_PRIMARY);
    AromaFont *body = font_ui(12);
    const char *shown = value && value[0] ? value : placeholder;
    text_fit(canvas, shown, x + 14, y + (h - 16) / 2, w - 28,
             value && value[0] ? C_TEXT : C_TEXT_FAINT, body);
    if (focused)
    {
        int caret = x + 14 + (value ? text_width(body, value) : 0);
        if (caret > x + w - 16)
            caret = x + w - 16;
        fill(canvas, caret, y + 12, 1, h - 24, 0, C_PRIMARY);
    }
}

static void wizard_draw(void)
{
    if (!s_dialog_canvas)
        return;
    AromaFont *ui10 = font_ui(10);
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui12 = font_ui(12);
    AromaFont *ui14 = font_ui(14);
    AromaFont *mono = font_mono(12);

    fill(s_dialog_canvas, 0, 0, win_w, win_h, 0, 0x66000000u);
    fill(s_dialog_canvas, s_wiz_x, s_wiz_y + 6, s_wiz_w, s_wiz_h, 28,
         0x44000000u);
    fill(s_dialog_canvas, s_wiz_x, s_wiz_y, s_wiz_w, s_wiz_h, 28, M3_SURFACE_HIGH);

    text(s_dialog_canvas, "NEW AROMA PROJECT", s_wiz_x + 32, s_wiz_y + 22,
         C_TEXT, ui14);
    text(s_dialog_canvas, "Create a real project on disk. Every step is editable.",
         s_wiz_x + 32, s_wiz_y + 46, C_TEXT_DIM, ui10);
    fill(s_dialog_canvas, s_wiz_x + 32, s_wiz_y + 138, s_wiz_w - 64, 1, 0,
         C_BORDER);

    if (s_wizard_step == 0)
    {
        text(s_dialog_canvas, "Project name", s_wiz_x + 32, WIZ_CONTENT_Y + 4,
             C_TEXT_SOFT, ui11);
        wizard_draw_input(s_dialog_canvas, s_wiz_x + 32, WIZ_CONTENT_Y + 26, 410,
                          44, s_wizard_name, "my_aroma_app",
                          s_wizard_focus == 0);
        text(s_dialog_canvas, "Parent folder", s_wiz_x + 32, WIZ_CONTENT_Y + 86,
             C_TEXT_SOFT, ui11);
        wizard_draw_input(s_dialog_canvas, s_wiz_x + 32, WIZ_CONTENT_Y + 108,
                          540, 44, s_wizard_path, ide_workspace_root,
                          s_wizard_focus == 1);
        wizard_draw_button(s_dialog_canvas, s_wiz_x + 584, WIZ_CONTENT_Y + 108,
                           136, 44, "Browse...", false, 4);

        fill(s_dialog_canvas, s_wiz_x + 32, WIZ_CONTENT_Y + 176, s_wiz_w - 64, 64,
             8, 0xFF2C2C2E);
        icon_center(s_dialog_canvas, AROMA_ICON_FOLDER, s_wiz_x + 52,
                    WIZ_CONTENT_Y + 208, 18, C_PRIMARY);
        char preview[PROJECT_PATH_MAX];
        snprintf(preview, sizeof(preview), "%s/%s",
                 s_wizard_path[0] ? s_wizard_path : ide_workspace_root,
                 s_wizard_name[0] ? s_wizard_name : "my_aroma_app");
        text(s_dialog_canvas, "The project will be created at", s_wiz_x + 72,
             WIZ_CONTENT_Y + 188, C_TEXT_FAINT, ui10);
        text_fit(s_dialog_canvas, preview, s_wiz_x + 72, WIZ_CONTENT_Y + 206,
                 s_wiz_w - 128, C_TEXT, mono);
    }
    else if (s_wizard_step == 1)
    {
        text(s_dialog_canvas, "Choose a starting template", s_wiz_x + 32,
             WIZ_CONTENT_Y + 4, C_TEXT_SOFT, ui11);
        text(s_dialog_canvas, "The markup is written to ui/app.aroma and opens in the designer.",
             s_wiz_x + 32, WIZ_CONTENT_Y + 24, C_TEXT_FAINT, ui10);
        for (int i = 0; i < 3; i++)
        {
            int card_x = s_wiz_x + 32 + i * 226;
            int card_y = WIZ_CONTENT_Y + 58;
            bool selected = s_wizard_template == i;
            bool hovered = s_wizard_hover == 10 + i;
            fill(s_dialog_canvas, card_x, card_y, 214, 196, 10,
                 selected ? M3_SURFACE_HIGH : hovered ? 0xFF3A3A3C : 0xFF2C2C2E);
            fill(s_dialog_canvas, card_x, card_y, 214, 2, 0,
                 selected ? C_PRIMARY : C_BORDER);
            if (selected)
                fill(s_dialog_canvas, card_x, card_y, 3, 196, 1, C_PRIMARY);
            const char *icon = i == 0 ? AROMA_ICON_CROP_SQUARE
                                      : i == 1 ? AROMA_ICON_DESCRIPTION
                                               : AROMA_ICON_DASHBOARD;
            icon_center(s_dialog_canvas, icon, card_x + 34, card_y + 42, 24,
                        selected ? C_PRIMARY : C_TEXT_DIM);
            text(s_dialog_canvas, k_wizard_template_titles[i], card_x + 18,
                 card_y + 74, C_TEXT, ui12);
            text_fit(s_dialog_canvas, k_wizard_template_descriptions[i],
                     card_x + 18, card_y + 102, 178, C_TEXT_DIM, ui10);
            if (selected)
                icon_center(s_dialog_canvas, AROMA_ICON_CHECK_CIRCLE,
                            card_x + 190, card_y + 20, 15, C_PRIMARY);
        }
    }
    else if (s_wizard_step == 2)
    {
        text(s_dialog_canvas, "Project options", s_wiz_x + 32,
             WIZ_CONTENT_Y + 4, C_TEXT_SOFT, ui11);
        text(s_dialog_canvas, "These choices change the files written to disk.",
             s_wiz_x + 32, WIZ_CONTENT_Y + 24, C_TEXT_FAINT, ui10);
        int row_y = WIZ_CONTENT_Y + 66;
        fill(s_dialog_canvas, s_wiz_x + 32, row_y - 12, s_wiz_w - 64, 58, 8,
             0xFF2C2C2E);
        wizard_draw_checkbox(s_dialog_canvas, s_wiz_x + 52, row_y,
                             s_wizard_with_cmake, "CMakeLists.txt");
        text(s_dialog_canvas, "Build the app with CMake and the Aroma SDK",
             s_wiz_x + 80, row_y + 20, C_TEXT_FAINT, ui10);
        row_y += 82;
        fill(s_dialog_canvas, s_wiz_x + 32, row_y - 12, s_wiz_w - 64, 58, 8,
             0xFF2C2C2E);
        wizard_draw_checkbox(s_dialog_canvas, s_wiz_x + 52, row_y,
                             s_wizard_with_readme, "README.md");
        text(s_dialog_canvas, "Document the project and its build commands",
             s_wiz_x + 80, row_y + 20, C_TEXT_FAINT, ui10);
        row_y += 82;
        fill(s_dialog_canvas, s_wiz_x + 32, row_y - 12, s_wiz_w - 64, 58, 8,
             0xFF2C2C2E);
        wizard_draw_checkbox(s_dialog_canvas, s_wiz_x + 52, row_y,
                             s_wizard_open_after, "Open the project after creation");
        text(s_dialog_canvas, "Switch the workspace to the new project",
             s_wiz_x + 80, row_y + 20, C_TEXT_FAINT, ui10);
    }
    else
    {
        text(s_dialog_canvas, "Review and create", s_wiz_x + 32,
             WIZ_CONTENT_Y + 4, C_TEXT_SOFT, ui11);
        int y = WIZ_CONTENT_Y + 40;
        const char *labels[5] = {"Name", "Location", "Template", "Build files",
                                 "README"};
        char values[5][PROJECT_PATH_MAX];
        snprintf(values[0], sizeof(values[0]), "%s",
                 s_wizard_name[0] ? s_wizard_name : "(missing)");
        snprintf(values[1], sizeof(values[1]), "%s/%s",
                 s_wizard_path[0] ? s_wizard_path : ide_workspace_root,
                 s_wizard_name[0] ? s_wizard_name : "");
        snprintf(values[2], sizeof(values[2]), "%s",
                 k_wizard_template_titles[s_wizard_template]);
        snprintf(values[3], sizeof(values[3]), "%s",
                 s_wizard_with_cmake ? "CMakeLists.txt" : "Not included");
        snprintf(values[4], sizeof(values[4]), "%s",
                 s_wizard_with_readme ? "README.md" : "Not included");
        for (int i = 0; i < 5; i++)
        {
            text(s_dialog_canvas, labels[i], s_wiz_x + 48, y + 6, C_TEXT_DIM,
                 ui10);
            text_fit(s_dialog_canvas, values[i], s_wiz_x + 156, y + 2,
                     s_wiz_w - 208, C_TEXT_SOFT, mono);
            fill(s_dialog_canvas, s_wiz_x + 32, y + 32, s_wiz_w - 64, 1, 0,
                 C_BORDER);
            y += 48;
        }
        fill(s_dialog_canvas, s_wiz_x + 32, y + 8, s_wiz_w - 64, 72, 8,
             M3_SURFACE_LOW);
        icon_center(s_dialog_canvas, AROMA_ICON_CHECK_CIRCLE, s_wiz_x + 54,
                    y + 44, 20, C_GREEN);
        text(s_dialog_canvas, "Ready to write the project", s_wiz_x + 76,
             y + 24, C_TEXT, ui12);
        text(s_dialog_canvas, "Press Create project to build the folder tree.",
             s_wiz_x + 76, y + 46, C_TEXT_DIM, ui10);
    }

    if (s_wizard_error[0])
        text_fit(s_dialog_canvas, s_wizard_error, s_wiz_x + 32,
                 WIZ_FOOTER_Y - 38, s_wiz_w - 64, C_RED, ui10);
    else
        text(s_dialog_canvas, "Step %d of 4", s_wiz_x + 32,
             WIZ_FOOTER_Y - 38, C_TEXT_FAINT, ui10);

    fill(s_dialog_canvas, s_wiz_x + 1, WIZ_FOOTER_Y - 16, s_wiz_w - 2, 1, 0,
         C_BORDER);
    wizard_draw_button(s_dialog_canvas, s_wiz_x + s_wiz_w - 124, WIZ_FOOTER_Y,
                       96, 36, s_wizard_step == 3 ? "Create" : "Next", true,
                       2);
    wizard_draw_button(s_dialog_canvas, s_wiz_x + s_wiz_w - 232, WIZ_FOOTER_Y,
                       96, 36, "Cancel", false, 3);
    if (s_wizard_step > 0)
        wizard_draw_button(s_dialog_canvas, s_wiz_x + s_wiz_w - 328, WIZ_FOOTER_Y,
                           88, 36, "Back", false, 1);
}

static bool wizard_on_click(int x, int y)
{
    if (x < s_wiz_x || x >= s_wiz_x + s_wiz_w || y < s_wiz_y || y >= s_wiz_y + s_wiz_h)
        return false;

    int footer_button_y = WIZ_FOOTER_Y;
    if (y >= footer_button_y && y < footer_button_y + 36)
    {
        if (x >= s_wiz_x + s_wiz_w - 124 && x < s_wiz_x + s_wiz_w - 28)
        {
            wizard_next();
            return true;
        }
        if (x >= s_wiz_x + s_wiz_w - 232 && x < s_wiz_x + s_wiz_w - 136)
        {
            close_dialog();
            return true;
        }
        if (s_wizard_step > 0 && x >= s_wiz_x + s_wiz_w - 328 &&
            x < s_wiz_x + s_wiz_w - 240)
        {
            wizard_back();
            return true;
        }
    }

    if (s_wizard_step == 0)
    {
        if (x >= s_wiz_x + 32 && x < s_wiz_x + 442 && y >= WIZ_CONTENT_Y + 26 &&
            y < WIZ_CONTENT_Y + 70)
        {
            s_wizard_focus = 0;
            draw_dialog();
            return true;
        }
        if (x >= s_wiz_x + 32 && x < s_wiz_x + 572 && y >= WIZ_CONTENT_Y + 108 &&
            y < WIZ_CONTENT_Y + 152)
        {
            s_wizard_focus = 1;
            draw_dialog();
            return true;
        }
        if (x >= s_wiz_x + 584 && x < s_wiz_x + 720 &&
            y >= WIZ_CONTENT_Y + 108 && y < WIZ_CONTENT_Y + 152)
        {
            wizard_browse_folder();
            return true;
        }
    }
    else if (s_wizard_step == 1)
    {
        for (int i = 0; i < 3; i++)
        {
            int card_x = s_wiz_x + 32 + i * 226;
            int card_y = WIZ_CONTENT_Y + 58;
            if (x >= card_x && x < card_x + 214 && y >= card_y &&
                y < card_y + 196)
            {
                s_wizard_template = i;
                draw_dialog();
                return true;
            }
        }
    }
    else if (s_wizard_step == 2)
    {
        int row_y = WIZ_CONTENT_Y + 66;
        if (x >= s_wiz_x + 32 && x < s_wiz_x + s_wiz_w - 32 && y >= row_y - 12 &&
            y < row_y + 46)
        {
            s_wizard_with_cmake = !s_wizard_with_cmake;
            draw_dialog();
            return true;
        }
        row_y += 82;
        if (x >= s_wiz_x + 32 && x < s_wiz_x + s_wiz_w - 32 && y >= row_y - 12 &&
            y < row_y + 46)
        {
            s_wizard_with_readme = !s_wizard_with_readme;
            draw_dialog();
            return true;
        }
        row_y += 82;
        if (x >= s_wiz_x + 32 && x < s_wiz_x + s_wiz_w - 32 && y >= row_y - 12 &&
            y < row_y + 46)
        {
            s_wizard_open_after = !s_wizard_open_after;
            draw_dialog();
            return true;
        }
    }
    return true;
}

static bool project_create_extended(const char *name, const char *path,
                                     int template_id, bool with_cmake,
                                     bool with_readme);

static void wizard_create_project(void)
{
    if (!wizard_validate_current())
    {
        draw_dialog();
        return;
    }
    if (!project_create_extended(s_wizard_name, s_wizard_path,
                                 s_wizard_template, s_wizard_with_cmake,
                                 s_wizard_with_readme))
    {
        if (!s_wizard_error[0])
            snprintf(s_wizard_error, sizeof(s_wizard_error),
                     "Could not create the project files");
        draw_dialog();
        return;
    }
    char root[PROJECT_PATH_MAX];
    if (s_wizard_path[strlen(s_wizard_path) - 1] == '/')
        snprintf(root, sizeof(root), "%s%s", s_wizard_path, s_wizard_name);
    else
        snprintf(root, sizeof(root), "%s/%s", s_wizard_path, s_wizard_name);
    if (s_home_mode)
    {
                                                                                
        snprintf(s_home_project_path, sizeof(s_home_project_path), "%s", root);
        s_home_mode = false;
        close_dialog();
        return;
    }
    close_dialog();
    if (s_wizard_open_after)
        project_open(root);
    else
        toast("Created %s", root);
    log_add("workspace/createProject", "Created %s at %s", s_wizard_name, root);
}

static void draw_cmd_palette(void)
{
    AromaFont *ui12 = font_ui(9);
    AromaFont *ui11 = font_ui(10);
    AromaFont *ui10 = font_ui(8);
    AromaFont *mono13 = font_mono(13);

                                                                          
    int bar_w = 520;
    int bar_h = 36;
    int bar_x = (win_w - bar_w) / 2;
    int bar_y = 70;
    int list_w = bar_w;
    int list_x = bar_x;
    int list_y = bar_y + bar_h;

                               
    fill(s_dialog_canvas, 0, 0, win_w, win_h, 0, 0x1A000000u);

                               
    fill(s_dialog_canvas, bar_x, bar_y, bar_w, bar_h, 6, M3_SURFACE_HIGH);
    fill(s_dialog_canvas, bar_x, bar_y, bar_w, 1, 0, M3_OUTLINE_VAR);

                                  
    icon_center(s_dialog_canvas, AROMA_ICON_SEARCH, bar_x + 20, bar_y + bar_h / 2,
                16, C_TEXT_DIM);
    text(s_dialog_canvas, ">", bar_x + 44, bar_y + 10, C_PRIMARY, mono13);

                     
    if (s_cmd_filter_len > 0)
        text_fit(s_dialog_canvas, s_cmd_filter, bar_x + 62, bar_y + 10,
                 bar_w - 120, C_TEXT, mono13);
    else
        text(s_dialog_canvas, "Type a command name or shortcut",
             bar_x + 62, bar_y + 10, C_TEXT_FAINT, mono13);
    int caret_x = bar_x + 62 + text_width(mono13, s_cmd_filter);
    if (caret_x > bar_x + bar_w - 40)
        caret_x = bar_x + bar_w - 40;
    fill(s_dialog_canvas, caret_x, bar_y + 7, 1, 22, 0, C_PRIMARY);

                                  
    int max_items = CMD_LIST_MAX;
    int list_h = max_items * CMD_ITEM_H;
    fill(s_dialog_canvas, list_x, list_y, list_w, list_h, 6, M3_SURFACE_HIGH);
    fill(s_dialog_canvas, list_x, list_y, list_w, 1, 0, M3_OUTLINE_VAR);

                      
    int visible = max_items;
    int yy = list_y;
    int shown = 0;
    for (int i = s_cmd_scroll; i < s_cmd_filtered_count && shown < visible;
         i++, shown++)
    {
        int idx = s_cmd_filtered[i];
        const CommandItem *cmd = &k_commands[idx];
        bool sel = (i == s_cmd_selected);
        int item_y = yy;
        if (sel)
            fill(s_dialog_canvas, list_x + 2, item_y, list_w - 4, CMD_ITEM_H,
                 4, C_SELECT);
        int icx = list_x + 26;
        int icsy = item_y + CMD_ITEM_H / 2;
        fill(s_dialog_canvas, icx - 6, icsy - 2, 4, 4, 2, C_TEXT_DIM);
        icon_center(s_dialog_canvas, cmd->icon, icx + 6, icsy, 14,
                    sel ? C_TEXT : C_TEXT_SOFT);
        text(s_dialog_canvas, cmd->label, icx + 30, item_y + 5,
             sel ? C_TEXT : C_TEXT_SOFT, ui11);
        int scw = text_width(ui10, cmd->shortcut);
        if (scw > 0)
            text(s_dialog_canvas, cmd->shortcut,
                 list_x + list_w - 32 - scw, item_y + 10, C_TEXT_FAINT, ui10);
        int dcx = icx + 30;
        int dw = list_x + list_w - 44 - scw - dcx;
        if (dw > 0)
            text_fit(s_dialog_canvas, cmd->desc, dcx, item_y + 20, dw,
                     C_TEXT_DIM, ui10);
        yy += CMD_ITEM_H;
    }

    if (s_cmd_filtered_count == 0)
    {
        text(s_dialog_canvas, "No commands match", list_x + 28, list_y + 10,
             C_TEXT_DIM, ui11);
    }
}

#define ANDROID_DEV_ROW_H 52
#define ANDROID_DEV_ROW_GAP 6
#define ANDROID_DEV_FOOTER_H 60

static int android_dev_list_top(void)
{
    return s_dialog_y + 116;
}

static int android_dev_list_bottom(void)
{
    return s_dialog_y + s_dialog_h - ANDROID_DEV_FOOTER_H - 10;
}

static int android_dev_visible_rows(void)
{
    int height = android_dev_list_bottom() - android_dev_list_top();
    int rows = (height + ANDROID_DEV_ROW_GAP) /
               (ANDROID_DEV_ROW_H + ANDROID_DEV_ROW_GAP);
    return rows < 1 ? 1 : rows;
}

static void android_dev_clamp_scroll(void)
{
    int maximum = s_android_device_count - android_dev_visible_rows();
    if (maximum < 0)
        maximum = 0;
    if (s_android_device_scroll > maximum)
        s_android_device_scroll = maximum;
    if (s_android_device_scroll < 0)
        s_android_device_scroll = 0;
}

static void android_dev_ensure_visible(int index)
{
    if (index < 0)
        return;
    int visible = android_dev_visible_rows();
    if (index < s_android_device_scroll)
        s_android_device_scroll = index;
    else if (index >= s_android_device_scroll + visible)
        s_android_device_scroll = index - visible + 1;
    android_dev_clamp_scroll();
}

static int android_dev_row_at(int x, int y)
{
    if (x < s_dialog_x + 24 || x >= s_dialog_x + s_dialog_w - 24)
        return -1;
    int top = android_dev_list_top();
    int bottom = android_dev_list_bottom();
    if (y < top || y >= bottom)
        return -1;
    int slot = (y - top) / (ANDROID_DEV_ROW_H + ANDROID_DEV_ROW_GAP);
    int offset = (y - top) % (ANDROID_DEV_ROW_H + ANDROID_DEV_ROW_GAP);
    if (offset >= ANDROID_DEV_ROW_H)
        return -1;
    int index = slot + s_android_device_scroll;
    return index >= 0 && index < s_android_device_count ? index : -1;
}

static bool android_dev_detail_value(const char *details, const char *key,
                                     char *out, size_t out_size)
{
    if (!details || !key || !out || out_size == 0)
        return false;
    out[0] = '\0';
    size_t key_length = strlen(key);
    const char *p = details;
    while ((p = strstr(p, key)) != NULL)
    {
        if ((p == details || isspace((unsigned char)p[-1])) &&
            p[key_length] == ':')
        {
            p += key_length + 1;
            const char *end = p;
            while (*end && !isspace((unsigned char)*end))
                end++;
            size_t size = (size_t)(end - p);
            if (size >= out_size)
                size = out_size - 1;
            memcpy(out, p, size);
            out[size] = '\0';
            return out[0] != '\0';
        }
        p += key_length;
    }
    return false;
}

static void android_dev_label(int index, char *name, size_t name_size,
                              char *kind, size_t kind_size)
{
    name[0] = '\0';
    kind[0] = '\0';
    if (index < 0 || index >= s_android_device_count)
        return;
    char model[128];
    if (android_dev_detail_value(s_android_device_details[index], "model",
                                 model, sizeof(model)))
    {
        for (char *p = model; *p; p++)
        {
            if (*p == '_')
                *p = ' ';
        }
        snprintf(name, name_size, "%s", model);
    }
    else if (strncmp(s_android_devices[index], "emulator-", 9) == 0)
    {
        snprintf(name, name_size, "Android Emulator");
    }
    else
    {
        snprintf(name, name_size, "Android device");
    }

    if (strncmp(s_android_devices[index], "emulator-", 9) == 0)
        snprintf(kind, kind_size, "Emulator");
    else if (strstr(s_android_device_details[index], "transport_id:"))
        snprintf(kind, kind_size, "USB device");
    else
        snprintf(kind, kind_size, "Connected");
}

static bool android_dev_refresh_hit(int x, int y)
{
    int footer_top = s_dialog_y + s_dialog_h - ANDROID_DEV_FOOTER_H;
    int setup_x = s_dialog_x + s_dialog_w - 24 - 150;
    int refresh_x = setup_x - 10 - 100;
    return x >= refresh_x && x < refresh_x + 100 && y >= footer_top + 15 &&
           y < footer_top + 45;
}

static bool android_dev_setup_hit(int x, int y)
{
    int footer_top = s_dialog_y + s_dialog_h - ANDROID_DEV_FOOTER_H;
    int setup_x = s_dialog_x + s_dialog_w - 24 - 150;
    return x >= setup_x && x < setup_x + 150 && y >= footer_top + 15 &&
           y < footer_top + 45;
}

static bool android_dev_close_hit(int x, int y)
{
    return x >= s_dialog_x + s_dialog_w - 48 && x < s_dialog_x + s_dialog_w - 12 &&
           y >= s_dialog_y + 14 && y < s_dialog_y + 50;
}

static void draw_android_device_manager(void)
{
    AromaFont *ui16 = font_ui(16);
    AromaFont *ui12 = font_ui(12);
    AromaFont *ui10 = font_ui(10);
    int x = s_dialog_x;
    int y = s_dialog_y;
    int w = s_dialog_w;
    int h = s_dialog_h;
    int content_x = x + 24;
    int content_w = w - 48;
    int list_top = android_dev_list_top();
    int list_bottom = android_dev_list_bottom();
    int footer_top = y + h - ANDROID_DEV_FOOTER_H;

    fill(s_dialog_canvas, 0, 0, win_w, win_h, 0, 0x66000000u);
    fill(s_dialog_canvas, x, y + 8, w, h, 28, 0x55000000u);
    fill(s_dialog_canvas, x, y, w, h, 28, M3_SURFACE_HIGH);
    line(s_dialog_canvas, x, y + 72, x + w, y + 72, C_BORDER, 1);
    line(s_dialog_canvas, x, footer_top, x + w, footer_top, C_BORDER, 1);

    fill(s_dialog_canvas, x + 22, y + 18, 40, 40, 20, M3_PRIMARY_CONTAINER);
    icon_center(s_dialog_canvas, AROMA_ICON_PHONE_ANDROID, x + 42, y + 38, 20,
                C_PRIMARY);
    text(s_dialog_canvas, "Android Device Manager", x + 74, y + 20, C_TEXT,
         ui16);
    text(s_dialog_canvas, "Select the device used for Build & Run", x + 74,
         y + 42, C_TEXT_DIM, ui10);

    if (s_android_device_hover == -2)
        fill(s_dialog_canvas, x + w - 46, y + 14, 28, 28, 8, C_HOVER);
    icon_center(s_dialog_canvas, AROMA_ICON_CLOSE, x + w - 32, y + 30, 15,
                C_TEXT_SOFT);

    text(s_dialog_canvas, "CONNECTED DEVICES", content_x, y + 86,
         C_TEXT_FAINT, ui10);

    android_dev_clamp_scroll();
    if (s_android_device_count == 0)
    {
        int center_x = x + w / 2;
        int center_y = list_top + (list_bottom - list_top) / 2;
        fill(s_dialog_canvas, center_x - 24, center_y - 24, 48, 48, 24,
             M3_PRIMARY_CONTAINER);
        icon_center(s_dialog_canvas, AROMA_ICON_PHONE_ANDROID, center_x,
                    center_y, 24, C_PRIMARY);
        text(s_dialog_canvas, "No devices found", center_x -
             text_width(ui12, "No devices found") / 2, center_y + 42,
             C_TEXT, ui12);
    
    }
    else
    {
        int visible = android_dev_visible_rows();
        for (int slot = 0; slot < visible; slot++)
        {
            int index = s_android_device_scroll + slot;
            if (index >= s_android_device_count)
                break;
            int row_y = list_top + slot * (ANDROID_DEV_ROW_H +
                                           ANDROID_DEV_ROW_GAP);
            bool selected = index == s_android_device_selected;
            bool hover = index == s_android_device_hover;
            uint32_t row_bg = selected ? M3_PRIMARY_CONTAINER
                                       : (hover ? M3_SURFACE_HIGHEST
                                                : M3_SURFACE_CONTAINER);
            fill(s_dialog_canvas, content_x, row_y, content_w,
                 ANDROID_DEV_ROW_H, 10, row_bg);
            if (selected)
                line(s_dialog_canvas, content_x, row_y, content_x + content_w,
                     row_y, C_PRIMARY, 1);

            fill(s_dialog_canvas, content_x + 12, row_y + 10, 32, 32, 16,
                 selected ? M3_PRIMARY : C_SELECT);
            icon_center(s_dialog_canvas, AROMA_ICON_PHONE_ANDROID,
                        content_x + 28, row_y + 26, 18,
                        selected ? M3_ON_PRIMARY : C_TEXT_DIM);

            char label[128];
            char kind[32];
            android_dev_label(index, label, sizeof(label), kind, sizeof(kind));
            int tw = text_width(ui12, label);
            text(s_dialog_canvas, label, content_x + 56, row_y + 10,
                 selected ? C_TEXT : C_TEXT_SOFT, ui12);
            text(s_dialog_canvas, kind, content_x + 56 + tw + 10, row_y + 10,
                 C_TEXT_DIM, ui10);

            if (selected)
            {
                icon_center(s_dialog_canvas, AROMA_ICON_CHECK,
                            content_x + content_w - 22, row_y + 26, 14,
                            M3_ON_PRIMARY);
            }
        }

        int visible_rows = android_dev_visible_rows();
        if (s_android_device_count > visible_rows)
        {
            int track_x = x + w - 10;
            int track_y = list_top + 2;
            int track_h = list_bottom - list_top - 4;
            int thumb_h = track_h * visible_rows / s_android_device_count;
            if (thumb_h < 28)
                thumb_h = 28;
            int maximum = s_android_device_count - visible_rows;
            int thumb_y = track_y;
            if (maximum > 0)
                thumb_y += (track_h - thumb_h) * s_android_device_scroll /
                           maximum;
            fill(s_dialog_canvas, track_x, track_y, 3, track_h, 2,
                 C_BORDER_SOFT);
            fill(s_dialog_canvas, track_x, thumb_y, 3, thumb_h, 2,
                 C_TEXT_DIM);
        }
    }

    int button_y = footer_top + 15;
    int setup_x = x + w - 24 - 150;
    int refresh_x = setup_x - 10 - 100;
    fill(s_dialog_canvas, refresh_x, button_y, 100, 30, 8,
         M3_SURFACE_CONTAINER);
    icon_center(s_dialog_canvas, AROMA_ICON_REFRESH, refresh_x + 22,
                button_y + 15, 14, C_TEXT_SOFT);
    text(s_dialog_canvas, "Refresh", refresh_x + 34, button_y + 8,
         C_TEXT_SOFT, ui10);

    fill(s_dialog_canvas, setup_x, button_y, 150, 30, 8, M3_PRIMARY);
    icon_center(s_dialog_canvas, AROMA_ICON_BUILD, setup_x + 24,
                button_y + 15, 14, M3_ON_PRIMARY);
    text(s_dialog_canvas, "Setup Android", setup_x + 38, button_y + 8,
         M3_ON_PRIMARY, ui10);
}

static void draw_dialog(void)
{
    if (!s_dialog_canvas)
        return;
    aroma_canvas_reset(s_dialog_canvas);
    if (!s_dialog_open)
        return;
    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
    {
        draw_properties_dialog();
        return;
    }
    if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
    {
        wizard_draw();
        return;
    }
    if (s_dialog_mode == IDE_DIALOG_COMMAND_PALETTE)
    {
        draw_cmd_palette();
        return;
    }
    if (s_dialog_mode == IDE_DIALOG_ANDROID_DEVICES)
    {
        draw_android_device_manager();
        return;
    }

    AromaFont *ui12 = font_ui(12);
    AromaFont *ui10 = font_ui(10);
    AromaFont *mono13 = font_mono(13);
    AromaFont *mono11 = font_mono(11);

    fill(s_dialog_canvas, 0, 0, win_w, win_h, 0, 0x66000000u);
    fill(s_dialog_canvas, s_dialog_x, s_dialog_y + 6, s_dialog_w, s_dialog_h, 28,
         0x44000000u);
    fill(s_dialog_canvas, s_dialog_x, s_dialog_y, s_dialog_w, s_dialog_h, 28,
         M3_SURFACE_HIGH);

    const char *title = "NEW FILE";
    const char *hint = "Create a file inside the open project";
    const char *placeholder = "src/new_file.c";
    if (s_dialog_mode == IDE_DIALOG_OPEN_FILE)
    {
        title = "OPEN FILE";
        hint = "Enter a project-relative path or a file name";
        placeholder = "src/main.c";
    }
    else if (s_dialog_mode == IDE_DIALOG_SEARCH)
    {
        title = "SEARCH WORKSPACE";
        hint = "Find text in every file of the project";
        placeholder = "language server";
    }
    else if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
    {
        title = "NEW PROJECT";
        hint = "Enter project name and optional path (name|path)";
        placeholder = "my_app|/home/user/projects";
    }
    else if (s_dialog_mode == IDE_DIALOG_OPEN_PROJECT)
    {
        title = "OPEN PROJECT";
        hint = "Enter path to a folder containing aroma.json";
        placeholder = "/home/user/projects/my_app";
    }
    else if (s_dialog_mode == IDE_DIALOG_COMMAND_PALETTE)
    {
        title = "COMMAND PALETTE";
        hint = "Type a command name to execute";
        placeholder = "> build";
    }

    text(s_dialog_canvas, title, s_dialog_x + 28, s_dialog_y + 22, C_TEXT, ui12);
    text(s_dialog_canvas, hint, s_dialog_x + 28, s_dialog_y + 44, C_TEXT_DIM, ui10);

    fill(s_dialog_canvas, s_dialog_x + 28, s_dialog_y + 76, s_dialog_w - 56, 42, 5,
         M3_SURFACE_CONTAINER);
    fill(s_dialog_canvas, s_dialog_x + 28, s_dialog_y + 76, s_dialog_w - 56, 1, 0,
         M3_OUTLINE_VAR);
    if (s_dialog_mode == IDE_DIALOG_SEARCH)
        icon_center(s_dialog_canvas, AROMA_ICON_SEARCH, s_dialog_x + 48,
                    s_dialog_y + 97, 15, C_TEXT_DIM);
    else if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
        icon_center(s_dialog_canvas, AROMA_ICON_ADD, s_dialog_x + 48,
                    s_dialog_y + 97, 15, C_TEXT_DIM);
    else if (s_dialog_mode == IDE_DIALOG_OPEN_PROJECT)
        icon_center(s_dialog_canvas, AROMA_ICON_FOLDER_OPEN, s_dialog_x + 48,
                    s_dialog_y + 97, 15, C_TEXT_DIM);
    else if (s_dialog_mode == IDE_DIALOG_COMMAND_PALETTE)
        icon_center(s_dialog_canvas, AROMA_ICON_KEYBOARD, s_dialog_x + 48,
                    s_dialog_y + 97, 15, C_TEXT_DIM);
    else
        icon_center(s_dialog_canvas, AROMA_ICON_DESCRIPTION, s_dialog_x + 48,
                    s_dialog_y + 97, 15, C_TEXT_DIM);
    if (s_dialog_text[0])
        text_fit(s_dialog_canvas, s_dialog_text, s_dialog_x + 66, s_dialog_y + 89,
                 s_dialog_w - 112, C_TEXT, mono13);
    else
        text(s_dialog_canvas, placeholder, s_dialog_x + 66, s_dialog_y + 89,
             C_TEXT_FAINT, mono13);
    int caret_x = s_dialog_x + 66 +
                  text_width(mono13, s_dialog_text[0] ? s_dialog_text : "");
    if (caret_x > s_dialog_x + s_dialog_w - 52)
        caret_x = s_dialog_x + s_dialog_w - 52;
    fill(s_dialog_canvas, caret_x, s_dialog_y + 88, 1, 21, 0, C_PRIMARY);

    if (s_dialog_error[0])
        text_fit(s_dialog_canvas, s_dialog_error, s_dialog_x + 28, s_dialog_y + 128,
                 s_dialog_w - 56, C_RED, ui10);
    else
        text(s_dialog_canvas, "Project-relative paths keep the workspace tidy",
             s_dialog_x + 28, s_dialog_y + 128, C_TEXT_FAINT, ui10);

    fill(s_dialog_canvas, s_dialog_x + 1, s_dialog_y + s_dialog_h - 37,
         s_dialog_w - 2, 1, 0, C_BORDER);
    text(s_dialog_canvas, "Enter", s_dialog_x + 28, s_dialog_y + s_dialog_h - 24,
         C_TEXT_SOFT, mono11);
    text(s_dialog_canvas, "confirm", s_dialog_x + 64, s_dialog_y + s_dialog_h - 24,
         C_TEXT_FAINT, ui10);
    text(s_dialog_canvas, "Esc", s_dialog_x + 124, s_dialog_y + s_dialog_h - 24,
         C_TEXT_SOFT, mono11);
    text(s_dialog_canvas, "cancel", s_dialog_x + 148, s_dialog_y + s_dialog_h - 24,
         C_TEXT_FAINT, ui10);
    text(s_dialog_canvas, "Ctrl+S saves the active file", s_dialog_x + 312,
         s_dialog_y + s_dialog_h - 24, C_TEXT_FAINT, ui10);

              
}

static bool cmd_palette_on_key(uint32_t key)
{
    bool esc = (key == 27 || key == 0xFF1B);
    bool enter = (key == 13 || key == 10 || key == 0xFF0D || key == 0xFF8D);
    bool up = (key == 0xFF52);
    bool down = (key == 0xFF54);
    bool page_up = (key == 0xFF55);
    bool page_down = (key == 0xFF56);
    bool home = (key == 0xFF50);
    bool end = (key == 0xFF57);
    bool bs = (key == 8 || key == 0xFF08 || key == 0x7F);

    if (esc)
    {
        close_dialog();
        return true;
    }
    if (enter)
    {
        cmd_palette_execute_selected();
        return true;
    }
    if (up)
    {
        if (s_cmd_selected > 0)
            s_cmd_selected--;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (down)
    {
        if (s_cmd_selected < s_cmd_filtered_count - 1)
            s_cmd_selected++;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (page_up)
    {
        int visible = CMD_LIST_MAX;
        if (visible < 1)
            visible = 1;
        s_cmd_selected -= visible;
        if (s_cmd_selected < 0)
            s_cmd_selected = 0;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (page_down)
    {
        int visible = CMD_LIST_MAX;
        if (visible < 1)
            visible = 1;
        s_cmd_selected += visible;
        if (s_cmd_selected >= s_cmd_filtered_count)
            s_cmd_selected = s_cmd_filtered_count > 0 ? s_cmd_filtered_count - 1 : 0;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (home)
    {
        s_cmd_selected = 0;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (end)
    {
        s_cmd_selected = s_cmd_filtered_count > 0 ? s_cmd_filtered_count - 1 : 0;
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (bs)
    {
        if (s_cmd_filter_len > 0)
            s_cmd_filter[--s_cmd_filter_len] = '\0';
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    if (key >= 32 && key <= 126 && s_cmd_filter_len + 1 < sizeof(s_cmd_filter))
    {
        s_cmd_filter[s_cmd_filter_len++] = (char)key;
        s_cmd_filter[s_cmd_filter_len] = '\0';
        cmd_palette_filter();
        draw_dialog();
        return true;
    }
    return true;
}

static bool dialog_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_dialog_open || s_dialog_mode != IDE_DIALOG_ANDROID_DEVICES ||
        !event || event->event_type != EVENT_TYPE_MOUSE_MOVE)
        return true;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hover = android_dev_row_at(x, y);
    if (android_dev_close_hit(x, y))
        hover = -2;
    if (hover != s_android_device_hover)
    {
        s_android_device_hover = hover;
        draw_dialog();
    }
    return true;
}

static bool dialog_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_dialog_open && s_dialog_mode == IDE_DIALOG_ANDROID_DEVICES &&
        s_android_device_hover != -1)
    {
        s_android_device_hover = -1;
        draw_dialog();
    }
    return true;
}

static bool dialog_on_key(AromaEvent *event, void *user_data)
{    (void)user_data;
    if (!s_dialog_open || !event || event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;
    if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
        return wizard_on_key(event);

    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
        return props_on_key(event->data.key.key_code);

    if (s_dialog_mode == IDE_DIALOG_COMMAND_PALETTE)
        return cmd_palette_on_key(event->data.key.key_code);
    if (s_dialog_mode == IDE_DIALOG_ANDROID_DEVICES)
    {
        uint32_t key = event->data.key.key_code;
        if (key == 27 || key == 0xFF1B)
        {
            close_dialog();
            return true;
        }
        if (s_android_device_count > 0 &&
            (key == 0xFF52 || key == 0xFF54))
        {
            int next = s_android_device_selected;
            if (next < 0)
                next = 0;
            else
                next += key == 0xFF52 ? -1 : 1;
            if (next < 0)
                next = s_android_device_count - 1;
            if (next >= s_android_device_count)
                next = 0;
            project_android_select_device(next);
            android_dev_ensure_visible(next);
            draw_dialog();
            return true;
        }
        if (key == 13 || key == 10 || key == 0xFF0D)
        {
            if (s_android_device_selected >= 0)
                project_android_select_device(s_android_device_selected);
            draw_dialog();
            return true;
        }
        if (key == 0x20) /* Space refreshes the device list. */
        {
            project_android_refresh_devices(true);
            android_dev_clamp_scroll();
            draw_dialog();
            return true;
        }
        return true;
    }

    uint32_t key = event->data.key.key_code;
    if (key == 27 || key == 0xFF1B)
    {
        close_dialog();
        return true;
    }
    if (key == 13 || key == 10 || key == 0xFF0D)
    {
        dialog_submit();
        return true;
    }
    if (key == 8 || key == 0xFF08 || key == 0x007F)
    {
        if (s_dialog_length > 0)
            s_dialog_text[--s_dialog_length] = '\0';
        s_dialog_error[0] = '\0';
        draw_dialog();
        return true;
    }
    if (key >= 32 && key <= 126 && s_dialog_length + 1 < sizeof(s_dialog_text))
    {
        s_dialog_text[s_dialog_length++] = (char)key;
        s_dialog_text[s_dialog_length] = '\0';
        s_dialog_error[0] = '\0';
        draw_dialog();
        return true;
    }
    return true;
}

static bool dialog_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_dialog_open)
        return false;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (s_dialog_mode == IDE_DIALOG_NEW_PROJECT)
    {
        if (x < s_wiz_x || x >= s_wiz_x + s_wiz_w || y < s_wiz_y ||
            y >= s_wiz_y + s_wiz_h)
            close_dialog();
        else
            wizard_on_click(x, y);
        return true;
    }
    if (s_dialog_mode == IDE_DIALOG_PROPERTIES)
        return props_on_click(x, y);
    if (s_dialog_mode == IDE_DIALOG_ANDROID_DEVICES)
    {
        if (android_dev_close_hit(x, y))
        {
            close_dialog();
            return true;
        }
        if (x < s_dialog_x || x >= s_dialog_x + s_dialog_w ||
            y < s_dialog_y || y >= s_dialog_y + s_dialog_h)
        {
            close_dialog();
            return true;
        }
        int device_index = android_dev_row_at(x, y);
        if (device_index >= 0)
        {
            project_android_select_device(device_index);
            draw_dialog();
            return true;
        }
        if (android_dev_refresh_hit(x, y))
        {
            project_android_refresh_devices(true);
            android_dev_clamp_scroll();
            draw_dialog();
            return true;
        }
        if (android_dev_setup_hit(x, y))
        {
            close_dialog();
            project_android_setup();
            return true;
        }
        return true;
    }
    if (x < s_dialog_x || x >= s_dialog_x + s_dialog_w || y < s_dialog_y ||
        y >= s_dialog_y + s_dialog_h)
        close_dialog();
    else
    {
                                                   
        int btn_w2 = 100, btn_h2 = 28, btn_x2 = s_dialog_x + s_dialog_w - 136,
            btn_y2 = s_dialog_y + s_dialog_h - 36;
        if (x >= btn_x2 && x < btn_x2 + btn_w2 && y >= btn_y2 &&
            y < btn_y2 + btn_h2)
            dialog_submit();
                                      
        else if (x >= btn_x2 + btn_w2 + 8 && x < btn_x2 + 2 * btn_w2 + 8 &&
                 y >= btn_y2 && y < btn_y2 + btn_h2)
            close_dialog();
    }
    return true;
}

static void draw_menu(void)
{
    if (!s_menu_canvas)
        return;
    aroma_canvas_reset(s_menu_canvas);

    bool hiding = s_menu_hiding;
    if (!s_menu_open && !s_window_menu_open && !hiding)
        return;

    float reveal = s_menu_reveal;
    if (reveal < 0.0f)
        reveal = 0.0f;
    if (reveal > 1.0f)
        reveal = 1.0f;

    bool window_mode = s_window_menu_open ||
                       (hiding && s_menu_last_window_menu);
    if (window_mode)
    {
        int x = s_window_menu_x;
        int y = TITLE_H;
        int w = 170;
        int full_h = 4 * MENU_ITEM_H + 12;
        int h = (int)((float)full_h * reveal + 0.5f);
        if (h <= 0)
            return;
        fill(s_menu_canvas, x, y, w, h, 6, M3_SURFACE_HIGH);
        for (int i = 0; i < 3; i++)
        {
            int item_y = y + 6 + i * MENU_ITEM_H;
            if (item_y + MENU_ITEM_H > y + h)
                break;
            text(s_menu_canvas,
                 i == 0 ? "Minimize"
                        : (i == 1 ? "Maximize / Restore" : "Close"),
                 x + 12, item_y, C_TEXT_SOFT, font_ui(11));
        }
        return;
    }

    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    const MenuItem *items = k_menu_items[s_menu_index];
    int count = k_menu_item_counts[s_menu_index];
    int panel_x = menu_panel_x();
    int full_height = menu_item_y(count - 1) + MENU_ITEM_H + 14;
    int panel_height = (int)((float)full_height * reveal + 0.5f);
    s_menu_panel_x = panel_x;
    if (panel_height <= 0)
        return;

    fill(s_menu_canvas, panel_x + 4, MENU_panel_y + 6, MENU_panel_w,
         panel_height, 7, 0x66000000u);
    fill(s_menu_canvas, panel_x, MENU_panel_y, MENU_panel_w, panel_height, 6,
         C_BORDER_SOFT);
    if (panel_height > 2)
        fill(s_menu_canvas, panel_x + 1, MENU_panel_y + 1, MENU_panel_w - 2,
             panel_height - 2, 5, M3_SURFACE_HIGH);

    int panel_bottom = MENU_panel_y + panel_height;
    for (int i = 0; i < count; i++)
    {
        int y = menu_item_y(i);
        if (y + MENU_ITEM_H > panel_bottom)
            break;
        if (s_menu_hover == i)
            fill(s_menu_canvas, panel_x + 5, y, MENU_panel_w - 10, MENU_ITEM_H,
                 4, C_SELECT);
        text(s_menu_canvas, items[i].label, panel_x + 16, y + 6,
             s_menu_hover == i ? C_TEXT : C_TEXT_SOFT, ui11);
        int shortcut_width = text_width(ui10, items[i].shortcut);
        text(s_menu_canvas, items[i].shortcut,
             panel_x + MENU_panel_w - 14 - shortcut_width, y + 7, C_TEXT_FAINT,
             ui10);
    }
    if (count > 4)
    {
        int sep_y = menu_item_y(4) - 4;
        if (sep_y < panel_bottom)
            line(s_menu_canvas, panel_x + 10, sep_y,
                 panel_x + MENU_panel_w - 10, sep_y, C_BORDER, 1);
    }
}

static void menu_finish_hide(void)
{
    if (s_menu_open || s_window_menu_open)
        return;
    s_menu_hiding = false;
    s_menu_reveal = 0.0f;
    if (s_menu_canvas)
    {
        aroma_node_set_hidden(s_menu_canvas, true);
        aroma_canvas_reset(s_menu_canvas);
        aroma_node_invalidate(s_menu_canvas);
    }
    draw_title();
}

static void menu_animation_step(AromaNode *target, float value,
                                void *user_data)
{
    (void)target;
    (void)user_data;
    s_menu_reveal = value;
    if (s_menu_reveal < 0.0f)
        s_menu_reveal = 0.0f;
    if (s_menu_reveal > 1.0f)
        s_menu_reveal = 1.0f;
    draw_menu();
    aroma_ui_request_redraw(aroma_ui_get_window_at(0));
}

static void menu_animation_complete(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (s_menu_hiding)
    {
        menu_finish_hide();
        return;
    }
    s_menu_reveal = 1.0f;
    draw_menu();
}

static void menu_animate_show(void)
{
    if (!s_menu_canvas)
        return;
    s_menu_hiding = false;
    s_menu_last_window_menu = s_window_menu_open;
    aroma_node_set_hidden(s_menu_canvas, false);
    if (s_menu_reveal >= 0.999f)
    {
        aroma_animation_stop(s_menu_canvas);
        s_menu_reveal = 1.0f;
        draw_menu();
        return;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        s_menu_canvas, s_menu_reveal, 1.0f, 130u,
        menu_animation_step, NULL);
    if (!anim)
    {
        s_menu_reveal = 1.0f;
        draw_menu();
        return;
    }
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_animation_set_on_complete(anim, menu_animation_complete);
}

static void menu_animate_hide(void)
{
    if (!s_menu_canvas)
    {
        menu_finish_hide();
        return;
    }
    if (s_menu_hiding)
        return;
    s_menu_hiding = true;
    if (s_menu_reveal <= 0.001f)
    {
        menu_finish_hide();
        return;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        s_menu_canvas, s_menu_reveal, 0.0f, 90u,
        menu_animation_step, NULL);
    if (!anim)
    {
        menu_finish_hide();
        return;
    }
    aroma_animation_set_easing(anim, AROMA_EASE_IN_QUAD);
    aroma_animation_set_on_complete(anim, menu_animation_complete);
}

static uint32_t ide_scale_alpha(uint32_t color, float alpha)
{
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint32_t base = (color >> 24) & 0xFFu;
    uint32_t scaled = (uint32_t)((float)base * alpha + 0.5f);
    if (scaled == 0)
        scaled = 1; /* the renderer treats alpha 0 as opaque */
    return (color & 0x00FFFFFFu) | (scaled << 24);
}

static uint32_t ide_mix_rgb(uint32_t from, uint32_t to, float t)
{
    if (t <= 0.0f)
        return from | 0xFF000000u;
    if (t >= 1.0f)
        return to | 0xFF000000u;
    uint8_t r1, g1, b1, r2, g2, b2;
    aroma_color_extract_rgb(from, &r1, &g1, &b1);
    aroma_color_extract_rgb(to, &r2, &g2, &b2);
    uint8_t r = (uint8_t)((float)r1 + ((float)r2 - (float)r1) * t);
    uint8_t g = (uint8_t)((float)g1 + ((float)g2 - (float)g1) * t);
    uint8_t b = (uint8_t)((float)b1 + ((float)b2 - (float)b1) * t);
    return aroma_color_rgba(r, g, b, 0xFF);
}

static void draw_toast(void)
{
    if (!s_toast_canvas)
        return;
    aroma_canvas_reset(s_toast_canvas);
    if (!s_toast_visible)
        return;

    float reveal = s_toast_reveal;
    if (reveal < 0.0f)
        reveal = 0.0f;
    if (reveal > 1.0f)
        reveal = 1.0f;

    int yoff = (int)((1.0f - reveal) * 12.0f + 0.5f);
    int x = s_toast_x;
    int y = s_toast_y + yoff;
    uint32_t surface = ide_scale_alpha(M3_INVERSE_SURFACE, reveal);
    uint32_t foreground = ide_mix_rgb(M3_INVERSE_ON_SURFACE,
                                      M3_INVERSE_SURFACE, 1.0f - reveal);
    foreground = ide_scale_alpha(foreground, reveal);
    uint32_t shadow = ide_scale_alpha(0x44000000u, reveal);

    AromaFont *ui12 = font_ui(12);
    fill(s_toast_canvas, x, y + 3, s_toast_w, s_toast_h, 8, shadow);
    fill(s_toast_canvas, x, y, s_toast_w, s_toast_h, 8, surface);
    icon_center(s_toast_canvas, AROMA_ICON_CHECK_CIRCLE, x + 26,
                y + s_toast_h / 2, 18, foreground);
    text_fit(s_toast_canvas, s_toast, x + 44, y + 22,
             s_toast_w - 60, foreground, ui12);
}

static void toast_finish_hide(void)
{
    s_toast_hiding = false;
    s_toast_visible = false;
    s_toast_until = 0;
    s_toast_reveal = 0.0f;
    if (s_toast_canvas)
    {
        aroma_node_set_hidden(s_toast_canvas, true);
        aroma_node_invalidate(s_toast_canvas);
    }
}

static void toast_animation_step(AromaNode *target, float value,
                                 void *user_data)
{
    (void)target;
    (void)user_data;
    if (!s_toast_canvas)
        return;
    s_toast_reveal = value;
    if (s_toast_reveal < 0.0f)
        s_toast_reveal = 0.0f;
    if (s_toast_reveal > 1.0f)
        s_toast_reveal = 1.0f;
    draw_toast();
    aroma_ui_request_redraw(aroma_ui_get_window_at(0));
}

static void toast_animation_complete(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (s_toast_hiding)
    {
        toast_finish_hide();
        return;
    }
    s_toast_reveal = 1.0f;
    draw_toast();
}

static void toast_animate_show(void)
{
    if (!s_toast_canvas)
        return;
    s_toast_hiding = false;
    aroma_node_set_hidden(s_toast_canvas, false);
    if (s_toast_reveal >= 0.999f)
    {
        aroma_animation_stop(s_toast_canvas);
        s_toast_reveal = 1.0f;
        draw_toast();
        return;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        s_toast_canvas, s_toast_reveal, 1.0f, 180u,
        toast_animation_step, NULL);
    if (!anim)
    {
        s_toast_reveal = 1.0f;
        draw_toast();
        return;
    }
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_animation_set_on_complete(anim, toast_animation_complete);
}

static void toast_animate_hide(void)
{
    if (!s_toast_canvas)
    {
        toast_finish_hide();
        return;
    }
    if (s_toast_hiding)
        return;
    s_toast_hiding = true;
    if (s_toast_reveal <= 0.001f)
    {
        toast_finish_hide();
        return;
    }

    AromaAnimation *anim = aroma_animation_start_custom(
        s_toast_canvas, s_toast_reveal, 0.0f, 150u,
        toast_animation_step, NULL);
    if (!anim)
    {
        toast_finish_hide();
        return;
    }
    aroma_animation_set_easing(anim, AROMA_EASE_IN_QUAD);
    aroma_animation_set_on_complete(anim, toast_animation_complete);
}