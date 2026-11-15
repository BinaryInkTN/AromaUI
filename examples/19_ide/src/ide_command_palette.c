                                                                        
                                                                        
                                                                        

                                                                            
                                            
static int cmd_match_score(const char *text, const char *filter)
{
    if (!filter[0])
        return 1;
    const char *t = text;
    const char *f = filter;
    int score = 0;
    int consecutive = 0;
    while (*t)
    {
        char tc = *t | 0x20;
        char fc = *f | 0x20;
        if (tc == fc)
        {
            f++;
            consecutive++;
            if (consecutive > 1)
                score += consecutive;
            else
                score += 10;                                          
            if (!*f)
                return score + 100;                       
        }
        else
        {
            consecutive = 0;
        }
        t++;
    }
    return 0;
}

static void cmd_palette_init(void)
{
    s_cmd_filter[0] = '\0';
    s_cmd_filter_len = 0;
    s_cmd_selected = 0;
    s_cmd_scroll = 0;
    s_cmd_recent_count = 0;
    cmd_palette_filter();
}

static void cmd_palette_filter(void)
{
    s_cmd_filtered_count = 0;
                                                                          
                                                 
    typedef struct
    {
        int idx;
        int score;
    } CmdScore;
    static CmdScore scores[CMD_MAX];
    int n = 0;
    for (int i = 0; i < CMD_COUNT; i++)
    {
        int score = cmd_match_score(k_commands[i].label, s_cmd_filter);
        if (score == 0 && s_cmd_filter[0])
            score = cmd_match_score(k_commands[i].desc, s_cmd_filter);
        if (score > 0 || !s_cmd_filter[0])
        {
            scores[n].idx = i;
            scores[n].score = score;
            n++;
        }
    }
                                            
    for (int i = 1; i < n; i++)
    {
        CmdScore key = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j].score < key.score)
        {
            scores[j + 1] = scores[j];
            j--;
        }
        scores[j + 1] = key;
    }
    for (int i = 0; i < n; i++)
        s_cmd_filtered[i] = scores[i].idx;
    s_cmd_filtered_count = n;
    if (s_cmd_selected >= s_cmd_filtered_count)
        s_cmd_selected = s_cmd_filtered_count > 0 ? s_cmd_filtered_count - 1 : 0;
    if (s_cmd_selected < 0)
        s_cmd_selected = 0;
                      
    int visible = CMD_LIST_MAX;
    if (visible < 1)
        visible = 1;
    if (s_cmd_selected < s_cmd_scroll)
        s_cmd_scroll = s_cmd_selected;
    if (s_cmd_selected >= s_cmd_scroll + visible)
        s_cmd_scroll = s_cmd_selected - visible + 1;
    if (s_cmd_scroll > s_cmd_filtered_count - visible)
        s_cmd_scroll = s_cmd_filtered_count - visible;
    if (s_cmd_scroll < 0)
        s_cmd_scroll = 0;
}

static void cmd_palette_execute_selected(void)
{
    if (s_cmd_filtered_count <= 0)
        return;
    int idx = s_cmd_filtered[s_cmd_selected];
    if (idx < 0 || idx >= CMD_COUNT)
        return;
    const CommandItem *cmd = &k_commands[idx];
                      
    for (int i = 0; i < s_cmd_recent_count; i++)
    {
        if (s_cmd_recent[i] == idx)
        {
                               
            for (int j = i; j > 0; j--)
                s_cmd_recent[j] = s_cmd_recent[j - 1];
            s_cmd_recent[0] = idx;
            return;
        }
    }
    if (s_cmd_recent_count < CMD_MAX)
    {
        for (int i = s_cmd_recent_count; i > 0; i--)
            s_cmd_recent[i] = s_cmd_recent[i - 1];
        s_cmd_recent[0] = idx;
        s_cmd_recent_count++;
    }
    close_dialog();
    const char *label = cmd->label;
    if (strcmp(label, "New File") == 0)
        open_dialog(IDE_DIALOG_NEW_FILE, "");
    else if (strcmp(label, "Open File...") == 0)
        ide_open_file_via_nfd();
    else if (strcmp(label, "Save") == 0)
    {
        if (s_active_file >= 0)
            save_file(s_active_file);
        toast("Saved");
    }
    else if (strcmp(label, "Save All") == 0)
    {
        int saved = save_all_files_ex(true);
        toast("Saved %d file%s", saved, saved == 1 ? "" : "s");
    }
    else if (strcmp(label, "Close Editor") == 0)
    {
        if (s_active_file >= 0)
            close_tab(s_active_file);
    }
    else if (strcmp(label, "New Project...") == 0)
        open_dialog(IDE_DIALOG_NEW_PROJECT, "");
    else if (strcmp(label, "Open Project...") == 0)
        open_dialog(IDE_DIALOG_OPEN_PROJECT, "");
    else if (strcmp(label, "Build Project") == 0)
        project_build();
    else if (strcmp(label, "Run Project") == 0)
        project_run();
    else if (strcmp(label, "Clean Build") == 0)
    {
        project_build();
        toast("Clean build started");
    }
    else if (strcmp(label, "Target: Linux") == 0)
        project_set_target(0);
    else if (strcmp(label, "Target: Android") == 0)
        project_set_target(1);
    else if (strcmp(label, "Next Android Device") == 0)
        project_android_next_device();
    else if (strcmp(label, "Android Device Manager") == 0)
        open_android_device_manager();
    else if (strcmp(label, "Refresh Android Devices") == 0)
        project_android_refresh_devices(true);
    else if (strcmp(label, "Setup Android Project") == 0)
        project_android_setup();
    else if (strcmp(label, "Exit") == 0)
    {
        aroma_ui_request_exit();
    }
    else if (strcmp(label, "Undo") == 0)
    {
        if (s_editor)
            aroma_code_editor_undo(s_editor);
    }
    else if (strcmp(label, "Redo") == 0)
    {
        if (s_editor)
            aroma_code_editor_redo(s_editor);
    }
    else if (strcmp(label, "Cut") == 0)
    {
        if (s_editor)
            aroma_code_editor_cut(s_editor);
    }
    else if (strcmp(label, "Copy") == 0)
    {
        if (s_editor)
            aroma_code_editor_copy(s_editor);
    }
    else if (strcmp(label, "Paste") == 0)
    {
        if (s_editor)
            aroma_code_editor_paste(s_editor);
    }
    else if (strcmp(label, "Select All") == 0)
    {
        if (s_editor)
            aroma_code_editor_select_all(s_editor);
    }
    else if (strcmp(label, "Find") == 0)
    {
        if (s_editor)
            aroma_code_editor_show_find(s_editor);
    }
    else if (strcmp(label, "Replace") == 0)
    {
        if (s_editor)
            aroma_code_editor_show_find(s_editor);
    }
    else if (strcmp(label, "Explorer") == 0)
    {
        s_sidebar_mode = 0;
        draw_sidebar();
    }
    else if (strcmp(label, "Toggle Panel") == 0)
    {
        bool show = !s_panel_visible || s_panel_tab != 1;
        s_panel_tab = 1;
        if (show)
        {
            if (!s_panel_visible)
                set_panel_visible(true);
            if (s_editor)
                aroma_ui_set_focused_node(s_panel_canvas);
        }
        else
        {
            set_panel_visible(false);
            if (s_editor)
                aroma_code_editor_focus(s_editor);
        }
        draw_panel();
    }
    else if (strcmp(label, "Detach Panel") == 0)
        panel_toggle_detach();
    else if (strcmp(label, "Detach Sidebar") == 0)
        sidebar_toggle_detach();
    else if (strcmp(label, "Problems") == 0)
    {
        s_panel_tab = 0;
        if (!s_panel_visible)
            set_panel_visible(true);
        draw_panel();
    }
    else if (strcmp(label, "Output") == 0)
    {
        s_panel_tab = 1;
        if (!s_panel_visible)
            set_panel_visible(true);
        draw_panel();
    }
    else if (strcmp(label, "Preview Window") == 0)
        preview_toggle();
    else if (strcmp(label, "Go to File...") == 0)
        open_dialog(IDE_DIALOG_OPEN_FILE, "");
    else if (strcmp(label, "Go to Definition") == 0)
    {
        if (s_active_file >= 0)
        {
                                                               
            toast("Go to Definition not yet wired");
        }
        else
        {
            toast("No active file");
        }
    }
    else if (strcmp(label, "Search Workspace") == 0)
        open_dialog(IDE_DIALOG_SEARCH, "");
    else if (strcmp(label, "Next Problem") == 0)
    {
                                             
        toast("Next Problem not yet wired");
    }
    else if (strcmp(label, "Language Server") == 0)
    {
        toast(s_lsp_connected ? "LSP: Connected" : "LSP: Not connected");
    }
    else if (strcmp(label, "Settings") == 0)
    {
        settings_window_open();
    }
    else if (strcmp(label, "Command Palette") == 0)
    {
                                               
        cmd_palette_filter();
        draw_dialog();
    }
    else if (strcmp(label, "Recent Commands") == 0)
    {
                                                                
    }
    else if (strcmp(label, "Changelog") == 0)
        home_show_changelog();
    else if (strcmp(label, "Toggle Sidebar") == 0)
    {
                                              
    }
    else if (strcmp(label, "Focus Editor") == 0)
    {
        if (s_editor)
            aroma_code_editor_focus(s_editor);
    }
}

static void draw_all(void);
static void open_dialog(IdeDialogMode mode, const char *initial);
static void draw_dialog(void);
static void close_dialog(void);
static void draw_cmd_palette(void);
static void open_properties_dialog(void);
static void draw_properties_dialog(void);
static void properties_dialog_submit(void);
static void refresh_workspace(void);
static void run_project_task(int index);
static void reset_workspace_ui(void);
static void open_initial_files(void);
static void designer_sync_for_file(void);
static void designer_set_visible(bool visible);
static void designer_apply_to_file(void);
static void designer_draw_all(void);
static int designer_history_push(void);
static bool designer_history_undo(void);
static bool designer_history_redo(void);
static void designer_history_clear(void);
static void wizard_open(void);
static void wizard_close_widgets(void);
static void wizard_set_step(int step);
static bool wizard_validate_current(void);
static void wizard_next(void);
static void wizard_back(void);
static void wizard_create_project(void);
static void wizard_draw(void);
static bool wizard_on_click(int x, int y);
static void wizard_stepper_changed(AromaNode *node, int value, void *user_data);
static void editor_on_definition(AromaNode *node, int line, int column,
                                 void *user_data);
static void draw_title(void);
static void draw_rail(void);
static void draw_sidebar(void);
static void draw_tabs(void);
static void draw_breadcrumb(void);
static void draw_outline(void);
static void draw_designer_tree(AromaNode *canvas, int x, int *y, int w, int depth);
static void draw_panel(void);
static void draw_status(void);
static void draw_menu(void);
static void draw_toast(void);
static void draw_properties_content(AromaNode *canvas, int offset_x,
                                    int offset_y, int canvas_w,
                                    int canvas_h);
static void preview_window_create(void);
static void preview_window_destroy(void);
static void preview_toggle(void);
static void preview_new_note(void *user_data);
static bool preview_on_click(AromaEvent *event, void *user_data);
static void markdown_preview_destroy(void);
static void ide_claim_node(AromaNode *node);
static int s_md_x = -1, s_md_y = -1, s_md_w = -1, s_md_h = -1;

                                                                           
                                                                       
static bool markdown_preview_fit(void)
{
    if (!s_markdown_webview)
        return false;

    int wx = EDITOR_X;
    int ww = editor_w;
    bool changed = s_md_x != wx || s_md_y != EDITOR_Y ||
                   s_md_w != ww || s_md_h != editor_h;
    AromaRect *wr = aroma_node_get_rect(s_markdown_webview);
    bool rect_stale = wr && (wr->x != wx || wr->y != EDITOR_Y ||
                             wr->width != ww || wr->height != editor_h);
    if (!changed && !rect_stale)
        return false;
    aroma_webview_set_position(s_markdown_webview, wx, EDITOR_Y);
    aroma_webview_set_size(s_markdown_webview, ww, editor_h);
                                                                         
    ide_claim_node(s_markdown_webview);
    s_md_x = wx;
    s_md_y = EDITOR_Y;
    s_md_w = ww;
    s_md_h = editor_h;
    if (s_editor_bg)
        aroma_node_invalidate(s_editor_bg);
    return changed;
}
typedef struct
{
    char *d;
    size_t len;
    size_t cap;
    bool oom;
} MdBuf;

typedef struct
{
    const char *p;
    size_t n;
} MdLine;

static void md_put(MdBuf *b, const char *s, size_t n)
{
    if (b->oom)
        return;
    if (b->len + n + 1 > b->cap)
    {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->len + n + 1)
            nc *= 2;
        char *nd = (char *)realloc(b->d, nc);
        if (!nd)
        {
            b->oom = true;
            return;
        }
        b->d = nd;
        b->cap = nc;
    }
    if (n)
        memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = '\0';
}

static void md_puts(MdBuf *b, const char *s) { md_put(b, s, strlen(s)); }
static void md_putc(MdBuf *b, char c) { md_put(b, &c, 1); }

static void md_esc(MdBuf *b, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        switch (s[i])
        {
        case '&': md_puts(b, "&amp;"); break;
        case '<': md_puts(b, "&lt;"); break;
        case '>': md_puts(b, "&gt;"); break;
        case '"': md_puts(b, "&quot;"); break;
        default: md_putc(b, s[i]); break;
        }
    }
}

                                                                            
                                                 
static size_t md_skip_code(const char *s, size_t n, size_t j)
{
    size_t r = 0;
    while (j + r < n && s[j + r] == '`')
        r++;
    size_t k = j + r;
    while (k < n)
    {
        if (s[k] == '`')
        {
            size_t m = 0;
            while (k + m < n && s[k + m] == '`')
                m++;
            if (m == r)
                return k + m;
            k += m;
        }
        else
            k++;
    }
    return j + r;
}

static bool md_parse_link(const char *s, size_t n, size_t i, size_t *ts,
                          size_t *te, size_t *us, size_t *ue, size_t *end)
{
    int depth = 0;
    size_t j = i;
    for (; j < n; j++)
    {
        if (s[j] == '\\')
        {
            j++;
            continue;
        }
        if (s[j] == '`')
        {
            size_t e = md_skip_code(s, n, j);
            j = e - 1;
            continue;
        }
        if (s[j] == '[')
            depth++;
        else if (s[j] == ']')
        {
            depth--;
            if (depth == 0)
                break;
        }
    }
    if (j + 1 >= n || s[j] != ']' || s[j + 1] != '(')
        return false;
    *ts = i + 1;
    *te = j;
    size_t k = j + 2;
    while (k < n && s[k] == ' ')
        k++;
    if (k < n && s[k] == '<')
    {
        size_t u0 = ++k;
        while (k < n && s[k] != '>' && s[k] != '\n')
            k++;
        if (k >= n || s[k] != '>')
            return false;
        *us = u0;
        *ue = k;
        k++;
    }
    else
    {
        size_t u0 = k;
        int pd = 0;
        while (k < n)
        {
            if (s[k] == '\\' && k + 1 < n)
            {
                k += 2;
                continue;
            }
            if (s[k] == '(')
                pd++;
            else if (s[k] == ')')
            {
                if (pd == 0)
                    break;
                pd--;
            }
            else if (s[k] == ' ' || s[k] == '\n')
                break;
            k++;
        }
        *us = u0;
        *ue = k;
    }
    while (k < n && s[k] == ' ')
        k++;
    if (k < n && (s[k] == '"' || s[k] == '\''))
    {
        char q = s[k++];
        while (k < n && s[k] != q)
            k++;
        if (k >= n)
            return false;
        k++;
        while (k < n && s[k] == ' ')
            k++;
    }
    if (k >= n || s[k] != ')')
        return false;
    *end = k + 1;
    return true;
}

static bool md_find_close(const char *s, size_t n, size_t from, char c,
                          size_t r, size_t *cpos)
{
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 1 && c == '~')
            break;
        size_t j = from;
        while (j < n)
        {
            if (s[j] == '\\')
            {
                j += 2;
                continue;
            }
            if (s[j] == '`')
            {
                j = md_skip_code(s, n, j);
                continue;
            }
            if (s[j] == c)
            {
                size_t k = 0;
                while (j + k < n && s[j + k] == c)
                    k++;
                bool prev_ok = j > from && !isspace((unsigned char)s[j - 1]);
                bool next_ok = (c != '_') || j + k >= n ||
                               !isalnum((unsigned char)s[j + k]);
                if (prev_ok && next_ok &&
                    ((pass == 0 && k == r) || (pass == 1 && k > r)))
                {
                    *cpos = j + (pass == 1 ? k - r : 0);
                    return true;
                }
                j += k;
                continue;
            }
            j++;
        }
    }
    return false;
}

static void md_put_url(MdBuf *b, const char *u, size_t n)
{
    if (n >= 11 && strncasecmp(u, "javascript:", 11) == 0)
    {
        md_puts(b, "#");
        return;
    }
    md_esc(b, u, n);
}

static void md_inline(MdBuf *b, const char *s, size_t n, int depth,
                      bool in_link)
{
    if (depth > 8)
    {
        md_esc(b, s, n);
        return;
    }
    size_t i = 0;
    while (i < n && !b->oom)
    {
        char c = s[i];

        if (c == '\\' && i + 1 < n)
        {
            if (s[i + 1] == '\n')
            {
                md_puts(b, "<br>\n");
                i += 2;
                continue;
            }
            if (ispunct((unsigned char)s[i + 1]))
            {
                md_esc(b, &s[i + 1], 1);
                i += 2;
                continue;
            }
        }

        if (c == '`')
        {
            size_t r = 0;
            while (i + r < n && s[i + r] == '`')
                r++;
            size_t e = md_skip_code(s, n, i);
            if (e != i + r)
            {
                const char *a = s + i + r;
                size_t alen = (e - r) - (i + r);
                while (alen && (*a == ' ' || *a == '\n'))
                {
                    a++;
                    alen--;
                }
                while (alen && (a[alen - 1] == ' ' || a[alen - 1] == '\n'))
                    alen--;
                md_puts(b, "<code>");
                md_esc(b, a, alen);
                md_puts(b, "</code>");
                i = e;
            }
            else
            {
                md_put(b, s + i, r);
                i += r;
            }
            continue;
        }

        if ((c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) &&
            !(c == '[' && in_link))
        {
            bool image = (c == '!');
            size_t ts, te, us, ue, end;
            if (md_parse_link(s, n, image ? i + 1 : i, &ts, &te, &us, &ue,
                              &end))
            {
                if (image)
                {
                    md_puts(b, "<img src=\"");
                    md_put_url(b, s + us, ue - us);
                    md_puts(b, "\" alt=\"");
                    md_esc(b, s + ts, te - ts);
                    md_puts(b, "\">");
                }
                else
                {
                    md_puts(b, "<a href=\"");
                    md_put_url(b, s + us, ue - us);
                    md_puts(b, "\">");
                    md_inline(b, s + ts, te - ts, depth + 1, true);
                    md_puts(b, "</a>");
                }
                i = end;
                continue;
            }
        }

        if (c == '<')
        {
            size_t j = i + 1;
            while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '\n')
                j++;
            if (j < n && s[j] == '>' && j > i + 1)
            {
                const char *u = s + i + 1;
                size_t L = j - i - 1;
                bool url = (L > 7 && strncmp(u, "http://", 7) == 0) ||
                           (L > 8 && strncmp(u, "https://", 8) == 0);
                bool mail = !url && memchr(u, '@', L) && memchr(u, '.', L) &&
                            !memchr(u, ':', L);
                if ((url || mail) && !in_link)
                {
                    md_puts(b, "<a href=\"");
                    if (mail)
                        md_puts(b, "mailto:");
                    md_esc(b, u, L);
                    md_puts(b, "\">");
                    md_esc(b, u, L);
                    md_puts(b, "</a>");
                    i = j + 1;
                    continue;
                }
            }
        }

        if (c == 'h' && !in_link && (i == 0 || !isalnum((unsigned char)s[i - 1])) &&
            ((i + 7 < n && strncmp(s + i, "http://", 7) == 0) ||
             (i + 8 < n && strncmp(s + i, "https://", 8) == 0)))
        {
            size_t j = i;
            while (j < n && !isspace((unsigned char)s[j]) && s[j] != '<')
                j++;
            while (j > i && strchr(".,;:!?\"'*_~", s[j - 1]))
                j--;
            while (j > i && s[j - 1] == ')' &&
                   memchr(s + i, '(', j - i) == NULL)
                j--;
            md_puts(b, "<a href=\"");
            md_esc(b, s + i, j - i);
            md_puts(b, "\">");
            md_esc(b, s + i, j - i);
            md_puts(b, "</a>");
            i = j;
            continue;
        }

        if (c == '*' || c == '_' || c == '~')
        {
            size_t r = 0;
            while (i + r < n && s[i + r] == c)
                r++;
            bool handled = false;
            if (c == '~')
            {
                if (r == 2 && i + 2 < n && !isspace((unsigned char)s[i + 2]))
                {
                    size_t cp;
                    if (md_find_close(s, n, i + 2, '~', 2, &cp))
                    {
                        md_puts(b, "<del>");
                        md_inline(b, s + i + 2, cp - (i + 2), depth + 1,
                                  in_link);
                        md_puts(b, "</del>");
                        i = cp + 2;
                        handled = true;
                    }
                }
            }
            else if (r <= 3 && i + r < n &&
                     !isspace((unsigned char)s[i + r]) &&
                     !(c == '_' && i > 0 &&
                       isalnum((unsigned char)s[i - 1])))
            {
                size_t cp;
                if (md_find_close(s, n, i + r, c, r, &cp))
                {
                    const char *open = r == 1   ? "<em>"
                                       : r == 2 ? "<strong>"
                                                : "<strong><em>";
                    const char *close = r == 1   ? "</em>"
                                        : r == 2 ? "</strong>"
                                                 : "</em></strong>";
                    md_puts(b, open);
                    md_inline(b, s + i + r, cp - (i + r), depth + 1, in_link);
                    md_puts(b, close);
                    i = cp + r;
                    handled = true;
                }
            }
            if (!handled)
            {
                md_put(b, s + i, r);
                i += r;
            }
            continue;
        }

        if (c == '\n')
        {
            if (i >= 2 && s[i - 1] == ' ' && s[i - 2] == ' ')
                md_puts(b, "<br>\n");
            else
                md_putc(b, '\n');
            i++;
            continue;
        }

        md_esc(b, &s[i], 1);
        i++;
    }
}
