                                                                             

static void subscribe_node(AromaNode *node, AromaEventType type, AromaEventHandler handler)
{
    if (node)
        aroma_event_subscribe(node->node_id, type, handler, NULL, 20);
}

static void close_menu(void);

static int title_menu_hit(int x, int y)
{
    if (y < 0 || y >= TITLE_H)
        return -1;
    for (int i = 0; i < 8; i++)
    {
        int mx = title_menu_x(i);
        int mw = title_menu_text_width(i);
        if (x >= mx - 8 && x < mx + mw + 8)
            return i;
    }
    return -1;
}

static int title_button_hit(int x, int y)
{
    if (!title_actions_visible() || y < 6 || y >= 30)
        return -1;
    if (x >= title_build_x() && x < title_build_x() + TITLE_BUILD_W)
        return 0;
    if (x >= title_run_x() && x < title_run_x() + TITLE_RUN_W)
        return 1;
    return -1;
}

static bool title_search_hit(int x, int y)
{
    return title_search_visible() && x >= title_search_x() &&
           x < title_search_x() + TITLE_SEARCH_W && y >= 6 && y < 30;
}

static int title_ctrl_hit(int x, int y)
{
    if (y < 4 || y >= 32)
        return -1;
    for (int i = 0; i < 5; i++)
    {
        int cx = title_ctrl_center(i);
        if (x >= cx - 12 && x < cx + 12)
            return i;
    }
    return -1;
}

static bool title_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int menu_hover = title_menu_hit(x, y);
    int button_hover = title_button_hit(x, y);
    int ctrl_hover = title_ctrl_hit(x, y);
    bool search_hover = title_search_hit(x, y);

    if (menu_hover != s_title_hover || button_hover != s_btn_hover ||
        ctrl_hover != s_title_ctrl_hover ||
        search_hover != s_title_search_hover)
    {
        s_title_hover = menu_hover;
        s_btn_hover = button_hover;
        s_title_ctrl_hover = ctrl_hover;
        s_title_search_hover = search_hover;
        draw_title();
    }
    return true;
}

static bool title_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_title_hover != -1 || s_btn_hover != -1 ||
        s_title_ctrl_hover != -1 || s_title_search_hover)
    {
        s_title_hover = -1;
        s_btn_hover = -1;
        s_title_ctrl_hover = -1;
        s_title_search_hover = false;
        draw_title();
    }
    return true;
}

static bool title_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    debug_click("title", event);
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    uint64_t click_now = now_ms();
    if (preview_theme_on_click(x, y))
        return true;

    if (y >= 4 && y < TITLE_H && title_ctrl_hit(x, y) < 0 &&
        title_menu_hit(x, y) < 0 && !title_search_hit(x, y) &&
        title_button_hit(x, y) < 0)
    {
        if (click_now - s_title_last_click_ms < 350)
        {
            s_title_maximized = !s_title_maximized;
            aroma_ui_set_window_maximized(aroma_ui_get_window_at(0),
                                          s_title_maximized);
            s_title_last_click_ms = 0;
            return true;
        }
        s_title_last_click_ms = click_now;
        aroma_ui_begin_window_drag(aroma_ui_get_window_at(0));
        return true;
    }

    int menu_index = title_menu_hit(x, y);
    if (menu_index >= 0)
    {
        if (s_menu_open && s_menu_index == menu_index)
        {
            close_menu();
            return true;
        }
        s_menu_index = menu_index;
        s_menu_open = true;
        s_menu_hover = -1;
        if (s_menu_canvas)
            aroma_node_set_hidden(s_menu_canvas, false);
        draw_menu();
        draw_title();
        return true;
    }

    int button = title_button_hit(x, y);
    if (button == 0)
    {
        project_build();
        return true;
    }
    if (button == 1)
    {
        project_run();
        return true;
    }

    if (title_search_hit(x, y))
    {
        open_dialog(IDE_DIALOG_SEARCH, s_search_query);
        return true;
    }

    int ctrl = title_ctrl_hit(x, y);
    switch (ctrl)
    {
    case 0:
        preview_toggle();
        draw_title();
        break;
    case 1:
        if (s_settings_visible)
            settings_window_close();
        else
            settings_window_open();
        draw_title();
        break;
    case 2:
        set_panel_visible(!s_panel_visible);
        draw_title();
        break;
    case 3:
        open_android_device_manager();
        break;
    case 4:
        aroma_ui_request_exit();
        break;
    default:
        break;
    }
    return true;
}

static bool rail_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int hover = rail_hit(event->data.mouse.x, event->data.mouse.y);
    if (hover != s_rail_hover)
    {
        s_rail_hover = hover;
        draw_rail();
    }
    return true;
}

static bool rail_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_rail_hover != -1)
    {
        s_rail_hover = -1;
        draw_rail();
    }
    return true;
}

static bool rail_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    debug_click("rail", event);
    int hit = rail_hit(event->data.mouse.x, event->data.mouse.y);
    if (hit < 0)
        return true;
    if (hit == RAIL_SETTINGS_INDEX)
    {
        if (s_settings_visible)
            settings_window_close();
        else
            settings_window_open();
        return true;
    }
                                     
    s_sidebar_mode = hit;
    s_sidebar_row_hover = -1;
    s_script_hover = -1;
    s_setting_hover = -1;
    draw_rail();
    draw_sidebar();
    return true;
}

static void clear_sidebar_hovers(void)
{
    s_sidebar_action_hover = -1;
    s_sidebar_row_hover = -1;
    s_script_hover = -1;
    s_setting_hover = -1;
    s_setting_row_hover = -1;
}

static bool sidebar_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int sx = s_sidebar_detached ? 0 : SIDEBAR_X;
    int old_action = s_sidebar_action_hover;
    int old_row = s_sidebar_row_hover;
    int old_script = s_script_hover;
    int old_setting = s_setting_hover;
    bool old_detach = s_sidebar_detach_hover;
    s_sidebar_action_hover = -1;
    s_sidebar_row_hover = -1;
    s_script_hover = -1;
    s_setting_hover = -1;
    s_sidebar_detach_hover = false;

    if (x >= sx && x < sx + s_sidebar_w)
    {
        if (y >= 42 && y < 64)
        {
            s_sidebar_action_hover = sidebar_action_hit(x, y);
            s_sidebar_detach_hover = sidebar_detach_hit(x, y);
        }
        else if (y >= 96)
        {
            if (s_sidebar_mode == 0)
            {
                int row = (y - TREE_TOP) / TREE_ROW_HEIGHT + s_tree_scroll;
                if (row >= 0 && row < s_tree_row_count &&
                    y < TREE_TOP + tree_row_capacity() * TREE_ROW_HEIGHT)
                    s_sidebar_row_hover = row;
                for (int i = 0; i < 4; i++)
                {
                    if (s_script_y[i] >= 0 && y >= s_script_y[i] - 2 &&
                        y < s_script_y[i] + 20)
                        s_script_hover = i;
                }
            }
            else if (s_sidebar_mode == 1)
            {
                for (int i = 0; i < s_search_hit_count; i++)
                {
                    if (s_search_y[i] >= 0 && y >= s_search_y[i] - 4 &&
                        y < s_search_y[i] + 34)
                        s_sidebar_row_hover = i;
                }
            }
            else if (s_sidebar_mode == 2)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (s_change_y[i] >= 0 && y >= s_change_y[i] - 3 &&
                        y < s_change_y[i] + 21)
                        s_sidebar_row_hover = i;
                }
            }
            else if (s_sidebar_mode == 3)
            {
                for (int i = 0; i < 4; i++)
                {
                    if (s_run_y[i] >= 0 && y >= s_run_y[i] - 3 &&
                        y < s_run_y[i] + 21)
                        s_sidebar_row_hover = i;
                }
            }
            else if (s_sidebar_mode == 4)
            {
                int cat_w = s_sidebar_w / 2;
                int item_x = sx + cat_w + 20;
                int iy = TITLE_H + 80;
                int item_h = 46;
                int bottom = s_panel_visible ? panel_y : status_y;
                int visible = (bottom - iy) / item_h;
                if (visible < 1)
                    visible = 1;

                static const int cat_rows[SETTING_CAT_COUNT] = {7, 3, 3, 4, 1, 0};

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
                        s_setting_category = c;
                        draw_sidebar();
                        return true;
                    }
                    item_y += item_h;
                    entry_idx++;
                }
                s_setting_row_hover = -1;
                draw_sidebar();
            }
        }
    }

    if (old_action != s_sidebar_action_hover || old_row != s_sidebar_row_hover ||
        old_script != s_script_hover || old_setting != s_setting_hover ||
        old_detach != s_sidebar_detach_hover)
        draw_sidebar();
    return true;
}

static bool sidebar_on_scroll(AromaEvent *event, void *user_data)
{
    (void)user_data;
    float delta = event->data.mouse.scroll_y;
    if (delta == 0.0f)
        return true;
    if (s_sidebar_mode == 0)
    {
        int step = delta > 0 ? 1 : -1;
        for (float amount = delta < 0 ? -delta : delta; amount >= 0.5f; amount -= 1.0f)
            s_tree_scroll += step;
        tree_clamp_scroll();
        draw_sidebar();
    }
    else if (s_sidebar_mode == 1)
    {
        int step = delta > 0 ? 1 : -1;
        s_search_scroll += step;
        int bottom = s_panel_visible ? panel_y : status_y;
        int visible = (bottom - 158) / 38;
        int maximum = s_search_hit_count - visible;
        if (maximum < 0)
            maximum = 0;
        if (s_search_scroll > maximum)
            s_search_scroll = maximum;
        if (s_search_scroll < 0)
            s_search_scroll = 0;
        draw_sidebar();
    }
    else if (s_sidebar_mode == 4)
    {
        int step = delta > 0 ? 1 : -1;
        s_setting_scroll += step;
        int iy = TITLE_H + 80;
        int item_h = 46;
        int bottom = s_panel_visible ? panel_y : status_y;
        int visible = (bottom - iy) / item_h;
        if (visible < 1)
            visible = 1;

        static const int cat_rows[SETTING_CAT_COUNT] = {7, 3, 3, 4, 1, 0};
        int total = 0;
        for (int c = 0; c < SETTING_CAT_COUNT; c++)
            total += 1 + cat_rows[c];                     

        int max_scroll = total - visible;
        if (max_scroll < 0)
            max_scroll = 0;
        if (s_setting_scroll > max_scroll)
            s_setting_scroll = max_scroll;
        if (s_setting_scroll < 0)
            s_setting_scroll = 0;
        draw_sidebar();
    }
    return true;
}

static bool sidebar_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_sidebar_action_hover != -1 || s_sidebar_row_hover != -1 ||
        s_script_hover != -1 || s_setting_hover != -1)
    {
        clear_sidebar_hovers();
        draw_sidebar();
    }
    return true;
}

static void project_init_state(void)
{
    memset(&s_project, 0, sizeof(s_project));
    s_android_device_count = 0;
    s_android_device_selected = -1;
    s_android_adb_path[0] = '\0';
    snprintf(s_project.root, sizeof(s_project.root), "%s", ide_workspace_root);
    snprintf(s_project.name, sizeof(s_project.name), "%s",
             ide_workspace_project_name());
    snprintf(s_project.build_cmd, sizeof(s_project.build_cmd),
             "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j");
    snprintf(s_project.run_cmd, sizeof(s_project.run_cmd),
             "./build/%s", ide_workspace_project_name());
    s_project.is_building = false;
    s_project.is_running = false;
    s_project.is_cleaning = false;
    s_project.output_mode = 0;
    s_project.has_build_output = false;
    s_project.has_run_output = false;
    s_project.has_clean_output = false;
    s_project.build_output[0] = '\0';
    s_project.run_output[0] = '\0';
    s_project.clean_output[0] = '\0';
}

static bool find_file_upwards(const char *start, const char *relative,
                              char *out, size_t out_size)
{
    char dir[PATH_MAX];
    if (!start || !start[0])
        return false;
    snprintf(dir, sizeof(dir), "%s", start);
    for (;;)
    {
        snprintf(out, out_size, "%s/%s", dir, relative);
        if (access(out, R_OK) == 0)
            return true;
        if (strcmp(dir, "/") == 0)
            break;
        char *slash = strrchr(dir, '/');
        if (!slash)
            break;
        if (slash == dir)
            dir[1] = '\0';
        else
            *slash = '\0';
    }
    return false;
}

static const char *ide_helper_path(void)
{
    static char path[PATH_MAX];
    static bool searched;
    if (searched)
        return path[0] ? path : NULL;
    searched = true;

    const char *env = getenv("AROMA_IDE_HELPER");
    if (env && access(env, R_OK) == 0)
    {
        snprintf(path, sizeof(path), "%s", env);
        return path;
    }

    char exe[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (exe_len > 0)
    {
        exe[exe_len] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash)
            *slash = '\0';
        if (find_file_upwards(exe, "tools/aroma_ide_helper.py", path,
                              sizeof(path)))
            return path;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
    {
        if (find_file_upwards(cwd, "tools/aroma_ide_helper.py", path,
                              sizeof(path)))
            return path;
        if (find_file_upwards(cwd, "examples/19_ide/tools/aroma_ide_helper.py",
                              path, sizeof(path)))
            return path;
    }

    snprintf(path, sizeof(path), "%s/examples/19_ide/tools/aroma_ide_helper.py",
             AROMA_IDE_SDK_ROOT);
    if (access(path, R_OK) == 0)
        return path;
    path[0] = '\0';
    return NULL;
}

static const char *aroma_cli_path(void)
{
    static char path[PATH_MAX];
    static bool searched;
    if (searched)
        return path[0] ? path : NULL;
    searched = true;

    const char *env = getenv("AROMA_CLI");
    if (env && access(env, R_OK) == 0)
    {
        snprintf(path, sizeof(path), "%s", env);
        return path;
    }

    char exe[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (exe_len > 0)
    {
        exe[exe_len] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash)
            *slash = '\0';
        if (find_file_upwards(exe, "tools/cli/aroma.py", path, sizeof(path)))
            return path;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) &&
        find_file_upwards(cwd, "tools/cli/aroma.py", path, sizeof(path)))
        return path;

    snprintf(path, sizeof(path), "%s/tools/cli/aroma.py", AROMA_IDE_SDK_ROOT);
    if (access(path, R_OK) == 0)
        return path;
    path[0] = '\0';
    return NULL;
}

static bool command_available(const char *name)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static void trim_line(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = '\0';
    char *start = line;
    while (*start == ' ' || *start == '\t')
        start++;
    if (start != line)
        memmove(line, start, strlen(start) + 1);
}

static void android_device_add(const char *serial)
{
    if (!serial || !serial[0])
        return;
    for (int i = 0; i < s_android_device_count; i++)
    {
        if (strcmp(s_android_devices[i], serial) == 0)
            return;
    }
    if (s_android_device_count >= PROJECT_ANDROID_MAX_DEVICES)
        return;
    snprintf(s_android_devices[s_android_device_count],
             sizeof(s_android_devices[s_android_device_count]), "%s", serial);
    s_android_device_count++;
}

static void project_android_refresh_devices(bool notify)
{
    char previous[PROJECT_ANDROID_SERIAL_MAX];
    snprintf(previous, sizeof(previous), "%s", s_project.android_serial);
    char configured[PROJECT_ANDROID_SERIAL_MAX] = "";
    s_android_device_count = 0;
    s_android_adb_path[0] = '\0';

    const char *helper = ide_helper_path();
    char cmd[PROJECT_CMD_MAX];
    if (helper)
        snprintf(cmd, sizeof(cmd),
                 "python3 \"%s\" adb-devices \"%s\" 2>/dev/null", helper,
                 s_project.root);
    else
        snprintf(cmd, sizeof(cmd), "adb devices 2>/dev/null");

    FILE *fp = popen(cmd, "r");
    if (fp)
    {
        char line[512];
        while (fgets(line, sizeof(line), fp))
        {
            trim_line(line);
            if (!line[0])
                continue;
            if (strncmp(line, "ADB:", 4) == 0)
            {
                snprintf(s_android_adb_path, sizeof(s_android_adb_path), "%s",
                         line + 4);
                continue;
            }
            if (strncmp(line, "SELECTED:", 9) == 0)
            {
                snprintf(configured, sizeof(configured), "%s", line + 9);
                continue;
            }
            if (strncmp(line, "DEVICE:", 7) == 0)
            {
                android_device_add(line + 7);
                continue;
            }
            if (strstr(line, "List of devices") || strstr(line, "daemon"))
                continue;
            char *tab = strchr(line, '\t');
            if (tab)
            {
                *tab = '\0';
                if (strcmp(tab + 1, "device") == 0)
                    android_device_add(line);
            }
            else
            {
                android_device_add(line);
            }
        }
        pclose(fp);
    }

    s_android_device_selected = -1;
    for (int i = 0; i < s_android_device_count; i++)
    {
        if (previous[0] && strcmp(s_android_devices[i], previous) == 0)
        {
            s_android_device_selected = i;
            break;
        }
    }
    if (s_android_device_selected < 0 && configured[0])
    {
        for (int i = 0; i < s_android_device_count; i++)
        {
            if (strcmp(s_android_devices[i], configured) == 0)
            {
                s_android_device_selected = i;
                break;
            }
        }
    }
    if (s_android_device_selected < 0 && s_android_device_count > 0)
        s_android_device_selected = 0;

    if (s_android_device_selected >= 0)
        snprintf(s_project.android_serial, sizeof(s_project.android_serial),
                 "%s", s_android_devices[s_android_device_selected]);
    else
        s_project.android_serial[0] = '\0';

    if (notify)
    {
        if (s_android_device_count > 0)
            toast("Android: %d device%s (%s)", s_android_device_count,
                  s_android_device_count == 1 ? "" : "s",
                  s_project.android_serial);
        else
            toast("No Android devices found");
    }
    s_project_ui_dirty = true;
    draw_status();
    if (s_panel_visible && s_panel_tab == 1)
        draw_panel();
}

static void project_android_select_device(int index)
{
    if (index < 0 || index >= s_android_device_count)
        return;
    s_android_device_selected = index;
    snprintf(s_project.android_serial, sizeof(s_project.android_serial), "%s",
             s_android_devices[index]);
    const char *helper = ide_helper_path();
    if (helper)
    {
        char cmd[PROJECT_CMD_MAX];
        snprintf(cmd, sizeof(cmd), "python3 \"%s\" set-device \"%s\" \"%s\" "
                                   " >/dev/null 2>&1",
                 helper, s_project.root, s_project.android_serial);
        (void)system(cmd);
    }
    toast("Android device: %s", s_project.android_serial);
    s_project_ui_dirty = true;
    draw_status();
    if (s_panel_visible && s_panel_tab == 1)
        draw_panel();
}

static void open_android_device_manager(void)
{
    project_android_refresh_devices(false);
    open_dialog(IDE_DIALOG_ANDROID_DEVICES, "");
}

static void project_android_next_device(void)
{
    if (s_project.target != 1)
        project_set_target(1);
    if (s_android_device_count == 0)
        project_android_refresh_devices(false);
    if (s_android_device_count == 0)
    {
        toast("No Android devices connected");
        return;
    }
    int next = s_android_device_selected + 1;
    if (next < 0 || next >= s_android_device_count)
        next = 0;
    project_android_select_device(next);
}

static void project_set_target(int target)
{
    target = target ? 1 : 0;
    s_project.target = target;
    if (target == 1)
    {
        project_android_refresh_devices(false);
        if (s_android_device_selected >= 0)
            toast("Target: Android (%s)", s_project.android_serial);
        else
            toast("Target: Android (no device connected)");
    }
    else
    {
        toast("Target: Linux");
    }
    log_add("workspace/executeCommand", "Target: %s",
            target == 1 ? "Android" : "Linux");
    s_project_ui_dirty = true;
    draw_status();
    if (s_panel_visible && s_panel_tab == 1)
        draw_panel();
}

static void project_android_setup(void)
{
    project_set_target(1);
    project_build();
}

static void project_build_command(char *out, size_t out_size)
{
    out[0] = '\0';
    if (s_project.target == 1)
    {
        const char *helper = ide_helper_path();
        const char *cli = aroma_cli_path();
        if (helper && cli)
        {
            snprintf(out, out_size,
                     "python3 \"%s\" setup-android \"%s\" && "
                     "python3 \"%s\" --no-color build android",
                     helper, s_project.root, cli);
            return;
        }
        if (cli)
        {
            snprintf(out, out_size, "python3 \"%s\" --no-color build android",
                     cli);
            return;
        }
        if (command_available("aroma"))
        {
            snprintf(out, out_size, "aroma build android");
            return;
        }
        snprintf(out, out_size,
                 "echo 'Aroma CLI not found; set AROMA_CLI or install aroma'");
        return;
    }
    snprintf(out, out_size, "%s", s_project.build_cmd);
}

static void project_run_command(char *out, size_t out_size)
{
    out[0] = '\0';
    if (s_project.target == 1)
    {
        const char *helper = ide_helper_path();
        const char *cli = aroma_cli_path();
        if (helper)
        {
            if (s_project.android_serial[0])
                snprintf(out, out_size,
                         "python3 \"%s\" run-android \"%s\" \"%s\"", helper,
                         s_project.root, s_project.android_serial);
            else
                snprintf(out, out_size,
                         "python3 \"%s\" run-android \"%s\"", helper,
                         s_project.root);
            return;
        }
        if (cli)
        {
            snprintf(out, out_size, "python3 \"%s\" --no-color run android",
                     cli);
            return;
        }
        if (command_available("aroma"))
        {
            snprintf(out, out_size, "aroma run android");
            return;
        }
        snprintf(out, out_size,
                 "echo 'Aroma CLI not found; set AROMA_CLI or install aroma'");
        return;
    }
    snprintf(out, out_size, "%s", s_project.run_cmd);
}

static void project_clean_command(char *out, size_t out_size)
{
    out[0] = '\0';
    if (s_project.target == 1)
    {
        const char *cli = aroma_cli_path();
        if (cli)
        {
            snprintf(out, out_size, "python3 \"%s\" --no-color clean android",
                     cli);
            return;
        }
        if (command_available("aroma"))
        {
            snprintf(out, out_size, "aroma clean android");
            return;
        }
        snprintf(out, out_size,
                 "rm -rf build android/app/build android/.gradle");
        return;
    }
    snprintf(out, out_size, "rm -rf build");
}

static void project_append_build_output(const char *text)
{
    size_t len = strlen(s_project.build_output);
    size_t add = strlen(text);
    if (len + add >= sizeof(s_project.build_output) - 1)
        add = sizeof(s_project.build_output) - 1 - len;
    if (add > 0)
    {
        memcpy(s_project.build_output + len, text, add);
        s_project.build_output[len + add] = '\0';
    }
    s_project.has_build_output = true;
    s_project_ui_dirty = true;
}

static void project_append_run_output(const char *text)
{
    size_t len = strlen(s_project.run_output);
    size_t add = strlen(text);
    if (len + add >= sizeof(s_project.run_output) - 1)
        add = sizeof(s_project.run_output) - 1 - len;
    if (add > 0)
    {
        memcpy(s_project.run_output + len, text, add);
        s_project.run_output[len + add] = '\0';
    }
    s_project.has_run_output = true;
    s_project_ui_dirty = true;
}

static void project_append_clean_output(const char *text)
{
    size_t len = strlen(s_project.clean_output);
    size_t add = strlen(text);
    if (len + add >= sizeof(s_project.clean_output) - 1)
        add = sizeof(s_project.clean_output) - 1 - len;
    if (add > 0)
    {
        memcpy(s_project.clean_output + len, text, add);
        s_project.clean_output[len + add] = '\0';
    }
    s_project.has_clean_output = true;
    s_project_ui_dirty = true;
}

static void reset_workspace_ui(void)
{
    s_active_file = -1;
    s_tab_count = 0;
    s_tab_hover = -1;
    s_tree_node_count = 0;
    s_tree_row_count = 0;
    s_tree_scroll = 0;
    s_search_hit_count = 0;
    s_search_scroll = 0;
    s_search_summary[0] = '\0';
    s_outline_count = 0;
    s_problem_total = 0;
    s_scm_changes = 0;
    s_scm_files[0] = s_scm_files[1] = s_scm_files[2] = -1;
    s_pending_definition = false;

    for (int i = 0; i < ide_file_count; ++i)
        ide_files[i].open = false;

    if (s_editor)
    {
        aroma_code_editor_set_text(s_editor, "");
        aroma_code_editor_set_diagnostics(s_editor, NULL, 0);
    }

    project_init_state();
    tree_build();
    workspace_search("");
    refresh_outline();
    update_scm_count();
    build_problem_list();
    draw_all();
}

static void open_initial_files(void)
{
    if (!s_editor)
        return;

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

    if (initial_active >= 0)
    {
        ide_open_file(initial_active, true);
        aroma_code_editor_focus(s_editor);
    }
    else
    {
        if (s_editor)
            aroma_code_editor_set_text(s_editor, "");
        refresh_outline();
        draw_tabs();
        draw_breadcrumb();
    }
}

static bool project_create_extended(const char *name, const char *path,
                                    int template_id, bool with_cmake,
                                    bool with_readme)
{
    if (!name || !name[0] || !path || !path[0])
        return false;
    if (template_id < 0 || template_id > 2)
        template_id = 1;

    char root[PROJECT_PATH_MAX];
    size_t path_len = strlen(path);
    if (path_len > 0 && path[path_len - 1] == '/')
        snprintf(root, sizeof(root), "%s%s", path, name);
    else
        snprintf(root, sizeof(root), "%s/%s", path, name);
    if (access(root, F_OK) == 0)
        return false;

    char src_dir[PROJECT_PATH_MAX];
    char ui_dir[PROJECT_PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/src", root);
    snprintf(ui_dir, sizeof(ui_dir), "%s/ui", root);
    if (!wizard_mkdir_p(src_dir) || !wizard_mkdir_p(ui_dir))
        return false;

    char markup[16384];
    char main_source[16384];
    char cmake_source[8192];
    char readme[8192];
    char json[2048];
    char cmake_name[PROJECT_NAME_MAX];
    wizard_cmake_identifier(name, cmake_name, sizeof(cmake_name));
    wizard_default_markup(template_id, name, markup, sizeof(markup));
    wizard_default_main(main_source, sizeof(main_source));

    if (!wizard_write_file(root, "ui/app.aroma", markup) ||
        !wizard_write_file(root, "src/main.c", main_source))
        return false;

    snprintf(json, sizeof(json),
             "{\n  \"title\": \"%s\",\n  \"width\": 900,\n"
             "  \"height\": 620,\n  \"template\": \"%s\"\n}\n",
             name, k_wizard_template_titles[template_id]);
    if (!wizard_write_file(root, "aroma.json", json))
        return false;

    if (with_cmake)
    {
#ifndef AROMA_IDE_SDK_ROOT
#define AROMA_IDE_SDK_ROOT "."
#endif
        snprintf(cmake_source, sizeof(cmake_source),
                 "cmake_minimum_required(VERSION 3.15)\n"
                 "\n"
                 "project(%s C)\n"
                 "\n"
                 "set(CMAKE_C_STANDARD 11)\n"
                 "set(CMAKE_C_STANDARD_REQUIRED ON)\n"
                 "\n"
                 "if(DEFINED ENV{AROMA_SDK_PATH})\n"
                 "    set(AROMA_DIR \"$ENV{AROMA_SDK_PATH}\")\n"
                 "else()\n"
                 "    set(AROMA_DIR \"%s\")\n"
                 "endif()\n"
                 "\n"
                 "if(NOT TARGET aroma)\n"
                 "    add_subdirectory(${AROMA_DIR} aroma_sdk)\n"
                 "endif()\n"
                 "\n"
                 "add_executable(%s src/main.c)\n"
                 "target_link_libraries(%s PRIVATE aroma)\n"
                 "\n"
                 "if(NOT EMSCRIPTEN)\n"
                 "    find_package(Vulkan QUIET)\n"
                 "    if(Vulkan_FOUND)\n"
                 "        target_link_libraries(%s PRIVATE Vulkan::Vulkan)\n"
                 "    endif()\n"
                 "endif()\n"
                 "\n"
                 "if(NOT EMSCRIPTEN AND NOT ANDROID)\n"
                 "    add_custom_command(TARGET %s POST_BUILD\n"
                 "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
                 "                \"${CMAKE_CURRENT_SOURCE_DIR}/ui\"\n"
                 "                \"$<TARGET_FILE_DIR:%s>/ui\")\n"
                 "endif()\n",
                 cmake_name, AROMA_IDE_SDK_ROOT, cmake_name, cmake_name,
                 cmake_name, cmake_name, cmake_name);
        if (!wizard_write_file(root, "CMakeLists.txt", cmake_source))
            return false;
    }

    if (with_readme)
    {
        snprintf(readme, sizeof(readme),
                 "# %s\n\n"
                 "Created with AromaUI Studio using the **%s** template.\n\n"
                 "## Layout\n\n"
                 "- `ui/app.aroma` - declarative UI, editable in the drag-and-drop designer\n"
                 "- `src/main.c` - application bootstrap and callbacks\n"
                 "- `aroma.json` - project metadata\n"
                 "%s\n"
                 "## Build\n\n"
                 "```bash\n"
                 "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug\n"
                 "cmake --build build -j\n"
                 "./build/%s\n"
                 "```\n\n"
                 "The project links to the AromaUI SDK through `AROMA_SDK_PATH`, or\n"
                 "the path recorded by Studio in `CMakeLists.txt`.\n",
                 name, k_wizard_template_titles[template_id],
                 with_cmake ? "- `CMakeLists.txt` - CMake target\n" : "",
                 cmake_name);
        if (!wizard_write_file(root, "README.md", readme))
            return false;
    }

    if (!wizard_write_file(root, ".gitignore", "build/\n*.o\n"))
        return false;

    log_add("workspace/createProject", "Created %s at %s", name, root);
    return true;
}

static bool project_create(const char *name, const char *path)
{
    if (!project_create_extended(name, path, 1, true, true))
        return false;

    char project_root[PROJECT_PATH_MAX];
    size_t path_len = strlen(path);
    if (path_len > 0 && path[path_len - 1] == '/')
        snprintf(project_root, sizeof(project_root), "%s%s", path, name);
    else
        snprintf(project_root, sizeof(project_root), "%s/%s", path, name);

    if (!project_open(project_root))
        return false;
    toast("Created and opened %s", name);
    return true;
}

static bool project_open(const char *path)
{
    if (!path || !path[0])
        return false;

    char json_path[PROJECT_PATH_MAX];
    snprintf(json_path, sizeof(json_path), "%s/aroma.json", path);
    if (access(json_path, R_OK) != 0)
    {
        snprintf(s_dialog_error, sizeof(s_dialog_error),
                 "No aroma.json project file found in %s", path);
        return false;
    }

    int saved = save_all_files_ex(true);
    if (saved > 0)
        log_add("workspace/applyEdit", "Saved %d modified files before switching project",
                saved);

                                                                              
                                                   
    designer_reset_cache();

    if (!ide_workspace_init(path))
    {
        snprintf(s_dialog_error, sizeof(s_dialog_error),
                 "Failed to open project: %s", ide_workspace_last_error());
        return false;
    }

    reset_workspace_ui();
    open_initial_files();
    draw_all();
    toast("Opened project %s", ide_workspace_project_name());
    log_add("workspace/openProject", "Opened project at %s", path);

                                                                        
    snprintf(s_home_recent_project_path, sizeof(s_home_recent_project_path),
             "%s", path);
    const char *proj_name = ide_workspace_project_name();
    if (proj_name && proj_name[0])
        snprintf(s_home_recent_project_name, sizeof(s_home_recent_project_name),
                 "%s", proj_name);
    else
        snprintf(s_home_recent_project_name, sizeof(s_home_recent_project_name),
                 "%s", path);
    s_home_has_recent_project = true;
    ide_home_set_recent_project(path);
    return true;
}

static void *build_thread_fn(void *arg)
{
    (void)arg;
    char build_cmd[PROJECT_CMD_MAX];
    project_build_command(build_cmd, sizeof(build_cmd));
    char cmd[PROJECT_CMD_MAX * 2];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && %s 2>&1", s_project.root,
             build_cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        project_append_build_output("Failed to start build process\n");
        s_project.is_building = false;
        toast("Build failed to start");
        return NULL;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
        project_append_build_output(buf);
    int rc = pclose(fp);

    if (rc == 0)
    {
        project_append_build_output("Build succeeded\n");
        toast("Build succeeded");
        log_add("workspace/executeCommand", "Build succeeded");
    }
    else
    {
        char rc_msg[64];
        snprintf(rc_msg, sizeof(rc_msg), "Build failed (exit %d)\n",
                 WEXITSTATUS(rc));
        project_append_build_output(rc_msg);
        toast("Build failed");
        log_add("workspace/executeCommand", "Build failed (exit %d)",
                WEXITSTATUS(rc));
    }

    s_project.is_building = false;
    s_project_ui_dirty = true;
    return NULL;
}

static void project_build(void)
{
    if (s_project.is_building || s_project.is_running || s_project.is_cleaning)
        return;

    s_project.is_building = true;
    s_project.output_mode = 0;
    s_project.has_build_output = false;
    s_project.build_output[0] = '\0';
    s_project_ui_dirty = true;

    char build_cmd[PROJECT_CMD_MAX];
    project_build_command(build_cmd, sizeof(build_cmd));
    log_add("workspace/executeCommand", "$ %s", build_cmd);
    toast("Building %s%s...", s_project.name,
          s_project.target == 1 ? " for Android" : "");
    s_panel_tab = 1;
    if (!s_panel_visible)
        set_panel_visible(true);
    else
        draw_panel();

    if (pthread_create(&s_project.build_thread, NULL, build_thread_fn, NULL) != 0)
    {
        s_project.is_building = false;
        toast("Build failed to start");
    }
    pthread_detach(s_project.build_thread);
}

static void *clean_thread_fn(void *arg)
{
    (void)arg;
    char clean_cmd[PROJECT_CMD_MAX];
    project_clean_command(clean_cmd, sizeof(clean_cmd));
    char cmd[PROJECT_CMD_MAX * 2];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && %s 2>&1", s_project.root,
             clean_cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        project_append_clean_output("Failed to start clean process\n");
        s_project.is_cleaning = false;
        toast("Clean failed to start");
        return NULL;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
        project_append_clean_output(buf);
    int rc = pclose(fp);

    if (rc == 0)
    {
        project_append_clean_output("Build directory removed\n");
        toast("Clean completed");
        log_add("workspace/executeCommand", "Clean completed");
    }
    else
    {
        project_append_clean_output("Clean failed\n");
        toast("Clean failed");
        log_add("workspace/executeCommand", "Clean failed");
    }

    s_project.is_cleaning = false;
    s_project_ui_dirty = true;
    return NULL;
}

static void project_clean(void)
{
    if (s_project.is_building || s_project.is_running || s_project.is_cleaning)
        return;

    s_project.is_cleaning = true;
    s_project.output_mode = 2;
    s_project.has_clean_output = false;
    s_project.clean_output[0] = '\0';
    s_project_ui_dirty = true;

    char clean_cmd[PROJECT_CMD_MAX];
    project_clean_command(clean_cmd, sizeof(clean_cmd));
    log_add("workspace/executeCommand", "$ %s", clean_cmd);
    toast("Cleaning %s%s...", s_project.name,
          s_project.target == 1 ? " for Android" : "");
    s_panel_tab = 1;
    if (!s_panel_visible)
        set_panel_visible(true);
    else
        draw_panel();

    if (pthread_create(&s_project.build_thread, NULL, clean_thread_fn, NULL) != 0)
    {
        s_project.is_cleaning = false;
        toast("Clean failed to start");
    }
    pthread_detach(s_project.build_thread);
}

static void *run_thread_fn(void *arg)
{
    (void)arg;
    char run_cmd[PROJECT_CMD_MAX];
    project_run_command(run_cmd, sizeof(run_cmd));
    char cmd[PROJECT_CMD_MAX * 2];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && %s 2>&1", s_project.root,
             run_cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        project_append_run_output("Failed to start program\n");
        s_project.is_running = false;
        toast("Run failed to start");
        return NULL;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
        project_append_run_output(buf);
    int rc = pclose(fp);

    if (rc == 0)
    {
        project_append_run_output("Program exited normally\n");
        toast("Program finished");
        log_add("workspace/executeCommand", "Program exited normally");
    }
    else
    {
        char rc_msg[64];
        snprintf(rc_msg, sizeof(rc_msg), "Program exited (code %d)\n",
                 WEXITSTATUS(rc));
        project_append_run_output(rc_msg);
        toast("Program exited");
        log_add("workspace/executeCommand", "Program exited (code %d)",
                WEXITSTATUS(rc));
    }

    s_project.is_running = false;
    s_project_ui_dirty = true;
    return NULL;
}

static void project_run(void)
{
    if (s_project.is_running || s_project.is_building || s_project.is_cleaning)
        return;

    if (s_project.target == 1)
    {
        project_android_refresh_devices(false);
        if (s_android_device_count == 0)
        {
            toast("Connect an Android device or start an emulator first");
            s_panel_tab = 1;
            if (!s_panel_visible)
                set_panel_visible(true);
            return;
        }
        if (s_android_device_selected < 0)
            project_android_select_device(0);
    }

    s_project.is_running = true;
    s_project.output_mode = 1;
    s_project.has_run_output = false;
    s_project.run_output[0] = '\0';
    s_project_ui_dirty = true;

    char run_cmd[PROJECT_CMD_MAX];
    project_run_command(run_cmd, sizeof(run_cmd));
    log_add("workspace/executeCommand", "$ %s", run_cmd);
    toast("Running %s%s...", s_project.name,
          s_project.target == 1 ? " on Android" : "");
    s_panel_tab = 1;
    if (!s_panel_visible)
        set_panel_visible(true);
    else
        draw_panel();

    if (pthread_create(&s_project.run_thread, NULL, run_thread_fn, NULL) != 0)
    {
        s_project.is_running = false;
        toast("Run failed to start");
    }
    pthread_detach(s_project.run_thread);
}

static void project_draw_output_panel(void)
{
    AromaNode *canvas = s_panel_draw_canvas;
    if (!canvas)
        return;

    AromaFont *ui10 = font_ui(10);
    AromaFont *mono11 = font_mono(11);
    AromaFont *ui11 = font_ui(11);

    int px = s_panel_draw_px;
    int y = s_panel_draw_py + 10;
    y += 24;

    text(canvas, "BUILD & RUN OUTPUT", px + 8, y, C_TEXT_SOFT, ui11);
    y += 24;

    int line_right = s_panel_draw_right_x - px - 28;
    line(canvas, px + 8, y, px + 308, y, C_BORDER, 1);
    y += 16;

    text(canvas, "Project: ", px + 8, y, C_TEXT_DIM, ui10);
    text(canvas, s_project.name, px + 68, y, C_TEXT, ui10);
    y += 18;

    text(canvas, "Root:   ", px + 8, y, C_TEXT_DIM, ui10);
    text_fit(canvas, s_project.root, px + 58, y,
             s_panel_draw_right_x - px - 58, C_TEXT, ui10);
    y += 18;

    text(canvas, "Target: ", px + 8, y, C_TEXT_DIM, ui10);
    text(canvas, s_project.target == 1 ? "Android" : "Linux", px + 68, y,
         C_TEXT, ui10);
    if (s_project.target == 1)
    {
        int target_w = text_width(ui10, "Android");
        text(canvas, "·", px + 68 + target_w + 8, y, C_TEXT_FAINT, ui10);
        text_fit(canvas,
                 s_project.android_serial[0] ? s_project.android_serial
                                             : "no device",
                 px + 68 + target_w + 20, y,
                 s_panel_draw_right_x - px - 68 - target_w - 28,
                 s_project.android_serial[0] ? C_TEXT : C_TEXT_FAINT, ui10);
    }
    y += 26;

    if (s_project.is_building)
    {
        fill(canvas, px + 8, y, 8, 8, 4, C_PRIMARY);
        text(canvas, "Building...", px + 22, y + 2, C_PRIMARY, ui10);
    }
    else if (s_project.is_cleaning)
    {
        fill(canvas, px + 8, y, 8, 8, 4, C_PRIMARY);
        text(canvas, "Cleaning...", px + 22, y + 2, C_PRIMARY, ui10);
    }
    else if (s_project.is_running)
    {
        fill(canvas, px + 8, y, 8, 8, 4, C_GREEN);
        text(canvas, "Running...", px + 22, y + 2, C_GREEN, ui10);
    }
    y += 20;

    const char *output = s_project.build_output;
    bool has_output = s_project.has_build_output;
    const char *empty_hint = "No build output yet. Press F7 or choose Build.";
    if (s_project.output_mode == 1)
    {
        output = s_project.run_output;
        has_output = s_project.has_run_output;
        empty_hint = "No run output yet. Press F5 or choose Run.";
    }
    else if (s_project.output_mode == 2)
    {
        output = s_project.clean_output;
        has_output = s_project.has_clean_output;
        empty_hint = "No clean output yet. Choose Clean Build.";
    }

    bool output_enabled = true;
    if (s_project.output_mode == 0 && !s_setting_show_build_output)
        output_enabled = false;
    else if (s_project.output_mode == 1 && !s_setting_show_run_output)
        output_enabled = false;

    if (!output_enabled)
    {
        text(canvas, s_project.output_mode == 0
                         ? "Build output is hidden (Settings > Output)"
                         : "Run output is hidden (Settings > Output)",
             px + 8, y, C_TEXT_FAINT, ui10);
        y += 18;
    }
    else if (!has_output)
    {
        text(canvas, empty_hint, px + 8, y, C_TEXT_FAINT, ui10);
        y += 18;
    }
    else
    {
        int total_lines = 0;
        for (const char *scan = output; *scan;)
        {
            size_t line_len = strcspn(scan, "\n");
            if (line_len > 0)
                total_lines++;
            scan += line_len;
            if (*scan == '\n')
                scan++;
        }
        int first_line = 0;
        if (s_setting_auto_scroll_output && total_lines > PANEL_VISIBLE_ROWS)
            first_line = total_lines - PANEL_VISIBLE_ROWS;

        char line_buf[512];
        const char *p = output;
        int line_index = 0;
        int lines = 0;
        while (*p && lines < PANEL_VISIBLE_ROWS)
        {
            size_t line_len = strcspn(p, "\n");
            if (line_len > 0)
            {
                if (line_index >= first_line)
                {
                    size_t copy_len = line_len;
                    if (copy_len >= sizeof(line_buf))
                        copy_len = sizeof(line_buf) - 1;
                    memcpy(line_buf, p, copy_len);
                    line_buf[copy_len] = '\0';
                    text_fit(canvas, line_buf, px + 8, y, line_right,
                             C_TEXT, mono11);
                    y += 16;
                    lines++;
                }
                line_index++;
            }
            p += line_len;
            if (*p == '\n')
                p++;
        }
    }

    text(canvas, "Press F7 to build, F5 to run", px + 8,
         s_panel_draw_py + s_panel_draw_h - 18, C_TEXT_FAINT, ui10);
}

static void destroy_prop_widgets(void)
{
    for (int i = 0; i < 16; i++)
    {
        if (s_prop_category_widgets[i])
        {
            __destroy_node(s_prop_category_widgets[i]);
            s_prop_category_widgets[i] = NULL;
        }
    }
    for (int i = 0; i < 48; i++)
    {
        if (s_prop_row_widgets[i])
        {
                                                                    
                                                                                 
            __destroy_node(s_prop_row_widgets[i]);
            s_prop_row_widgets[i] = NULL;
        }
        s_prop_label_widgets[i] = NULL;
        s_prop_value_widgets[i] = NULL;
        s_prop_dropdown_widgets[i] = NULL;
        s_prop_dropdown_widget_count[i] = 0;
    }
}

static void redraw_properties_widgets(void)
{
    if (s_dialog_open && s_dialog_mode == IDE_DIALOG_PROPERTIES)
        draw_dialog();
    else
        draw_panel();
}

static void prop_row_on_click(void *user_data)
{
    intptr_t ptr = (intptr_t)user_data;
    if (ptr <= -100)
    {
        int cat_idx = -100 - (int)ptr;
        if (cat_idx >= 0 && cat_idx < s_prop_cat_count)
        {
            s_prop_cats[cat_idx].expanded = !s_prop_cats[cat_idx].expanded;
            redraw_properties_widgets();
        }
        return;
    }
    if (ptr < 0)
    {
        int opt = -1 - (int)ptr;
        if (!s_designer_visible || s_prop_dropdown_index < 0 ||
            s_prop_dropdown_index >= s_design_doc.node_count ||
            s_prop_dropdown_row < 0 || s_prop_dropdown_row >= PROP_SCHEMA_COUNT)
            return;
        const PropSchemaEntry *schema = &k_prop_schema_all[s_prop_dropdown_row];
        const char *prop_key = schema->key;
        int opt_count = 0;
        const char *const *opts = prop_dropdown_options(
            s_design_doc.nodes[s_prop_dropdown_index].tag, prop_key,
            &opt_count);
        if (opts && opt_count > 0 && opt >= 0 && opt < opt_count)
        {
            ide_design_set_attr(&s_design_doc, s_prop_dropdown_index,
                                prop_key, opts[opt], true);
            designer_after_mutation();
        }
        s_prop_dropdown_open = false;
        s_prop_edit_active = false;
        ide_layout_recalc();
        ide_update_panel_rects();
        redraw_properties_widgets();
        return;
    }

    int row = (int)ptr;
    if (!s_designer_visible || s_design_doc.selected < 0 ||
        s_design_doc.selected >= s_design_doc.node_count)
        return;
    const IdeDesignNode *node = &s_design_doc.nodes[s_design_doc.selected];
    if (row < 0 || row >= PROP_SCHEMA_COUNT)
        return;
    const PropSchemaEntry *schema = &k_prop_schema_all[row];
    const char *prop_key = schema->key;
    int opt_count = 0;
    const char *const *opts = prop_dropdown_options(node->tag, prop_key,
                                                    &opt_count);
    if (opts && opt_count > 0)
    {
        s_prop_dropdown_index = s_design_doc.selected;
        s_prop_dropdown_row = row;
        s_prop_dropdown_open = true;
        s_prop_edit_active = false;
        s_prop_edit_row = 0;
        const char *cur_val = ide_design_attr(
            &s_design_doc, s_design_doc.selected, prop_key);
        for (int o = 0; o < opt_count; o++)
        {
            if (cur_val && strcmp(cur_val, opts[o]) == 0)
            {
                s_prop_edit_row = o;
                break;
            }
        }
        ide_layout_recalc();
        ide_update_panel_rects();
        redraw_properties_widgets();
        return;
    }
    s_prop_edit_index = s_design_doc.selected;
    s_prop_edit_row = row;
    s_prop_edit_active = true;
    const char *cur_val = ide_design_attr(&s_design_doc, s_design_doc.selected,
                                          prop_key);
    const char *cur = cur_val ? cur_val : "";
    snprintf(s_prop_edit_buf, sizeof(s_prop_edit_buf), "%s", cur);
    redraw_properties_widgets();
}
static void draw_properties_content(AromaNode *canvas, int offset_x,
                                    int offset_y, int canvas_w,
                                    int canvas_h)
{
    if (!s_designer_visible || s_design_doc.selected <= 0 ||
        s_design_doc.selected >= s_design_doc.node_count)
    {
        if (s_prop_last_selected >= 0)
        {
            destroy_prop_widgets();
            s_prop_last_selected = -1;
            s_prop_edit_active = false;
            s_prop_dropdown_open = false;
            s_prop_cat_count = 0;
        }
        text(canvas, "No widget selected", offset_x + PANEL_X_MARGIN,
             offset_y + 60, C_TEXT_DIM, font_ui(11));
        text(canvas, "Select a widget on the canvas to edit its properties",
             offset_x + PANEL_X_MARGIN, offset_y + 78, C_TEXT_FAINT,
             font_ui(10));
        return;
    }

    int index = s_design_doc.selected;
    const IdeDesignNode *node = &s_design_doc.nodes[index];
    AromaFont *ui11 = font_ui(11);
    AromaFont *ui10 = font_ui(10);
    int row_h = 26;
    int cat_h = 22;
                                                     
    if (s_prop_last_selected != index)
    {
        destroy_prop_widgets();
        s_prop_last_selected = index;
        s_prop_edit_active = false;
        s_prop_dropdown_open = false;
        s_prop_cat_count = 0;
        for (int c = 0; c < 16; c++)
            s_prop_cats[c].expanded = true;
    }

                   
    text(canvas, "PROPERTIES", offset_x + PANEL_X_MARGIN, offset_y + 16,
         C_TEXT, ui11);
    text(canvas, node->tag, offset_x + PANEL_X_MARGIN + 90, offset_y + 16,
         C_TEXT_SOFT, ui10);

                    
    text(canvas, "\ue8b2  Search properties", offset_x + PANEL_X_MARGIN,
         offset_y + 42, C_TEXT_DIM, ui10);

                                                
    const WidgetPropMap *wmap = widget_prop_find(node->tag);

                                                              
    int cat_count = 0;
    int cat_start_idx[16];
    int cat_prop_count[16];
    const char *cat_names[16];

    for (int i = 0; i < 16; i++)
    {
        cat_start_idx[i] = -1;
        cat_prop_count[i] = 0;
        cat_names[i] = NULL;
    }

                                        
    for (int pi = 0; pi < wmap->count; pi++)
    {
        const char *key = wmap->keys[pi];
        int schema_idx = prop_schema_find_index(key);
        if (schema_idx < 0)
            continue;
        const char *cat_name = k_prop_schema_all[schema_idx].category;
        int ci = -1;
        for (int c = 0; c < cat_count; c++)
        {
            if (strcmp(cat_names[c], cat_name) == 0)
            {
                ci = c;
                break;
            }
        }
        if (ci < 0)
        {
            ci = cat_count;
            cat_names[ci] = cat_name;
            cat_start_idx[ci] = 0;
            cat_prop_count[ci] = 0;
            cat_count++;
        }
        cat_prop_count[ci]++;
    }

    s_prop_cat_count = cat_count;
    for (int c = 0; c < cat_count; c++)
    {
        s_prop_cats[c].name = cat_names[c];
        s_prop_cats[c].start_row = cat_start_idx[c];
        s_prop_cats[c].row_count = cat_prop_count[c];
        s_prop_cats[c].expanded = true;
    }

                                                    
    int cy = offset_y + 70;
    int row_x = offset_x + PANEL_X_MARGIN;
    int row_w = canvas_w - PANEL_X_MARGIN * 2;
    if (s_prop_last_panel_y != offset_y || s_prop_last_selected != index)
    {
        s_prop_last_panel_y = offset_y;
        int widget_counter = 0;

                                              
        int prop_idx = 0;
        for (int ci = 0; ci < s_prop_cat_count; ci++)
        {
            struct PropCategory *cat = &s_prop_cats[ci];

                                 
            AromaNode *cat_card = aroma_card_create(
                canvas, row_x - 4, cy - 2, row_w + 8, cat_h - 4, CARD_TYPE_FILLED);
            if (cat_card)
            {
                s_prop_category_widgets[ci] = cat_card;
                aroma_node_set_z_index(cat_card, 5);
                AromaRect *cr = aroma_node_get_rect(cat_card);
                if (cr)
                {
                    cr->x = row_x - 4;
                    cr->y = cy - 2;
                    cr->width = row_w + 8;
                    cr->height = cat_h - 4;
                }
                aroma_card_set_colors(cat_card,
                                      cat->expanded ? M3_SURFACE_HIGH : M3_SURFACE_LOWEST,
                                      cat->expanded ? M3_SURFACE_HIGH : M3_SURFACE_LOWEST);
                aroma_card_set_click_callback(cat_card, prop_row_on_click,
                                              (void *)(intptr_t)(-100 - ci));
            }

            const char *arrow = cat->expanded ? "\ue5e5" : "\ue5e4";
            text(canvas, arrow, row_x + 4, cy, cat->expanded ? C_PRIMARY : C_TEXT_DIM,
                 ui10);
            text(canvas, cat->name, row_x + 20, cy, C_TEXT, ui10);
            cy += cat_h;

            if (!cat->expanded)
            {
                prop_idx += cat->row_count;
                continue;
            }

                                               
            for (int pi = 0; pi < wmap->count; pi++)
            {
                const char *key = wmap->keys[pi];
                int schema_idx = prop_schema_find_index(key);
                if (schema_idx < 0)
                    continue;
                const PropSchemaEntry *schema = &k_prop_schema_all[schema_idx];
                if (strcmp(schema->category, cat->name) != 0)
                    continue;
                const char *prop_label = schema->label;

                int row_y = cy;
                bool editing = s_prop_edit_active && s_prop_edit_index == index &&
                               s_prop_edit_row == schema_idx;
                bool dropdown_open = s_prop_dropdown_open &&
                                     s_prop_dropdown_index == index &&
                                     s_prop_dropdown_row == schema_idx;

                                       
                char val_buf[128];
                const char *val = "";
                if (strcmp(key, "x") == 0)
                    snprintf(val_buf, sizeof(val_buf), "%d", node->x), val = val_buf;
                else if (strcmp(key, "y") == 0)
                    snprintf(val_buf, sizeof(val_buf), "%d", node->y), val = val_buf;
                else if (strcmp(key, "width") == 0)
                    snprintf(val_buf, sizeof(val_buf), "%d", node->width),
                        val = val_buf;
                else if (strcmp(key, "height") == 0)
                    snprintf(val_buf, sizeof(val_buf), "%d", node->height),
                        val = val_buf;
                else
                    val = ide_design_attr(&s_design_doc, index, key);
                if (!val)
                    val = "";
                if (editing)
                    val = s_prop_edit_buf;

                int opt_count = 0;
                const char *const *opts = NULL;
                if (schema->is_dropdown)
                {
                    opts = prop_dropdown_options(node->tag, key, &opt_count);
                }
                bool is_dropdown = opts != NULL && opt_count > 0;

                              
                AromaNode *row_card = aroma_card_create(
                    canvas, row_x, row_y - 2, row_w, row_h - 4, CARD_TYPE_FILLED);
                if (row_card)
                {
                    aroma_node_set_z_index(row_card, 5);
                    AromaRect *rr = aroma_node_get_rect(row_card);
                    if (rr)
                    {
                        rr->x = row_x;
                        rr->y = row_y - 2;
                        rr->width = row_w;
                        rr->height = row_h - 4;
                    }
                    if (editing)
                        aroma_card_set_colors(row_card, C_SELECT, C_SELECT);
                    else if (dropdown_open || is_dropdown)
                        aroma_card_set_colors(row_card, C_HOVER_SOFT, C_HOVER_SOFT);
                    else
                        aroma_card_set_colors(row_card, M3_SURFACE_LOWEST, M3_SURFACE_LOWEST);
                    aroma_card_set_click_callback(row_card, prop_row_on_click,
                                                  (void *)(intptr_t)schema_idx);
                }

                                    
                int name_w = (row_w * 30) / 100;
                AromaNode *lbl = aroma_label_create(
                    row_card, prop_label, 6, 2, LABEL_STYLE_LABEL_MEDIUM);
                if (lbl)
                {
                    aroma_node_set_z_index(lbl, 6);
                    aroma_label_set_color(
                        lbl, editing ? C_PRIMARY
                                     : (is_dropdown ? C_TEXT_SOFT : C_TEXT));
                }

                                    
                int val_x = row_x + name_w + 8;
                int val_w = row_w - name_w - 16;
                AromaNode *val_lbl = aroma_label_create(
                    row_card, val, val_x - row_x + 6, 2,
                    LABEL_STYLE_LABEL_MEDIUM);
                if (val_lbl)
                {
                    aroma_node_set_z_index(val_lbl, 6);
                    aroma_label_set_color(val_lbl, C_TEXT);
                }

                                    
                AromaNode *arrow_lbl = NULL;
                if (is_dropdown && !dropdown_open)
                {
                    arrow_lbl = aroma_label_create(
                        row_card, AROMA_ICON_ARROW_DROP_DOWN, row_w - 18, 2,
                        LABEL_STYLE_LABEL_MEDIUM);
                    if (arrow_lbl)
                    {
                        aroma_node_set_z_index(arrow_lbl, 6);
                        aroma_label_set_color(arrow_lbl, C_TEXT_DIM);
                    }
                }

                                                   
                if (widget_counter < 48)
                {
                    s_prop_row_widgets[widget_counter] = row_card;
                    s_prop_label_widgets[widget_counter] = lbl;
                    s_prop_value_widgets[widget_counter] = val_lbl;
                    s_prop_dropdown_widgets[widget_counter] =
                        is_dropdown ? arrow_lbl : NULL;
                    s_prop_dropdown_widget_count[widget_counter] =
                        is_dropdown ? 1 : 0;
                }
                widget_counter++;

                cy += row_h;
            }
        }
    }
                                             
    int widget_counter = 0;
    for (int ci = 0; ci < s_prop_cat_count; ci++)
    {
        if (!s_prop_cats[ci].expanded)
            continue;
        for (int pi = 0; pi < wmap->count; pi++)
        {
            const char *key = wmap->keys[pi];
            int schema_idx = prop_schema_find_index(key);
            if (schema_idx < 0)
                continue;
            const PropSchemaEntry *schema = &k_prop_schema_all[schema_idx];
            if (strcmp(schema->category, s_prop_cats[ci].name) != 0)
                continue;
            bool editing = s_prop_edit_active && s_prop_edit_index == index &&
                           s_prop_edit_row == schema_idx;
            bool dropdown_open = s_prop_dropdown_open &&
                                 s_prop_dropdown_index == index &&
                                 s_prop_dropdown_row == schema_idx;
            int opt_count = 0;
            const char *const *opts = NULL;
            if (schema->is_dropdown)
            {
                opts = prop_dropdown_options(node->tag, key, &opt_count);
            }
            bool is_dropdown = opts != NULL && opt_count > 0;

                                   
            char val_buf[128];
            const char *val = "";
            if (strcmp(key, "x") == 0)
                snprintf(val_buf, sizeof(val_buf), "%d", node->x), val = val_buf;
            else if (strcmp(key, "y") == 0)
                snprintf(val_buf, sizeof(val_buf), "%d", node->y), val = val_buf;
            else if (strcmp(key, "width") == 0)
                snprintf(val_buf, sizeof(val_buf), "%d", node->width),
                    val = val_buf;
            else if (strcmp(key, "height") == 0)
                snprintf(val_buf, sizeof(val_buf), "%d", node->height),
                    val = val_buf;
            else if (editing)
                val = s_prop_edit_buf;
            else
                val = ide_design_attr(&s_design_doc, index, key);
            if (!val)
                val = "";

            if (widget_counter < 48)
            {
                if (s_prop_row_widgets[widget_counter])
                {
                    if (editing)
                        aroma_card_set_colors(s_prop_row_widgets[widget_counter],
                                              C_SELECT, C_SELECT);
                    else if (dropdown_open || is_dropdown)
                        aroma_card_set_colors(s_prop_row_widgets[widget_counter],
                                              C_HOVER_SOFT, C_HOVER_SOFT);
                    else
                        aroma_card_set_colors(s_prop_row_widgets[widget_counter],
                                              M3_SURFACE_LOWEST, M3_SURFACE_LOWEST);
                }
                if (s_prop_label_widgets[widget_counter])
                {
                    aroma_label_set_color(
                        s_prop_label_widgets[widget_counter],
                        editing ? C_PRIMARY
                                : (is_dropdown ? C_TEXT_SOFT : C_TEXT));
                }
                if (s_prop_value_widgets[widget_counter])
                    aroma_label_set_text(s_prop_value_widgets[widget_counter],
                                         val);
                if (s_prop_dropdown_widgets[widget_counter] &&
                    is_dropdown && !dropdown_open)
                {
                    aroma_label_set_text(s_prop_dropdown_widgets[widget_counter],
                                         AROMA_ICON_ARROW_DROP_DOWN);
                }
            }
            widget_counter++;
        }
    }

                                    
    int desc_y = cy + 10;
    if (desc_y < offset_y + canvas_h - 40)
    {
        text(canvas, "Description:", offset_x + PANEL_X_MARGIN, desc_y,
             C_TEXT_DIM, ui10);
        text(canvas, "Sets the widget's properties.",
             offset_x + PANEL_X_MARGIN, desc_y + 16, C_TEXT_FAINT, ui10);
    }
}

static void project_show_in_sidebar(void)
{
    s_sidebar_mode = 3;
    draw_rail();
    draw_sidebar();
}

static void run_project_task(int index)
{
    switch (index)
    {
    case 0:
        project_build();
        break;
    case 1:
        project_run();
        break;
    case 2:
        project_clean();
        break;
    default:
        s_panel_tab = 2;
        if (!s_panel_visible)
            set_panel_visible(true);
        else
            draw_panel();
        toast("Embedded Aroma language server is ready");
        break;
    }
}

static void run_npm_script(int index)
{
    static const char *const scripts[4] = {"dev", "build", "preview", "tauri"};
    if (index < 0 || index > 3)
        return;
    log_add("terminal/run", "$ npm run %s", scripts[index]);
    s_panel_tab = 3;
    if (!s_panel_visible)
        set_panel_visible(true);
    else
        draw_panel();
    toast("Running npm run %s", scripts[index]);
}

static bool sidebar_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    debug_click("sidebar", event);
    int sx = s_sidebar_detached ? 0 : SIDEBAR_X;

                                                                        
                                                                              
    if (s_sidebar_detached && s_sidebar_detached_canvas)
        aroma_ui_set_focused_node(s_sidebar_detached_canvas);

    if (x < sx || x >= sx + s_sidebar_w)
        return true;

    if (sidebar_detach_hit(x, y))
    {
        s_sidebar_detach_hover = false;
        sidebar_toggle_detach();
        return true;
    }

    int action = sidebar_action_hit(x, y);
    if (action >= 0)
    {
        if (s_sidebar_mode == 0 && action == 0)
            open_dialog(IDE_DIALOG_NEW_FILE, "");
        else if ((s_sidebar_mode == 0 && action == 1) ||
                 (s_sidebar_mode != 0 && action == 0))
            refresh_workspace();
        else
        {
            bool any_expanded = false;
            for (int j = 0; j < s_tree_node_count; j++)
            {
                if (s_tree_nodes[j].is_dir && s_tree_nodes[j].expanded)
                    any_expanded = true;
            }
            tree_toggle_all(!any_expanded);
            draw_sidebar();
        }
        return true;
    }

    if (s_sidebar_mode == 0 && y >= 96)
    {
        int row = (y - TREE_TOP) / TREE_ROW_HEIGHT + s_tree_scroll;
        if (row >= 0 && row < s_tree_row_count &&
            y < TREE_TOP + tree_row_capacity() * TREE_ROW_HEIGHT)
        {
            IdeTreeItem *node = &s_tree_nodes[s_tree_rows[row]];
            if (node->is_dir)
            {
                node->expanded = !node->expanded;
                tree_rebuild_rows();
                draw_sidebar();
            }
            else if (node->file_index >= 0)
            {
                ide_open_file(node->file_index, false);
            }
            return true;
        }
        for (int i = 0; i < 4; i++)
        {
            if (s_script_y[i] >= 0 && y >= s_script_y[i] - 2 &&
                y < s_script_y[i] + 20)
            {
                run_project_task(i);
                return true;
            }
        }
    }
    else if (s_sidebar_mode == 1)
    {
        if (y >= 94 && y < 124 && x >= sx + 10 && x < sx + s_sidebar_w - 10)
        {
            open_dialog(IDE_DIALOG_SEARCH, s_search_query);
            return true;
        }
        for (int i = 0; i < s_search_hit_count; i++)
        {
            if (s_search_y[i] >= 0 && y >= s_search_y[i] - 4 &&
                y < s_search_y[i] + 34)
            {
                open_search_hit(i);
                return true;
            }
        }
    }
    else if (s_sidebar_mode == 2)
    {
        if (y >= 118 && y < 150)
        {
            int saved = save_all_files_ex(true);
            log_add("workspace/applyEdit", "Saved %d modified files to disk", saved);
            draw_rail();
            toast("Saved %d modified files", saved);
            return true;
        }
        if (y >= 158 && y < 188)
        {
            refresh_workspace();
            return true;
        }
        for (int i = 0; i < 3; i++)
        {
            if (s_change_y[i] >= 0 && y >= s_change_y[i] - 3 &&
                y < s_change_y[i] + 21)
            {
                if (s_scm_files[i] >= 0)
                    ide_open_file(s_scm_files[i], false);
                return true;
            }
        }
    }
    else if (s_sidebar_mode == 3)
    {
        if (y >= 94 && y < 130)
        {
            run_project_task(0);
            return true;
        }
        if (y >= 142 && y < 166)
        {
            run_project_task(1);
            return true;
        }
        for (int i = 0; i < 4; i++)
        {
            if (s_run_y[i] >= 0 && y >= s_run_y[i] - 3 && y < s_run_y[i] + 21)
            {
                run_project_task(i);
                return true;
            }
        }
    }
    else if (s_sidebar_mode == 4)
    {
        if (settings_on_click(x, y))
            return true;
    }
    return true;
}

static bool tabs_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hover = -1;
    bool close_hover = false;
    int width = tab_width();
    if (y >= TITLE_H && y < TITLE_H + TAB_H && x >= EDITOR_X && x < right_x)
    {
        for (int i = 0; i < s_tab_count; i++)
        {
            int left = EDITOR_X + i * width;
            if (x >= left && x < left + width)
            {
                hover = i;
                close_hover = x >= left + width - 27;
                break;
            }
        }
    }
    if (hover != s_tab_hover || close_hover != s_tab_close_hover)
    {
        s_tab_hover = hover;
        s_tab_close_hover = close_hover;
        draw_tabs();
    }
    return true;
}

static bool tabs_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_tab_hover != -1 || s_tab_close_hover)
    {
        s_tab_hover = -1;
        s_tab_close_hover = false;
        draw_tabs();
    }
    return true;
}

static bool tabs_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int width = tab_width();
    if (y < TITLE_H || y >= TITLE_H + TAB_H)
        return true;
    for (int i = 0; i < s_tab_count; i++)
    {
        int left = EDITOR_X + i * width;
        if (x >= left && x < left + width)
        {
            if (x >= left + width - 27)
                close_tab(i);
            else
                ide_open_file(s_tabs[i], false);
            return true;
        }
    }

    if (x >= right_x - 70 && x < right_x - 40)
    {
        preview_toggle();
        return true;
    }
    if (x >= right_x - 40 && x < right_x - 23)
    {
        set_panel_visible(!s_panel_visible);
        return true;
    }
    if (x >= right_x - 23 && x <= right_x)
    {
        s_menu_index = 3;
        s_menu_open = true;
        s_menu_hover = -1;
        aroma_node_set_hidden(s_menu_canvas, false);
        draw_menu();
        draw_title();
        return true;
    }
    return true;
}

static bool breadcrumb_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    if (s_designer_available && s_active_file >= 0 &&
        designer_is_markup_path(ide_files[s_active_file].path) &&
        x >= right_x - 104)
    {
        designer_set_visible(!s_designer_visible);
        return true;
    }
    if (s_active_file >= 0)
        toast("Breadcrumb: %s", ide_files[s_active_file].path);
    return true;
}

static int outline_row_at(int x, int y)
{
    if (x < right_x || x >= win_w)
        return -1;
    if (y < OUTLINE_ROWS_TOP || y >= outline_rows_bottom())
        return -1;
    int row = s_outline_scroll + (y - OUTLINE_ROWS_TOP) / OUTLINE_ROW_H;
    if (row < 0 || row >= outline_row_count())
        return -1;
    return row;
}

static bool outline_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (event->event_type == EVENT_TYPE_MOUSE_SCROLL)
    {
        float steps = event->data.mouse.scroll_y;
        if (steps > -1.0f && steps < 1.0f)
            steps = steps < 0.0f ? -1.0f : (steps > 0.0f ? 1.0f : 0.0f);
        s_outline_scroll -= (int)steps * 3;
        draw_outline();
        return true;
    }
    int hover = outline_row_at(event->data.mouse.x, event->data.mouse.y);
    if (hover != s_outline_hover)
    {
        s_outline_hover = hover;
        draw_outline();
    }
    return true;
}

static bool outline_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_outline_hover != -1)
    {
        s_outline_hover = -1;
        draw_outline();
    }
    return true;
}

static bool outline_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int row = outline_row_at(event->data.mouse.x, event->data.mouse.y);
    if (row < 0)
        return true;

    if (s_designer_visible)
    {
        if (row >= 0 && row < s_design_doc.node_count)
        {
            s_design_doc.selected = row;
            s_designer_drag_node = -1;
            s_designer_drag_palette = -1;
            s_designer_resizing = false;
            s_designer_resize_node = -1;
            s_designer_drag_moved = false;
            designer_draw_all();
            draw_outline();
        }
        return true;
    }

    AromaCodeSymbol *symbol = &s_outline[row];
    aroma_code_editor_goto_line_col(s_editor, symbol->line, symbol->column);
    aroma_code_editor_focus(s_editor);
    log_add("textDocument/documentSymbol", "Revealed %s at %d:%d",
            symbol->name, symbol->line, symbol->column);
    draw_breadcrumb();
    draw_status();
    return true;
}

static int panel_tab_at(int x, int y, int py, int px)
{
    if (y < py + 4 || y >= py + PANEL_HEADER_H)
        return -1;
    for (int i = 0; i < 4; i++)
    {
        int tab_x = px + 8 + i * 110;
        int next_tab_x = px + 8 + (i + 1) * 110;
        if (x >= tab_x - 6 && x < next_tab_x - 8)
            return i;
    }
    return -1;
}

static bool panel_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int py = s_panel_detached ? 0 : panel_y;
    int px = s_panel_detached ? 0 : SIDEBAR_X;
    int pw = s_panel_detached ? s_panel_detached_w : panel_w;
    int right_x = px + (pw > 0 ? pw : panel_w);
    int old_tab = s_panel_tab_hover;
    int old_icon = s_panel_icon_hover;
    int old_problem = s_panel_problem_hover;
    s_panel_tab_hover = panel_tab_at(x, y, py, px);
    s_panel_icon_hover = -1;
    s_panel_problem_hover = -1;
    if (y >= py + 4 && y < py + PANEL_HEADER_H)
    {
        if (x >= right_x - 58 && x < right_x - 34)
            s_panel_icon_hover = 1;
        else if (x >= right_x - 34 && x < right_x)
            s_panel_icon_hover = 2;
    }
    if (s_panel_tab == 0 && s_panel_visible)
    {
        build_problem_list();
        int shown = s_problem_total > PANEL_VISIBLE_ROWS ? PANEL_VISIBLE_ROWS
                                                         : s_problem_total;
        int content_start = py + PANEL_HEADER_H + 8;
        for (int i = 0; i < shown; i++)
        {
            int row_y = content_start + i * 21;
            if (y >= row_y - 2 && y < row_y + 19)
            {
                s_panel_problem_hover = i;
                break;
            }
        }
    }
    if (old_tab != s_panel_tab_hover || old_icon != s_panel_icon_hover ||
        old_problem != s_panel_problem_hover)
        draw_panel();
    return true;
}

static bool panel_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_panel_tab_hover != -1 || s_panel_icon_hover != -1 ||
        s_panel_problem_hover != -1)
    {
        s_panel_tab_hover = -1;
        s_panel_icon_hover = -1;
        s_panel_problem_hover = -1;
        draw_panel();
    }
    return true;
}

static bool panel_on_key(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;
    uint32_t key = event->data.key.key_code;
    if (key == 27 || key == 0xFF1B)                         
    {
        if (s_panel_detached)
            panel_attach();
        else
            set_panel_visible(false);
        return true;
    }
    return false;
}

static bool panel_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int py = s_panel_detached ? 0 : panel_y;
    int px = s_panel_detached ? 0 : SIDEBAR_X;
    int pw = s_panel_detached ? s_panel_detached_w : panel_w;
    int ph = s_panel_detached ? s_panel_detached_h : panel_h;
    int right_x = px + (pw > 0 ? pw : panel_w);
    if (y < py || y >= py + (ph > 0 ? ph : panel_h))
        return true;
    if (y < py + 4)
        return true;                          

                                                                      
                                                                              
    if (s_panel_detached && s_panel_detached_canvas)
        aroma_ui_set_focused_node(s_panel_detached_canvas);

    if (y < py + PANEL_HEADER_H)
    {
        if (x >= right_x - 34 && x < right_x)
        {
            panel_toggle_detach();
            return true;
        }
        if (x >= right_x - 58 && x < right_x - 34)
        {
            draw_panel();
            toast("Panel refreshed");
            return true;
        }
        int tab = panel_tab_at(x, y, py, px);
        if (tab >= 0)
        {
            s_panel_tab = tab;
            draw_panel();
            return true;
        }
    }
    if (s_panel_tab == 0)
    {
        build_problem_list();
        int shown = s_problem_total > PANEL_VISIBLE_ROWS ? PANEL_VISIBLE_ROWS
                                                         : s_problem_total;
        int content_start = py + PANEL_HEADER_H + 8;
        for (int i = 0; i < shown; i++)
        {
            int row_y = content_start + i * 21;
            if (y >= row_y - 2 && y < row_y + 19)
            {
                activate_problem(i);
                return true;
            }
        }
    }
    return true;
}

static bool sidebar_resize_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_resizing_sidebar || s_sidebar_detached)
        return true;
    int x = event->data.mouse.x;
    int new_w = x - SIDEBAR_X;
    if (new_w < 160)
        new_w = 160;
    if (new_w > 400)
        new_w = 400;
    s_sidebar_w = new_w;
    ide_layout_recalc();
    ide_sync_layout_nodes();
    if (s_editor)
    {
        int ed_w = s_markdown_split ? editor_w / 2 : editor_w;
        aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y,
                                     ed_w, editor_h);
    }
    markdown_preview_fit();
    aroma_node_invalidate(s_editor_bg);
    aroma_node_invalidate(s_panel_bg);
    aroma_node_invalidate(s_panel_canvas);
    draw_all();
    return true;
}

static bool sidebar_resize_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (s_sidebar_detached)
        return true;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hx = SIDEBAR_X + s_sidebar_w;
    if (x >= hx - 4 && x < hx + 4 && y >= TITLE_H && y < win_h - STATUS_H)
    {
                                                                           
        if (y >= panel_y && y < panel_y + PANEL_HEADER_H && x >= SIDEBAR_X)
            return true;
        s_resizing_sidebar = !s_resizing_sidebar;
    }
    return true;
}

static bool panel_resize_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_resizing_panel || s_panel_detached)
        return true;
    int y = event->data.mouse.y;
    int new_h = status_y - y;
    if (new_h < 120)
        new_h = 120;
    if (new_h > win_h - EDITOR_Y - STATUS_H - 100)
        new_h = win_h - EDITOR_Y - STATUS_H - 100;
    s_panel_h_resizable = new_h;
    ide_layout_recalc();
    ide_sync_layout_nodes();
    if (s_editor)
    {
        int ed_w = s_markdown_split ? editor_w / 2 : editor_w;
        aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y,
                                     ed_w, editor_h);
    }
    markdown_preview_fit();
    aroma_node_invalidate(s_editor_bg);
    aroma_node_invalidate(s_panel_bg);
    aroma_node_invalidate(s_panel_canvas);
    draw_all();
    return true;
}

static bool panel_resize_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (s_panel_detached)
        return false;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (y >= panel_y - 4 && y < panel_y + 4 && x >= SIDEBAR_X &&
        x < right_x)
    {
        s_resizing_panel = !s_resizing_panel;
        return true;
    }
    return false;
}

static bool settings_editor_bg_on_click(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
                                                
    return false;
}

static bool status_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hover = 0;
    if (y >= status_y && x >= win_w - 70 && x < win_w - 30)
        hover = 1;
    if (hover != s_status_hover)
    {
        s_status_hover = hover;
        draw_status();
    }
    return true;
}

static bool status_on_exit(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    if (s_status_hover != 0)
    {
        s_status_hover = 0;
        draw_status();
    }
    return true;
}

static bool status_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (y < status_y)
        return true;
    if (x >= win_w - 70 && x < win_w - 30)
    {
        set_panel_visible(!s_panel_visible);
        return true;
    }
    if (x < 152)
    {
        s_panel_tab = 0;
        if (!s_panel_visible)
            set_panel_visible(true);
        draw_panel();
        return true;
    }
    if (x < 206)
    {
        s_panel_tab = 0;
        if (!s_panel_visible)
            set_panel_visible(true);
        draw_panel();
        return true;
    }
    if (x >= win_w - 380 && x < win_w - 275)
    {
        toast("Language: %s", s_active_file >= 0
                                  ? language_label(ide_files[s_active_file].language)
                                  : "AromaUI");
        return true;
    }
    if (x >= win_w - 32)
    {
        s_panel_tab = 1;
        if (!s_panel_visible)
            set_panel_visible(true);
        else
            draw_panel();
        toast("Activity log opened");
    }
    return true;
}

static bool menu_on_move(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!s_menu_open)
        return true;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    int hover = -1;
    if (x >= s_menu_panel_x && x < s_menu_panel_x + MENU_panel_w)
    {
        for (int i = 0; i < k_menu_item_counts[s_menu_index]; i++)
        {
            int item_y = menu_item_y(i);
            if (y >= item_y && y < item_y + MENU_ITEM_H)
            {
                hover = i;
                break;
            }
        }
    }
    if (hover != s_menu_hover)
    {
        s_menu_hover = hover;
        draw_menu();
    }
    return true;
}

static void close_menu(void)
{
    if (!s_menu_open)
        return;
    s_menu_open = false;
    s_menu_hover = -1;
    if (s_menu_canvas)
        aroma_node_set_hidden(s_menu_canvas, true);
    draw_menu();
    draw_title();
}

static void menu_show_panel(int tab)
{
    s_panel_tab = tab;
    if (!s_panel_visible)
        set_panel_visible(true);
    else
        draw_panel();
    draw_title();
}

static void ide_open_file_via_nfd(void)
{
    char *selected = NULL;
    AromaFileDialogResult result = aroma_file_dialog_open(
        "Open a file in the current project",
        "Aroma and C files:aroma,c,h,md,json;All files:*",
        ide_workspace_root, &selected);
    if (result == AROMA_FILE_DIALOG_CANCEL)
    {
        aroma_file_dialog_free(selected);
        return;
    }
    if (result == AROMA_FILE_DIALOG_ERROR)
    {
        toast("%s", aroma_file_dialog_last_error());
        aroma_file_dialog_free(selected);
        open_dialog(IDE_DIALOG_OPEN_FILE, "");
        return;
    }
    if (!selected)
        return;
    size_t root_length = strlen(ide_workspace_root);
    bool inside = strncmp(selected, ide_workspace_root, root_length) == 0 &&
                  selected[root_length] == '/';
    if (!inside)
    {
        toast("Choose a file inside the open project");
        aroma_file_dialog_free(selected);
        return;
    }
    const char *relative = selected + root_length + 1;
    int index = ide_workspace_file_by_path(relative);
    if (index < 0)
    {
        refresh_workspace();
        index = ide_workspace_file_by_path(relative);
    }
    if (index >= 0)
    {
        ide_open_file(index, false);
        draw_all();
    }
    else
    {
        toast("The workspace could not index %s", relative);
    }
    aroma_file_dialog_free(selected);
}

static void ide_open_project_via_nfd(void)
{
    char *selected = NULL;
    AromaFileDialogResult result = aroma_file_dialog_pick_folder(
        "Open an Aroma project", ide_workspace_root, &selected);
    if (result == AROMA_FILE_DIALOG_CANCEL)
    {
        aroma_file_dialog_free(selected);
        return;
    }
    if (result == AROMA_FILE_DIALOG_ERROR)
    {
        toast("%s", aroma_file_dialog_last_error());
        aroma_file_dialog_free(selected);
        open_dialog(IDE_DIALOG_OPEN_PROJECT, ide_workspace_root);
        return;
    }
    if (!selected)
        return;
    char project_file[PROJECT_PATH_MAX];
    snprintf(project_file, sizeof(project_file), "%s/aroma.json", selected);
    if (access(project_file, R_OK) != 0)
        toast("No aroma.json found in the selected folder");
    else if (project_open(selected))
        draw_all();
    aroma_file_dialog_free(selected);
}

static void menu_execute_action(IdeMenuAction action)
{
    switch (action)
    {
    case IDE_ACTION_NEW_FILE:
        open_dialog(IDE_DIALOG_NEW_FILE, "");
        break;
    case IDE_ACTION_OPEN_FILE:
        ide_open_file_via_nfd();
        break;
    case IDE_ACTION_SAVE:
        if (s_active_file >= 0)
            save_file(s_active_file);
        break;
    case IDE_ACTION_SAVE_ALL:
    {
        int saved = save_all_files_ex(true);
        toast("Saved %d file%s", saved, saved == 1 ? "" : "s");
        break;
    }
    case IDE_ACTION_CLOSE_EDITOR:
        for (int i = 0; i < s_tab_count; i++)
        {
            if (s_tabs[i] == s_active_file)
            {
                close_tab(i);
                break;
            }
        }
        break;
    case IDE_ACTION_NEW_PROJECT:
        open_dialog(IDE_DIALOG_NEW_PROJECT, "");
        break;
    case IDE_ACTION_OPEN_PROJECT:
        ide_open_project_via_nfd();
        break;
    case IDE_ACTION_BUILD:
        project_build();
        break;
    case IDE_ACTION_RUN:
        project_run();
        break;
    case IDE_ACTION_CLEAN:
        project_clean();
        break;
    case IDE_ACTION_TARGET_LINUX:
        project_set_target(0);
        break;
    case IDE_ACTION_TARGET_ANDROID:
        project_set_target(1);
        break;
    case IDE_ACTION_ANDROID_DEVICE_NEXT:
        project_android_next_device();
        break;
    case IDE_ACTION_ANDROID_DEVICE_MANAGER:
        open_android_device_manager();
        break;
    case IDE_ACTION_ANDROID_REFRESH:
        project_android_refresh_devices(true);
        break;
    case IDE_ACTION_ANDROID_SETUP:
        project_android_setup();
        break;
    case IDE_ACTION_EXIT:
        aroma_ui_request_exit();
        break;
    case IDE_ACTION_UNDO:
        if (s_designer_visible)
            designer_history_undo();
        else if (s_editor)
            aroma_code_editor_undo(s_editor);
        break;
    case IDE_ACTION_REDO:
        if (s_designer_visible)
            designer_history_redo();
        else if (s_editor)
            aroma_code_editor_redo(s_editor);
        break;
    case IDE_ACTION_CUT:
        if (s_editor)
            aroma_code_editor_cut(s_editor);
        break;
    case IDE_ACTION_COPY:
        if (s_editor)
            aroma_code_editor_copy(s_editor);
        break;
    case IDE_ACTION_PASTE:
        if (s_editor)
            aroma_code_editor_paste(s_editor);
        break;
    case IDE_ACTION_SELECT_ALL:
        if (s_editor)
            aroma_code_editor_select_all(s_editor);
        break;
    case IDE_ACTION_FIND:
        if (s_editor)
        {
            aroma_code_editor_focus(s_editor);
            aroma_code_editor_show_find(s_editor);
        }
        break;
    case IDE_ACTION_DEFINITION:
        if (s_editor && s_active_file >= 0)
        {
            int line = 1, column = 1;
            aroma_code_editor_get_cursor_position(s_editor, &line, &column);
            editor_on_definition(s_editor, line, column, NULL);
        }
        break;
    case IDE_ACTION_TOGGLE_SIDEBAR:
        s_sidebar_mode = 0;
        s_sidebar_row_hover = -1;
        draw_rail();
        draw_sidebar();
        break;
    case IDE_ACTION_TOGGLE_PANEL:
        set_panel_visible(!s_panel_visible);
        break;
    case IDE_ACTION_TOGGLE_PANEL_DETACH:
        panel_toggle_detach();
        break;
    case IDE_ACTION_TOGGLE_SIDEBAR_DETACH:
        sidebar_toggle_detach();
        break;
    case IDE_ACTION_SHOW_PROBLEMS:
        menu_show_panel(0);
        break;
    case IDE_ACTION_SHOW_OUTPUT:
        menu_show_panel(1);
        break;
    case IDE_ACTION_SHOW_TRACE:
        menu_show_panel(2);
        break;
    case IDE_ACTION_TOGGLE_PREVIEW:
        preview_toggle();
        break;
    case IDE_ACTION_MARKDOWN_SPLIT:
        markdown_preview_split_toggle();
        break;
    case IDE_ACTION_SEARCH_WORKSPACE:
        open_dialog(IDE_DIALOG_SEARCH, s_search_query);
        break;
    case IDE_ACTION_NEXT_PROBLEM:
    {
        static int problem_cursor;
        build_problem_list();
        if (s_problem_total > 0)
        {
            if (problem_cursor >= s_problem_total)
                problem_cursor = 0;
            activate_problem(problem_cursor++);
        }
        else
        {
            toast("No problems in this workspace");
        }
        break;
    }
    case IDE_ACTION_LANGUAGE_SERVER:
        menu_show_panel(2);
        toast("Embedded Aroma language server is ready");
        break;
    case IDE_ACTION_ABOUT:
        toast("AromaUI Studio 0.1.0  ·  %s", ide_workspace_root);
        break;
    case IDE_ACTION_SHORTCUTS:
        toast("Ctrl+P open · Ctrl+S save · F7 build · F5 run · F12 definition");
        break;
    case IDE_ACTION_OPEN_README:
    {
        int index = ide_workspace_file_by_path("README.md");
        if (index < 0)
            index = ide_workspace_file_by_path("readme.md");
        if (index >= 0)
        {
            ide_open_file(index, false);
            toast("Opened README.md");
        }
        else
        {
            toast("This project has no README.md");
        }
        break;
    }
    case IDE_ACTION_NONE:
    default:
        break;
    }
}

static bool menu_on_click(AromaEvent *event, void *user_data)
{
    (void)user_data;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;

                                                                             
                                                                               
    int title_menu = title_menu_hit(x, y);
    if (title_menu >= 0 && title_menu != s_menu_index)
    {
        s_menu_index = title_menu;
        s_menu_hover = -1;
        draw_menu();
        draw_title();
        return true;
    }

    int item = -1;
    if (x >= s_menu_panel_x && x < s_menu_panel_x + MENU_panel_w)
    {
        for (int i = 0; i < k_menu_item_counts[s_menu_index]; i++)
        {
            int item_y = menu_item_y(i);
            if (y >= item_y && y < item_y + MENU_ITEM_H)
            {
                item = i;
                break;
            }
        }
    }
    IdeMenuAction action = IDE_ACTION_NONE;
    if (item >= 0)
        action = k_menu_items[s_menu_index][item].action;
    close_menu();
    if (action != IDE_ACTION_NONE)
        menu_execute_action(action);
    return true;
}

static bool toast_on_click(AromaEvent *event, void *user_data)
{
    (void)event;
    (void)user_data;
    hide_toast();
    return true;
}

static void editor_flush_pending(bool force)
{
    if (!s_edit_pending)
        return;
    uint64_t now = now_ms();
    if (!force && now - s_edit_last_ms < EDIT_FLUSH_IDLE_MS &&
        now - s_edit_first_ms < EDIT_FLUSH_MAX_MS)
        return;
    s_edit_pending = false;

    int index = s_edit_pending_file;
    if (index < 0 || index >= ide_file_count)
        return;
    IdeFile *file = &ide_files[index];
    ide_lsp_refresh_diagnostics(index);
    if (index != s_active_file)
        return;                                                              

    if (s_editor)
        aroma_code_editor_set_diagnostics(s_editor, file->diagnostics,
                                          file->diagnostic_count);
    refresh_outline();
    draw_tabs();
    draw_sidebar();
    draw_status();
    if (s_panel_visible && s_panel_tab == 0)
        draw_panel();

    if (now - s_last_change_log > 700u)
    {
        s_last_change_log = now;
        log_add("textDocument/didChange", "%s  ·  %zu bytes  ·  %d diagnostics",
                file->name, file->text ? strlen(file->text) : (size_t)0,
                file->diagnostic_count);
        if (s_panel_visible && s_panel_tab == 2)
            draw_panel();
    }
}

static void editor_on_change(AromaNode *node, const char *text_value, void *user_data)
{
    (void)node;
    (void)user_data;
    if (s_designer_committing)
        return;
    if (s_active_file < 0 || s_active_file >= ide_file_count)
        return;
    IdeFile *file = &ide_files[s_active_file];
    const char *source = text_value ? text_value : "";
    size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1);
    if (!copy)
        return;
    memcpy(copy, source, length + 1);
    free(file->text);
    file->text = copy;
    file->dirty = true;

                                                                             
    if (s_edit_pending && s_edit_pending_file != s_active_file)
        editor_flush_pending(true);
    uint64_t now = now_ms();
    if (!s_edit_pending)
        s_edit_first_ms = now;
    s_edit_pending = true;
    s_edit_pending_file = s_active_file;
    s_edit_last_ms = now;
}

static void editor_on_cursor(AromaNode *node, int line, int column,
                             int selection_chars, void *user_data)
{
    (void)node;
    (void)user_data;
    s_line = line;
    s_column = column;
    s_selection_chars = selection_chars;
    draw_status();
}

static bool editor_on_save(AromaNode *node, const char *text_value, void *user_data)
{
    (void)node;
    (void)text_value;
    (void)user_data;
    if (s_active_file < 0)
        return false;
    save_file(s_active_file);
    if (s_preview_visible && s_active_file >= 0 &&
        strstr(ide_files[s_active_file].name, ".aroma") != NULL)
        preview_window_create();
    return true;
}

static void editor_on_completion(AromaNode *node, int line, int column,
                                 void *user_data)
{
    (void)user_data;
    if (s_active_file < 0)
        return;
    AromaCodeCompletion completions[AROMA_CODE_EDITOR_MAX_COMPLETIONS];
    int count = ide_lsp_complete(s_active_file, line, column, completions,
                                 AROMA_CODE_EDITOR_MAX_COMPLETIONS);
    if (count > 0)
        aroma_code_editor_set_completions(node, completions, count);
    else
        aroma_code_editor_hide_completions(node);

    uint64_t now = now_ms();
    if (count > 0 && now - s_last_completion_log > 260u)
    {
        s_last_completion_log = now;
        log_add("textDocument/completion", "%d items for %s at %d:%d", count,
                ide_files[s_active_file].name, line, column);
        if (s_panel_visible && s_panel_tab == 2)
            draw_panel();
    }
}

static void editor_on_hover(AromaNode *node, int line, int column, void *user_data)
{
    (void)user_data;
    if (s_active_file < 0)
        return;
    char hover[1024];
    if (ide_lsp_hover(s_active_file, line, column, hover, sizeof(hover)))
    {
        aroma_code_editor_set_hover(node, hover, line, column);
        log_add("textDocument/hover", "%s:%d:%d  ·  %s",
                ide_files[s_active_file].name, line, column,
                hover[0] ? "resolved" : "empty");
        if (s_panel_visible && s_panel_tab == 2)
            draw_panel();
    }
    else
    {
        aroma_code_editor_hide_hover(node);
    }
}

static void editor_on_definition(AromaNode *node, int line, int column,
                                 void *user_data)
{
    (void)node;
    (void)user_data;
    if (s_active_file < 0)
        return;
    int target_file = -1;
    int target_line = 0;
    int target_column = 0;
    char symbol[96];
    symbol[0] = '\0';
    if (ide_lsp_definition(s_active_file, line, column, &target_file, &target_line,
                           &target_column, symbol, sizeof(symbol)))
    {
        s_pending_definition = true;
        s_pending_file = target_file;
        s_pending_line = target_line;
        s_pending_column = target_column;
        snprintf(s_pending_symbol, sizeof(s_pending_symbol), "%s", symbol);
        log_add("textDocument/definition", "%s -> %s:%d:%d (requested)",
                symbol[0] ? symbol : "symbol", ide_files[target_file].path,
                target_line, target_column);
        if (s_panel_visible && s_panel_tab == 2)
            draw_panel();
    }
    else
    {
        log_add("textDocument/definition", "No definition found at %s:%d:%d",
                ide_files[s_active_file].name, line, column);
        toast("No definition found for this symbol");
    }
}

static bool ide_global_key(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || event->event_type != EVENT_TYPE_KEY_PRESS)
        return false;

    uint32_t key = event->data.key.key_code;
    uint16_t mods = event->data.key.modifiers;
    bool ctrl = (mods & AROMA_KEY_MOD_CTRL) != 0;
    bool shift = (mods & AROMA_KEY_MOD_SHIFT) != 0;

                                                                             
                                 
    if (s_dialog_open && s_dialog_mode != IDE_DIALOG_PROPERTIES)
        return false;

                                                                            
                                                               
    if (s_dialog_open && s_dialog_mode == IDE_DIALOG_PROPERTIES &&
        aroma_ui_get_focused_node() == s_dialog_canvas)
        return props_on_key(key);

                                                                              
                                                                        
                                                                
    if (s_panel_detached && s_panel_detached_canvas &&
        aroma_ui_get_focused_node() == s_panel_detached_canvas)
    {
        panel_on_key(event, NULL);
        return true;
    }

                                        
    if (s_sidebar_detached && s_sidebar_detached_canvas &&
        aroma_ui_get_focused_node() == s_sidebar_detached_canvas)
    {
        if (key == 27 || key == 0xFF1B)                               
        {
            sidebar_attach();
            return true;
        }
        return true;
    }

                                                                          
                                                                             
                                                                              
                                                                              
                                                                             
                               
    bool is_enter = !ctrl && (key == 0xFF0D || key == 0xFF8D || key == 13 ||
                              key == 10);
    if (s_editor && is_enter && !s_designer_visible && !s_markdown_visible)
    {
        if (s_prop_edit_active || s_prop_dropdown_open)
        {
                                                   
        }
        else
        {
                                                                         
                                                                           
                                                                        
                                                                       
            if (event->target_node == s_editor &&
                aroma_ui_get_focused_node() == s_editor)
                return false;
            aroma_code_editor_hide_completions(s_editor);
            aroma_code_editor_insert_newline(s_editor);
            return true;
        }
    }

                               
    if (s_prop_edit_active || s_prop_dropdown_open)
    {
        if (s_editor && aroma_ui_get_focused_node() == s_editor)
        {
            s_prop_edit_active = false;
            s_prop_dropdown_open = false;
            draw_panel();
            aroma_code_editor_insert_newline(s_editor);
            return true;
        }
        else
        {
                                 
        if (s_prop_dropdown_open)
        {
            int opt_count = 0;
            const char *const *opts = prop_dropdown_options(
                s_design_doc.nodes[s_prop_dropdown_index].tag,
                k_prop_schema_all[s_prop_dropdown_row].key,
                &opt_count);

            if (key == 0xFF1B || key == 27)                             
            {
                s_prop_dropdown_open = false;
                ide_layout_recalc();
                ide_update_panel_rects();
                draw_panel();
                return true;
            }
            if (key == 0xFF0D || key == 13 || key == 10)                    
            {
                if (s_prop_dropdown_index >= 0 &&
                    s_prop_dropdown_index < s_design_doc.node_count &&
                    s_prop_dropdown_row < PROP_SCHEMA_COUNT &&
                    opts && opt_count > 0)
                {
                    int sel = s_prop_edit_row;
                    if (sel >= 0 && sel < opt_count)
                    {
                        ide_design_set_attr(
                            &s_design_doc, s_prop_dropdown_index,
                            k_prop_schema_all[s_prop_dropdown_row].key,
                            opts[sel], true);
                        designer_after_mutation();
                    }
                }
                s_prop_dropdown_open = false;
                ide_layout_recalc();
                ide_update_panel_rects();
                draw_panel();
                return true;
            }
            if (key == 0xFF52 || key == 0xFF54)                    
            {
                int step = key == 0xFF52 ? -1 : 1;
                int new_sel = s_prop_edit_row + step;
                if (new_sel < 0)
                    new_sel = 0;
                if (new_sel >= opt_count)
                    new_sel = opt_count - 1;
                s_prop_edit_row = new_sel;
                draw_panel();
                return true;
            }
        }

        if (key == 0xFF0D || key == 13 || key == 10)                   
        {
            if (s_prop_edit_index >= 0 &&
                s_prop_edit_index < s_design_doc.node_count &&
                s_prop_edit_row >= 0 && s_prop_edit_row < PROP_SCHEMA_COUNT)
            {
                ide_design_set_attr(&s_design_doc, s_prop_edit_index,
                                    k_prop_schema_all[s_prop_edit_row].key,
                                    s_prop_edit_buf, true);
                designer_after_mutation();
            }
            s_prop_edit_active = false;
            draw_panel();
            return true;
        }
        if (key == 0xFF1B || key == 27)                     
        {
            s_prop_edit_active = false;
            draw_panel();
            return true;
        }
        if (key == 0xFF08 || key == 127)                
        {
            int len = (int)strlen(s_prop_edit_buf);
            if (len > 0)
                s_prop_edit_buf[len - 1] = '\0';
            draw_panel();
            return true;
        }
        if (key >= 32 && key < 127 &&
            (int)strlen(s_prop_edit_buf) < (int)sizeof(s_prop_edit_buf) - 1)
        {
            int len = (int)strlen(s_prop_edit_buf);
            s_prop_edit_buf[len] = (char)key;
            s_prop_edit_buf[len + 1] = '\0';
            draw_panel();
            return true;
        }
        return true;
    }
                                                                        
                                                                
    if (s_editor && aroma_ui_get_focused_node() == s_editor)
    {
        s_prop_edit_active = false;
        s_prop_dropdown_open = false;
        draw_panel();
                                                              
    }
    else
    {
                                 
        if (s_prop_dropdown_open)
        {
            int opt_count = 0;
            const char *const *opts = prop_dropdown_options(
                s_design_doc.nodes[s_prop_dropdown_index].tag,
                k_prop_schema_all[s_prop_dropdown_row].key,
                &opt_count);

            if (key == 0xFF1B || key == 27)                             
            {
                s_prop_dropdown_open = false;
                ide_layout_recalc();
                ide_update_panel_rects();
                draw_panel();
                return true;
            }
            if (key == 0xFF0D || key == 13 || key == 10)                    
            {
                if (s_prop_dropdown_index >= 0 &&
                    s_prop_dropdown_index < s_design_doc.node_count &&
                    s_prop_dropdown_row < PROP_SCHEMA_COUNT &&
                    opts && opt_count > 0)
                {
                    int sel = s_prop_edit_row;
                    if (sel >= 0 && sel < opt_count)
                    {
                        ide_design_set_attr(
                            &s_design_doc, s_prop_dropdown_index,
                            k_prop_schema_all[s_prop_dropdown_row].key,
                            opts[sel], true);
                        designer_after_mutation();
                    }
                }
                s_prop_dropdown_open = false;
                ide_layout_recalc();
                ide_update_panel_rects();
                draw_panel();
                return true;
            }
            if (key == 0xFF52 || key == 0xFF54)                    
            {
                int step = key == 0xFF52 ? -1 : 1;
                int new_sel = s_prop_edit_row + step;
                if (new_sel < 0)
                    new_sel = 0;
                if (new_sel >= opt_count)
                    new_sel = opt_count - 1;
                s_prop_edit_row = new_sel;
                draw_panel();
                return true;
            }
        }

        if (key == 0xFF0D || key == 13 || key == 10)                   
        {
            if (s_prop_edit_index >= 0 &&
                s_prop_edit_index < s_design_doc.node_count &&
                s_prop_edit_row >= 0 && s_prop_edit_row < PROP_SCHEMA_COUNT)
            {
                ide_design_set_attr(&s_design_doc, s_prop_edit_index,
                                    k_prop_schema_all[s_prop_edit_row].key,
                                    s_prop_edit_buf, true);
                designer_after_mutation();
            }
            s_prop_edit_active = false;
            draw_panel();
            return true;
        }
        if (key == 0xFF1B || key == 27)                     
        {
            s_prop_edit_active = false;
            draw_panel();
            return true;
        }
        if (key == 0xFF08 || key == 127)                
        {
            int len = (int)strlen(s_prop_edit_buf);
            if (len > 0)
                s_prop_edit_buf[len - 1] = '\0';
            draw_panel();
            return true;
        }
        if (key >= 32 && key < 127 &&
            (int)strlen(s_prop_edit_buf) < (int)sizeof(s_prop_edit_buf) - 1)
        {
            int len = (int)strlen(s_prop_edit_buf);
            s_prop_edit_buf[len] = (char)key;
            s_prop_edit_buf[len + 1] = '\0';
            draw_panel();
            return true;
        }
        return true;
    }
    }

                          
    if (ctrl && !shift && key == 78)                          
    {
        open_dialog(IDE_DIALOG_NEW_PROJECT, "");
        return true;
    }
    if (ctrl && !shift && key == 79)                           
    {
        open_dialog(IDE_DIALOG_OPEN_FILE, "");
        return true;
    }
    if (ctrl && !shift && key == 83)                   
    {
        if (s_active_file >= 0)
            save_file(s_active_file);
        return true;
    }
    if (ctrl && shift && key == 83)                             
    {
        ide_workspace_save_all();
        draw_sidebar();
        draw_tabs();
        draw_status();
        toast("Saved all files");
        return true;
    }
    if (ctrl && !shift && key == 66)                    
    {
        project_build();
        return true;
    }
    if (ctrl && !shift && key == 82)                  
    {
        project_run();
        return true;
    }
    if (ctrl && !shift && key == 44)                       
    {
        if (s_settings_visible)
            settings_window_close();
        else
            settings_window_open();
        return true;
    }

    if (key == 0xFFC2)              
    {
        run_project_task(1);
        return true;
    }
    if (key == 0xFFC4)                
    {
        run_project_task(0);
        return true;
    }
    if (key == 0xFFC9)                            
    {
        if (s_editor && s_active_file >= 0)
        {
            int line = 1, column = 1;
            aroma_code_editor_get_cursor_position(s_editor, &line, &column);
            editor_on_definition(s_editor, line, column, NULL);
        }
        return true;
    }
    if (key == 0xFFC5)                       
    {
        menu_execute_action(IDE_ACTION_NEXT_PROBLEM);
        return true;
    }

    if (s_designer_visible)
    {
        if (!ctrl && key == 0xFFFF)             
        {
            designer_delete_selected();
            return true;
        }
        if (!ctrl && key >= 0xFF51 && key <= 0xFF54)
        {
            int step = shift ? 10 : 1;
            int dx = key == 0xFF51 ? -step : key == 0xFF53 ? step : 0;
            int dy = key == 0xFF52 ? -step : key == 0xFF54 ? step : 0;
            designer_nudge_selected(dx, dy);
            return true;
        }
        if (ctrl && !shift && key == 26)             
        {
            designer_history_undo();
            return true;
        }
        if (ctrl && ((!shift && key == 25) || (shift && key == 26)))           
        {
            designer_history_redo();
            return true;
        }
        if (ctrl && !shift && key == 4)             
        {
            designer_duplicate_selected();
            return true;
        }
        if (ctrl && !shift && key == 3)             
        {
            designer_copy_selected();
            return true;
        }
        if (ctrl && !shift && key == 22)             
        {
            designer_paste_selected();
            return true;
        }
    }

    if (!ctrl)
        return false;

    if (!shift && key == 2)                       
    {
        menu_execute_action(IDE_ACTION_TOGGLE_SIDEBAR);
        return true;
    }

    if (shift && key == 2)                          
    {
        run_project_task(0);
        return true;
    }
    if (shift && key == 6)                                     
    {
        open_dialog(IDE_DIALOG_SEARCH, s_search_query);
        return true;
    }
    if (shift && key == 14)                                
    {
        open_dialog(IDE_DIALOG_NEW_PROJECT, "");
        return true;
    }
    if (shift && key == 15)                                 
    {
        ide_open_project_via_nfd();
        return true;
    }
    if (shift && key == 19)                             
    {
        if (s_designer_visible)
            designer_apply_to_file();
        int saved = ide_workspace_save_all();
        update_scm_count();
        draw_tabs();
        draw_status();
        toast("Saved %d file%s", saved, saved == 1 ? "" : "s");
        return true;
    }
    if (shift && key == 16)                                    
    {
        open_dialog(IDE_DIALOG_COMMAND_PALETTE, "");
        return true;
    }
    if (shift && key == 10)                                        
    {
        panel_toggle_detach();
        return true;
    }

    switch (key)
    {
    case 14:             
        open_dialog(IDE_DIALOG_NEW_FILE, "");
        return true;
    case 15:             
        ide_open_file_via_nfd();
        return true;
    case 16:                              
        open_dialog(IDE_DIALOG_COMMAND_PALETTE, "");
        return true;
    case 19:             
        if (s_active_file >= 0)
            save_file(s_active_file);
        return true;
    case 23:             
        if (s_active_file >= 0)
            close_tab(s_active_file);
        return true;
    case 10:                           
    {
        bool show = !s_panel_visible || s_panel_tab != 1;
        s_panel_tab = 1;
        if (show)
        {
            if (!s_panel_visible)
                set_panel_visible(true);
            aroma_ui_set_focused_node(s_panel_canvas);
        }
        else
        {
            set_panel_visible(false);
            if (s_editor)
                aroma_code_editor_focus(s_editor);
        }
        draw_panel();
        return true;
    }
    default:
        break;
    }
    return false;
}

static void setup_events(void)
{
    subscribe_node(s_title_canvas, EVENT_TYPE_MOUSE_MOVE, title_on_move);
    subscribe_node(s_title_canvas, EVENT_TYPE_MOUSE_CLICK, title_on_click);
    subscribe_node(s_title_canvas, EVENT_TYPE_MOUSE_EXIT, title_on_exit);

    subscribe_node(s_rail_canvas, EVENT_TYPE_MOUSE_MOVE, rail_on_move);
    subscribe_node(s_rail_canvas, EVENT_TYPE_MOUSE_CLICK, rail_on_click);
    subscribe_node(s_rail_canvas, EVENT_TYPE_MOUSE_EXIT, rail_on_exit);

    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_MOVE, sidebar_on_move);
    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_CLICK, sidebar_on_click);
    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_EXIT, sidebar_on_exit);
    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_SCROLL, sidebar_on_scroll);

    subscribe_node(s_tabs_canvas, EVENT_TYPE_MOUSE_MOVE, tabs_on_move);
    subscribe_node(s_tabs_canvas, EVENT_TYPE_MOUSE_CLICK, tabs_on_click);
    subscribe_node(s_tabs_canvas, EVENT_TYPE_MOUSE_EXIT, tabs_on_exit);

    subscribe_node(s_breadcrumb_canvas, EVENT_TYPE_MOUSE_CLICK, breadcrumb_on_click);

    subscribe_node(s_designer_toolbar, EVENT_TYPE_MOUSE_MOVE,
                   designer_toolbar_on_move);
    subscribe_node(s_designer_toolbar, EVENT_TYPE_MOUSE_CLICK,
                   designer_toolbar_on_click);
    subscribe_node(s_designer_toolbar, EVENT_TYPE_MOUSE_EXIT,
                   designer_toolbar_on_exit);
    subscribe_node(s_designer_palette, EVENT_TYPE_MOUSE_MOVE,
                   designer_palette_on_move);
    subscribe_node(s_designer_palette, EVENT_TYPE_MOUSE_SCROLL,
                   designer_palette_on_move);
    subscribe_node(s_designer_palette, EVENT_TYPE_MOUSE_CLICK,
                   designer_palette_on_click);
    subscribe_node(s_designer_palette, EVENT_TYPE_MOUSE_EXIT,
                   designer_palette_on_exit);
    subscribe_node(s_designer_overlay, EVENT_TYPE_MOUSE_MOVE,
                   designer_canvas_on_move);
    subscribe_node(s_designer_overlay, EVENT_TYPE_MOUSE_CLICK,
                   designer_canvas_on_click);
    subscribe_node(s_designer_overlay, EVENT_TYPE_MOUSE_RELEASE,
                   designer_canvas_on_release);
    subscribe_node(s_designer_overlay, EVENT_TYPE_MOUSE_EXIT,
                   designer_canvas_on_exit);

    subscribe_node(s_panel_canvas, EVENT_TYPE_MOUSE_MOVE, panel_on_move);
    subscribe_node(s_panel_canvas, EVENT_TYPE_MOUSE_CLICK, panel_on_click);
    subscribe_node(s_panel_canvas, EVENT_TYPE_MOUSE_EXIT, panel_on_exit);
    subscribe_node(s_panel_canvas, EVENT_TYPE_KEY_PRESS, panel_on_key);

    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_MOVE, sidebar_resize_on_move);
    subscribe_node(s_sidebar_canvas, EVENT_TYPE_MOUSE_CLICK, sidebar_resize_on_click);

    subscribe_node(s_outline_canvas, EVENT_TYPE_MOUSE_MOVE, outline_on_move);
    subscribe_node(s_outline_canvas, EVENT_TYPE_MOUSE_CLICK, outline_on_click);
    subscribe_node(s_outline_canvas, EVENT_TYPE_MOUSE_EXIT, outline_on_exit);

    subscribe_node(s_editor_bg, EVENT_TYPE_MOUSE_MOVE, panel_resize_on_move);
    subscribe_node(s_editor_bg, EVENT_TYPE_MOUSE_CLICK, panel_resize_on_click);
    subscribe_node(s_editor_bg, EVENT_TYPE_MOUSE_CLICK, settings_editor_bg_on_click);

    subscribe_node(s_settings_canvas, EVENT_TYPE_MOUSE_MOVE, panel_resize_on_move);
    subscribe_node(s_settings_canvas, EVENT_TYPE_MOUSE_CLICK, panel_resize_on_click);
    subscribe_node(s_settings_canvas, EVENT_TYPE_MOUSE_CLICK,
                   settings_editor_bg_on_click);

    subscribe_node(s_status_canvas, EVENT_TYPE_MOUSE_MOVE, status_on_move);
    subscribe_node(s_status_canvas, EVENT_TYPE_MOUSE_CLICK, status_on_click);
    subscribe_node(s_status_canvas, EVENT_TYPE_MOUSE_EXIT, status_on_exit);

    subscribe_node(s_menu_canvas, EVENT_TYPE_MOUSE_MOVE, menu_on_move);
    subscribe_node(s_menu_canvas, EVENT_TYPE_MOUSE_CLICK, menu_on_click);

    subscribe_node(s_toast_canvas, EVENT_TYPE_MOUSE_CLICK, toast_on_click);

    subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_CLICK, dialog_on_click);
    subscribe_node(s_dialog_canvas, EVENT_TYPE_MOUSE_SCROLL, props_on_scroll);
    subscribe_node(s_dialog_canvas, EVENT_TYPE_KEY_PRESS, dialog_on_key);
}

static void cache_nodes(IncenseRegistry *registry)
{
    s_root = find_widget(registry, "ide_root");
    s_title_canvas = find_widget(registry, "title_canvas");
    s_rail_canvas = find_widget(registry, "rail_canvas");
    s_sidebar_canvas = find_widget(registry, "sidebar_canvas");
    s_tabs_canvas = find_widget(registry, "tabs_canvas");
    s_breadcrumb_canvas = find_widget(registry, "breadcrumb_canvas");
    s_outline_canvas = find_widget(registry, "outline_canvas");
    s_panel_canvas = find_widget(registry, "panel_canvas");
    s_status_canvas = find_widget(registry, "status_canvas");
    s_editor_bg = find_widget(registry, "editor_bg");
    s_panel_bg = find_widget(registry, "panel_bg");

                          
    s_card_title_bg = find_widget(registry, "title_bg");
    s_card_rail_bg = find_widget(registry, "rail_bg");
    s_card_sidebar_bg = find_widget(registry, "sidebar_bg");
    s_card_tabs_bg = find_widget(registry, "tabs_bg");
    s_card_breadcrumb_bg = find_widget(registry, "breadcrumb_bg");
    s_card_editor_bg = find_widget(registry, "editor_bg");
    s_card_outline_bg = find_widget(registry, "outline_bg");
    s_card_panel_bg = find_widget(registry, "panel_bg");
    s_card_status_bg = find_widget(registry, "status_bg");

                       
    s_div_title_bottom = find_widget(registry, "div_title_bottom");
    s_div_rail_right = find_widget(registry, "div_rail_right");
    s_div_outline_left = find_widget(registry, "div_outline_left");
    s_div_panel_top = find_widget(registry, "div_panel_top");
    s_div_status_top = find_widget(registry, "div_status_top");
}

static void create_overlays(void)
{
    if (!s_root)
        return;
    s_designer_toolbar = aroma_canvas_create(s_root, EDITOR_X, EDITOR_Y,
                                             editor_w, DESIGN_TOOLBAR_H);
    s_designer_palette = aroma_canvas_create(s_root, DESIGN_PALETTE_X,
                                             design_body_y, DESIGN_PALETTE_W,
                                             design_body_h);
    s_designer_canvas = aroma_canvas_create(s_root, DESIGN_CANVAS_X,
                                            design_body_y, design_canvas_w,
                                            design_body_h);
                                                                            
                                                                    
    s_designer_preview = aroma_container_create(s_root, DESIGN_CANVAS_X,
                                                design_body_y, design_canvas_w,
                                                design_body_h);
    s_designer_overlay = aroma_canvas_create(s_root, DESIGN_CANVAS_X,
                                             design_body_y, design_canvas_w,
                                             design_body_h);
    if (s_designer_canvas)
        aroma_node_set_z_index(s_designer_canvas, 5);
    if (s_designer_preview)
        aroma_node_set_z_index(s_designer_preview, 6);
    if (s_designer_overlay)
        aroma_node_set_z_index(s_designer_overlay, 100);
    AromaNode *designer_nodes[] = {s_designer_toolbar, s_designer_palette,
                                   s_designer_canvas, s_designer_preview,
                                   s_designer_overlay};
    for (size_t i = 0; i < sizeof(designer_nodes) / sizeof(designer_nodes[0]); i++)
    {
        if (designer_nodes[i])
        {
            if (designer_nodes[i] == s_designer_toolbar ||
                designer_nodes[i] == s_designer_palette)
                aroma_node_set_z_index(designer_nodes[i], 101);
            aroma_node_set_hidden(designer_nodes[i], true);
        }
    }
                                                                        
                                                
    s_settings_canvas = aroma_canvas_create(s_root, EDITOR_X, EDITOR_Y,
                                            editor_w, editor_h);
    if (s_settings_canvas)
    {
        aroma_node_set_z_index(s_settings_canvas, 5);
        aroma_node_set_hidden(s_settings_canvas, true);
    }
    s_menu_canvas = aroma_canvas_create(s_root, 0, 0, win_w, win_h);
    if (s_menu_canvas)
    {
        aroma_node_set_z_index(s_menu_canvas, 200);
        aroma_node_set_hidden(s_menu_canvas, true);
        aroma_node_set_layout_fill(s_menu_canvas);
    }
    s_toast_canvas = aroma_canvas_create(s_root, win_w - 390, win_h - 116, 358, 64);
    if (s_toast_canvas)
    {
        aroma_node_set_z_index(s_toast_canvas, 150);
        aroma_node_set_hidden(s_toast_canvas, true);
    }
    s_dialog_canvas = aroma_canvas_create(s_root, 0, 0, win_w, win_h);
    if (s_dialog_canvas)
    {
        aroma_node_set_z_index(s_dialog_canvas, 180);
        aroma_node_set_hidden(s_dialog_canvas, true);
        aroma_node_set_layout_fill(s_dialog_canvas);
    }
}

static void apply_editor_palette(void)
{
    if (!s_editor)
        return;
    AromaCodeEditorPalette palette = aroma_code_editor_default_palette();
    uint32_t accent = C_PRIMARY;
    uint32_t accent_soft = (accent & 0x00FFFFFFu) | 0x55000000u;
    uint32_t accent_faint = (accent & 0x00FFFFFFu) | 0x33000000u;

    if (s_theme_is_dark)
    {
        palette.background = C_EDITOR;
        palette.gutter_background = C_EDITOR;
        palette.current_line = 0x12FFFFFFu;
        palette.selection = accent_soft;
        palette.text = 0xFFE5E5EAu;
        palette.comment = 0xFF7C7C80u;
        palette.keyword = 0xFFFF7AB2u;
        palette.string = 0xFFFC6A5Du;
        palette.number = 0xFFD9C97Cu;
        palette.type = 0xFF5DD8FFu;
        palette.function = 0xFF67B7A4u;
        palette.property = 0xFF9CDCFEu;
        palette.punctuation = 0xFFC7C7CCu;
        palette.tag = 0xFFFF7AB2u;
        palette.attribute = 0xFFFF9F0Au;
        palette.gutter_text = 0xFF636366u;
        palette.gutter_active = 0xFFF2F2F7u;
        palette.cursor = accent;
        palette.minimap = M3_SURFACE_HIGH;
        palette.minimap_viewport = accent_faint;
        palette.scrollbar = 0x55636366u;
        palette.scrollbar_hover = M3_OUTLINE;
        palette.popup_background = M3_SURFACE_HIGH;
        palette.popup_border = M3_SURFACE_HIGHEST;
        palette.popup_selection = accent_faint;
        palette.error = C_RED;
        palette.warning = C_YELLOW;
        palette.info = C_CYAN;
        palette.find_match = accent_soft;
        palette.find_bar = M3_SURFACE_HIGH;
    }
    else
    {
        palette.background = C_EDITOR;
        palette.gutter_background = 0xFFF2F2F7u;
        palette.current_line = 0x0A000000u;
        palette.selection = accent_faint;
        palette.text = 0xFF1C1C1Eu;
        palette.comment = 0xFF6C6C70u;
        palette.keyword = 0xFF9B2393u;
        palette.string = 0xFFC41A16u;
        palette.number = 0xFF1C00CFu;
        palette.type = 0xFF0B4F79u;
        palette.function = 0xFF326D74u;
        palette.property = 0xFF0F68A0u;
        palette.punctuation = 0xFF3C3C43u;
        palette.tag = 0xFF9B2393u;
        palette.attribute = 0xFFB35C00u;
        palette.gutter_text = 0xFF8E8E93u;
        palette.gutter_active = 0xFF1C1C1Eu;
        palette.cursor = accent;
        palette.minimap = 0xFFF2F2F7u;
        palette.minimap_viewport = accent_faint;
        palette.scrollbar = 0x55636366u;
        palette.scrollbar_hover = 0xFF8E8E93u;
        palette.popup_background = 0xFFFFFFFFu;
        palette.popup_border = 0xFFD1D1D6u;
        palette.popup_selection = accent_faint;
        palette.error = 0xFFFF3B30u;
        palette.warning = 0xFFFF9500u;
        palette.info = 0xFF32ADE6u;
        palette.find_match = accent_soft;
        palette.find_bar = 0xFFF2F2F7u;
    }
    aroma_code_editor_set_palette(s_editor, &palette);
}

