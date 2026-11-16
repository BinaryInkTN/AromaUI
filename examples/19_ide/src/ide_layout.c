                                                                       
                                                                               
static int s_synced_win_w = 1440;
static int s_synced_right_x = 1226;

                                                                           
                                                                            
                                                         
static void ide_set_node_rect(AromaNode *node, int x, int y, int width,
                              int height)
{
    if (!node)
        return;
    AromaRect *r = aroma_node_get_rect(node);
    if (!r)
        return;
    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;
    if (r->x == x && r->y == y && r->width == width && r->height == height)
        return;
    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    aroma_node_invalidate(node);
}

                                                                           
                                    
static void ide_sync_layout_nodes(void)
{
    const int content_y = TITLE_H;
    const int content_h = status_y - TITLE_H;
    const int outline_w = win_w - right_x;

    ide_set_node_rect(s_card_title_bg, 0, 0, win_w, TITLE_H);
    ide_set_node_rect(s_title_canvas, 0, 0, win_w, TITLE_H);

    ide_set_node_rect(s_card_rail_bg, 0, content_y, s_rail_w, content_h);
    ide_set_node_rect(s_rail_canvas, 0, content_y, s_rail_w, content_h);

                                                                        
                              
    if (s_sidebar_detached)
    {
                                                                        
                                                                              
                                        
        ide_set_node_rect(s_card_sidebar_bg, 0, 0, 0, 0);
        if (s_sidebar_canvas_saved)
            ide_set_node_rect(s_sidebar_canvas_saved, 0, 0, 0, 0);
    }
    else
    {
        ide_set_node_rect(s_card_sidebar_bg, SIDEBAR_X, content_y, s_sidebar_w,
                          content_h);
        ide_set_node_rect(s_sidebar_canvas, SIDEBAR_X, content_y, s_sidebar_w,
                          content_h);
    }

    int editor_x = s_sidebar_detached ? s_rail_w : EDITOR_X;
    ide_set_node_rect(s_card_tabs_bg, editor_x, TITLE_H, editor_w, TAB_H);
    ide_set_node_rect(s_tabs_canvas, editor_x, TITLE_H, editor_w, TAB_H);

    ide_set_node_rect(s_card_breadcrumb_bg, editor_x, TITLE_H + TAB_H,
                      editor_w, BREADCRUMB_H);
    ide_set_node_rect(s_breadcrumb_canvas, editor_x, TITLE_H + TAB_H,
                      editor_w, BREADCRUMB_H);

    ide_set_node_rect(s_editor_bg, editor_x, EDITOR_Y, editor_w, editor_h);
    ide_set_node_rect(s_settings_canvas, editor_x, EDITOR_Y, editor_w,
                      editor_h);

    ide_set_node_rect(s_card_outline_bg, right_x, content_y, outline_w,
                      content_h);
    ide_set_node_rect(s_outline_canvas, right_x, content_y, outline_w,
                      content_h);
    if (s_card_outline_bg)
        aroma_node_set_hidden(s_card_outline_bg, outline_w <= 0);
    if (s_outline_canvas)
        aroma_node_set_hidden(s_outline_canvas, outline_w <= 0);

    ide_set_node_rect(s_card_panel_bg, SIDEBAR_X, panel_y, panel_w, panel_h);
    ide_set_node_rect(s_panel_canvas, SIDEBAR_X, panel_y, panel_w, panel_h);

    ide_set_node_rect(s_card_status_bg, 0, status_y, win_w, STATUS_H);
    ide_set_node_rect(s_status_canvas, 0, status_y, win_w, STATUS_H);

    ide_set_node_rect(s_div_title_bottom, 0, TITLE_H - 1, win_w, 1);
    ide_set_node_rect(s_div_rail_right, SIDEBAR_X - 1, content_y, 1, content_h);
    ide_set_node_rect(s_div_outline_left, right_x - 1, content_y, 1, content_h);
    ide_set_node_rect(s_div_panel_top, SIDEBAR_X, panel_y - 1, panel_w, 1);
    ide_set_node_rect(s_div_status_top, 0, status_y - 1, win_w, 1);
    if (s_div_outline_left)
        aroma_node_set_hidden(s_div_outline_left, outline_w <= 0);
    if (s_div_panel_top)
        aroma_node_set_hidden(s_div_panel_top, !s_panel_visible);

                           
    ide_set_node_rect(s_designer_toolbar, EDITOR_X, EDITOR_Y, editor_w,
                      DESIGN_TOOLBAR_H);
    ide_set_node_rect(s_designer_palette, DESIGN_PALETTE_X, design_body_y,
                      DESIGN_PALETTE_W, design_body_h);
    ide_set_node_rect(s_designer_canvas, DESIGN_CANVAS_X, design_body_y,
                      design_canvas_w, design_body_h);
    ide_set_node_rect(s_designer_preview, DESIGN_CANVAS_X, design_body_y,
                      design_canvas_w, design_body_h);
    ide_set_node_rect(s_designer_overlay, DESIGN_CANVAS_X, design_body_y,
                      design_canvas_w, design_body_h);
    if (s_synced_win_w != win_w || s_synced_right_x != right_x)
        s_designer_preview_dirty = true;

                                                               
    ide_set_node_rect(s_toast_canvas, s_toast_x, s_toast_y, s_toast_w,
                      s_toast_h);

    s_synced_right_x = right_x;
    s_synced_win_w = win_w;
}

void ide_tick(void)
{
    if (s_settings_close_requested)
    {
        s_settings_close_requested = false;
        settings_window_close();
    }

                                                                                           
    int cur_w = g_windows[0].window_width;
    int cur_h = g_windows[0].window_height;

                                                                         
                                                                          
                                                                         
    static int s_last_root_w = 0;
    static int s_last_root_h = 0;
    AromaRect *root_rect = g_windows[0].root_node
                               ? aroma_node_get_rect(g_windows[0].root_node)
                               : NULL;
    if (root_rect && root_rect->width > 0 && root_rect->height > 0 &&
        (root_rect->width != s_last_root_w ||
         root_rect->height != s_last_root_h))
    {
        s_last_root_w = root_rect->width;
        s_last_root_h = root_rect->height;
        if (cur_w == last_win_w && cur_h == last_win_h)
        {
            cur_w = root_rect->width;
            cur_h = root_rect->height;
        }
    }

                                                                      
    static int s_debug_resize = -1;
    static unsigned s_debug_frames = 0;
    if (s_debug_resize < 0)
        s_debug_resize = getenv("IDE_DEBUG_RESIZE") != NULL;
    if (s_debug_resize && (s_debug_frames++ % 120) == 0)
    {
        AromaRect *er = s_editor ? aroma_node_get_rect(s_editor) : NULL;
        AromaRect *br = s_editor_bg ? aroma_node_get_rect(s_editor_bg) : NULL;
        fprintf(stderr,
                "[ide-resize] window_width/height=%dx%d root_rect=%dx%d "
                "win=%dx%d right_x=%d editor_w=%d editor_rect_w=%d "
                "editor_bg_w=%d\n",
                g_windows[0].window_width, g_windows[0].window_height,
                root_rect ? root_rect->width : -1,
                root_rect ? root_rect->height : -1, win_w, win_h, right_x,
                editor_w, er ? er->width : -1, br ? br->width : -1);
    }

    if (cur_w > 0 && cur_h > 0 &&
        (cur_w != last_win_w || cur_h != last_win_h))
    {
        last_win_w = cur_w;
        last_win_h = cur_h;
        win_w = cur_w;
        win_h = cur_h;
    }

    ide_layout_recalc();
    panel_sync_detached_size();

                                                                     
    bool layout_changed = false;
    if (win_w != s_synced_win_w || right_x != s_synced_right_x)
    {
        ide_sync_layout_nodes();
        layout_changed = true;
    }

                                                      
    if (s_editor)
    {
        int ed_w = editor_w;
        AromaRect *er = aroma_node_get_rect(s_editor);
        bool editor_stale = er && (er->width != ed_w ||
                                   er->height != editor_h ||
                                   er->x != EDITOR_X || er->y != EDITOR_Y);
        if (editor_stale)
            aroma_code_editor_set_bounds(s_editor, EDITOR_X, EDITOR_Y,
                                         ed_w, editor_h);
        if (editor_stale || layout_changed)
        {
            aroma_node_invalidate(s_editor_bg);
            aroma_node_invalidate(s_panel_bg);
            aroma_node_invalidate(s_panel_canvas);
            aroma_node_invalidate(s_card_editor_bg);
            aroma_node_invalidate(s_card_panel_bg);
            aroma_node_invalidate(s_card_outline_bg);
            aroma_node_invalidate(s_card_sidebar_bg);
            aroma_node_invalidate(s_card_title_bg);
            aroma_node_invalidate(s_card_rail_bg);
            aroma_node_invalidate(s_card_tabs_bg);
            aroma_node_invalidate(s_card_breadcrumb_bg);
            aroma_node_invalidate(s_card_status_bg);
            draw_all();
        }
    }

                                                                           
    if (markdown_preview_fit())
        draw_all();

    if (s_pending_definition)
    {
        s_pending_definition = false;
        goto_definition_target(s_pending_file, s_pending_line,
                               s_pending_column, s_pending_symbol);
    }
    if (s_toast_visible && s_toast_until > 0 && now_ms() > s_toast_until)
        hide_toast();
    uint64_t tick_now = now_ms();
    if (s_project_ui_dirty && tick_now - s_project_ui_last >= 100)
    {
        s_project_ui_dirty = false;
        s_project_ui_last = tick_now;
        if (s_panel_visible && s_panel_tab == 1)
            draw_panel();
    }
    editor_flush_pending(false);
    if (s_editor)
        aroma_code_editor_tick(s_editor);
    if (s_preview_visible && s_preview_window)
    {
        AromaTheme ide_theme = aroma_ui_get_theme();
        AromaTheme preview_theme = preview_theme_value(s_preview_theme_index);
        aroma_ui_set_theme(&preview_theme);
        aroma_ui_render(s_preview_window);
        aroma_ui_set_theme(&ide_theme);
    }
}

void ide_shutdown(void)
{
    aroma_ui_set_window_close_callback(NULL, NULL);
    aroma_event_set_global_key_handler(NULL, NULL);
    hide_toast();
    close_menu();
    preview_window_destroy();
    if (s_panel_detached_window)
    {
        aroma_ui_destroy_window(s_panel_detached_window);
        s_panel_detached_window = NULL;
    }
    if (s_sidebar_detached_window)
    {
        aroma_ui_destroy_window(s_sidebar_detached_window);
        s_sidebar_detached_window = NULL;
    }
    s_panel_detached = false;
    s_panel_detached_canvas = NULL;
    s_panel_draw_canvas = NULL;
    s_sidebar_detached = false;
    s_sidebar_detached_canvas = NULL;
    s_sidebar_canvas_saved = NULL;
    free(s_design_clipboard);
    s_design_clipboard = NULL;
    designer_history_clear();
    designer_preview_clear();
    ide_workspace_shutdown();
    s_root = NULL;
    s_title_canvas = NULL;
    s_rail_canvas = NULL;
    s_sidebar_canvas = NULL;
    s_tabs_canvas = NULL;
    s_breadcrumb_canvas = NULL;
    s_outline_canvas = NULL;
    s_panel_canvas = NULL;
    s_status_canvas = NULL;
    s_editor_bg = NULL;
    s_settings_canvas = NULL;
    s_panel_bg = NULL;
    s_menu_canvas = NULL;
    s_toast_canvas = NULL;
    s_editor = NULL;

                          
    s_card_title_bg = NULL;
    s_card_rail_bg = NULL;
    s_card_sidebar_bg = NULL;
    s_card_tabs_bg = NULL;
    s_card_breadcrumb_bg = NULL;
    s_card_editor_bg = NULL;
    s_card_outline_bg = NULL;
    s_card_panel_bg = NULL;
    s_card_status_bg = NULL;

                       
    s_div_title_bottom = NULL;
    s_div_rail_right = NULL;
    s_div_outline_left = NULL;
    s_div_panel_top = NULL;
    s_div_status_top = NULL;

    s_designer_toolbar = NULL;
    s_designer_palette = NULL;
    s_designer_canvas = NULL;
    s_designer_preview = NULL;
    s_designer_overlay = NULL;
    s_designer_preview_registry = NULL;
    s_designer_preview_ok = false;
    s_designer_preview_dirty = true;
    s_wizard_stepper = NULL;
    s_preview_window = NULL;
    s_preview_registry = NULL;
    s_preview_visible = false;
    s_assets = NULL;
    s_active_file = -1;
    s_tab_count = 0;
    s_log_count = 0;
    s_outline_count = 0;
    s_problem_total = 0;
    s_pending_definition = false;
    s_toast_visible = false;
    s_toast_until = 0;
    s_menu_open = false;
}
