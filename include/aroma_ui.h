#ifndef AROMA_UI_H
#define AROMA_UI_H










#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "aroma_style.h"
#include "aroma_widgets.h"
#include "aroma_material_icons.h"
#include "aroma_drawlist.h"
#include "aroma_material_font.h"
#include "aroma_chrome_icon_font.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#ifdef ESP32
#include <Arduino.h>
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct  AromaNode AromaNode;
    typedef struct  AromaWindow AromaWindow;
    typedef struct  AromaContainer AromaContainer;
    typedef struct  AromaButton AromaButton;
    typedef struct  AromaDropdown AromaDropdown;
    typedef struct  AromaSlider AromaSlider;
    typedef struct  AromaTextbox AromaTextbox;
    typedef struct  AromaSwitch AromaSwitch;
    typedef struct  AromaMenu AromaMenu;
    typedef struct  AromaFont AromaFont;
    typedef struct  AromaCheckbox AromaCheckbox;
    typedef struct  AromaRadioButton AromaRadioButton;
    typedef struct  AromaProgressBar AromaProgressBar;
    typedef struct  AromaLabel AromaLabel;
    typedef struct  AromaDivider AromaDivider;
    typedef struct  AromaIconButton AromaIconButton;
    typedef struct  AromaDialog AromaDialog;
    typedef struct  AromaListView AromaListView;
    typedef struct  AromaTooltip AromaTooltip;
    typedef struct  AromaCard AromaCard;
    typedef struct  AromaChip AromaChip;
    typedef struct  AromaFAB AromaFAB;
    typedef struct  AromaSnackbar AromaSnackbar;
    typedef struct  AromaTabs AromaTabs;
    typedef struct  AromaSidebar AromaSidebar;
    typedef struct  AromaDebugOverlay AromaDebugOverlay;
    typedef struct  AromaIcon AromaIcon;





    typedef struct
    {

        AromaWindow *window;

        AromaNode *root_node;

        size_t window_id;

        bool is_active;

        AromaFont *default_font;
        bool needs_redraw;
    } AromaWindowHandle;

#define AROMA_MAX_WINDOWS 16
#define AROMA_CLEAR_NONE UINT32_MAX

    extern bool g_ui_initialized;
    extern AromaWindowHandle g_windows[AROMA_MAX_WINDOWS];
    extern int g_window_count;
    extern AromaNode *g_main_window;

    extern AromaNode *g_focused_node;
    extern void aroma_graphics_load_font_for_window(size_t window_id, AromaFont *font);





    static inline AromaNode *aroma_ui_get_focused_node(void)
    {
        return g_focused_node;
    }





    static inline void aroma_ui_set_focused_node(AromaNode *node)
    {
        g_focused_node = node;
    }





    static inline void aroma_ui_clear_focused_node(AromaNode *node)
    {
        if (g_focused_node == node)
        {
            g_focused_node = NULL;
        }
    }












    static inline AromaNode *aroma_ui_loading(
        AromaNode *parent,
        int x, int y, int radius, int thickness,
        uint32_t color)
    {
        return aroma_loading_create(parent, x, y, radius, thickness, color);
    }











    static inline AromaNode *aroma_ui_map(
        AromaNode *parent,
        int x, int y, int width, int height)
    {
        return aroma_map_create(parent, x, y, width, height);
    }

    void aroma_ui_set_immediate_mode(bool enabled);
    bool aroma_ui_is_immediate_mode(void);
    void aroma_ui_request_redraw(void *user_data);
    void aroma_ui_request_frame(void);
    bool aroma_ui_consume_redraw(void);

    AromaDrawList *aroma_ui_begin_frame(size_t window_id);
    void aroma_ui_end_frame(size_t window_id);
    void aroma_ui_render_dirty_window(size_t window_id, uint32_t clear_color);

    extern bool aroma_ui_init_impl(void);





    static inline bool aroma_ui_init(void)
    {
        if (g_ui_initialized)
        {
            LOG_INFO("Aroma UI already initialized");
            return true;
        }
        return aroma_ui_init_impl();
    }












    static inline void aroma_ui_set_theme(const AromaTheme *theme)
    {
        if (theme)
        {
            aroma_theme_set_global(theme);
            for (int i = 0; i < g_window_count; i++)
            {
                if (g_windows[i].root_node)
                    aroma_node_invalidate_tree(g_windows[i].root_node);
            }
            aroma_ui_request_redraw(NULL);
            LOG_INFO("Theme updated");
        }
    }





    static inline AromaTheme aroma_ui_get_theme(void)
    {
        return aroma_theme_get_global();
    }

    extern void aroma_ui_shutdown_impl(void);




    static inline void aroma_ui_shutdown(void)
    {
        if (!g_ui_initialized)
            return;
        aroma_ui_shutdown_impl();
    }

    extern bool aroma_ui_is_running_impl(void);





    static inline bool aroma_ui_is_running(void)
    {
        if (!g_ui_initialized)
            return false;
        return aroma_ui_is_running_impl();
    }

    extern void aroma_ui_process_events_impl(void);







    static inline void aroma_ui_process_events(void)
    {
        if (!g_ui_initialized)
            return;
        aroma_ui_process_events_impl();
    }

    extern void aroma_ui_render_impl(struct AromaWindow *window_data);
    extern void aroma_ui_render_all_windows_impl(void);





    static inline void aroma_ui_render(AromaWindow *window)
    {
        if (!g_ui_initialized || !window)
            return;
        AromaNode *window_node = (AromaNode *)window;
        if (!window_node || window_node->node_type != NODE_TYPE_ROOT)
            return;

        struct AromaWindow *window_data = (struct AromaWindow *)window_node->node_widget_ptr;
        if (!window_data)
            return;

        size_t dirty_count = 0;
        aroma_dirty_list_get(&dirty_count);
        if (dirty_count == 0 && !aroma_ui_is_immediate_mode())
            return;

        aroma_ui_render_impl(window_data);
    }




    static inline void aroma_ui_render_all(void)
    {
        if (!g_ui_initialized)
            return;
        aroma_ui_render_all_windows_impl();
    }







    static inline AromaFont *aroma_ui_load_font(const char *path, int size_px)
    {
        if (!path || size_px <= 0)
        {
            LOG_ERROR("Invalid font parameters");
            return NULL;
        }

        AromaFont *font = aroma_font_create(path, size_px);
        if (font)
        {
            LOG_INFO("Font loaded: %s (size %d)", path, size_px);
        }
        else
        {
            LOG_WARNING("Failed to load font: %s", path);
        }
        return font;
    }





    static inline void aroma_ui_unload_font(AromaFont *font)
    {
        if (font)
        {
            aroma_font_destroy(font);
            LOG_INFO("Font unloaded");
        }
    }

    static inline void aroma_ui_prepare_font_for_window(size_t window_id, AromaFont *font)
    {
        if (!font)
            return;
        aroma_graphics_load_font_for_window(window_id, font);
    }

    extern AromaWindow *aroma_ui_create_window_impl(const char *title, int width, int height);
    extern void aroma_ui_open_url_impl(const char *url);









    static inline AromaWindow *aroma_ui_create_window(const char *title, int width, int height)
    {
        if (!g_ui_initialized)
        {
            LOG_ERROR("Aroma UI not initialized");
            return NULL;
        }

        if (!title || width <= 0 || height <= 0)
        {
            LOG_ERROR("Invalid window parameters");
            return NULL;
        }

        return aroma_ui_create_window_impl(title, width, height);
    }

    extern void aroma_ui_destroy_window_impl(AromaWindow *window);





    extern void aroma_ui_set_offscreen_mode(bool offscreen);
    extern void aroma_ui_set_use_surfaceless(bool use_surfaceless);








    extern void aroma_ui_read_pixels(AromaWindow *window, void* buffer, int width, int height);





    static inline void aroma_ui_destroy_window(AromaWindow *window)
    {
        if (!window)
            return;
        aroma_ui_destroy_window_impl(window);
    }






    static inline void aroma_ui_window_set_background(AromaWindow *window, uint32_t color)
    {
        if (!window)
            return;
        LOG_INFO("Window background color set to 0x%06X", color);
    }






    static inline void aroma_ui_window_set_visible(AromaWindow *window, bool visible)
    {
        if (!window)
            return;
        LOG_INFO("Window visibility set to %s", visible ? "visible" : "hidden");
    }





    static inline int aroma_ui_window_count(void)
    {
        return g_window_count;
    }






    static inline AromaWindow *aroma_ui_get_window_at(int index)
    {
        if (index < 0 || index >= g_window_count)
            return NULL;
        return g_windows[index].window;
    }





    static inline void aroma_ui_open_url(const char *url)
    {
        if (!g_ui_initialized || !url)
            return;
        aroma_ui_open_url_impl(url);
    }

    void aroma_graphics_clear(size_t window_id, uint32_t color);

    void aroma_graphics_render_text(size_t window_id, AromaFont *font, const char *text,
                                    int x, int y, uint32_t color, float scale);

    void aroma_graphics_swap_buffers(size_t window_id);





    typedef enum
    {

        AROMA_INTENT_VIEW,

        AROMA_INTENT_SEND,

        AROMA_INTENT_EDIT,

        AROMA_INTENT_DIAL,

        AROMA_INTENT_CALL
    } AromaIntentAction;





    typedef struct
    {

        const char *key;

        const char *string_value;
    } AromaIntentExtra;











    static inline void aroma_ui_android_intent(AromaIntentAction action, const char *uri, const char *type, const AromaIntentExtra *extras, int extra_count)
    {
        if (!g_ui_initialized)
            return;
        extern void aroma_ui_android_intent_impl(int action, const char *uri, const char *type, const AromaIntentExtra *extras, int extra_count);
        aroma_ui_android_intent_impl((int)action, uri, type, extras, extra_count);
    }

    void aroma_platform_set_window_update_callback(void (*callback)(size_t, void *), void *user_data);















    static inline AromaNode *aroma_ui_button(
        AromaNode *parent,
        const char *text,
        int x, int y, int width, int height,
        bool (*on_click)(AromaNode *, void *),
        void *user_data,
        AromaFont *font)
    {
        if (!parent)
            return NULL;
        AromaNode *btn = aroma_button_create(parent, text, x, y, width, height);
        if (btn)
        {
            if (on_click)
            {
                aroma_button_set_on_click(btn, on_click, user_data);
            }
            aroma_button_setup_events(btn, aroma_ui_request_redraw, NULL);
            if (font)
            {
                aroma_button_set_font(btn, font);
            }
        }
        return btn;
    }






    static inline AromaNode *aroma_ui_button_with_type(
        AromaNode *parent,
        const char *text,
        int x, int y, int width, int height,
        AromaButtonType type,
        bool (*on_click)(AromaNode *, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *btn = aroma_ui_button(parent, text, x, y, width, height, on_click, user_data, font);
        if (btn)
            aroma_button_set_type(btn, type);
        return btn;
    }

















    static inline AromaNode *aroma_ui_button_with_icon(
        AromaNode *parent,
        const char *text,
        int x, int y, int width, int height,
        bool (*on_click)(AromaNode *, void *),
        void *user_data,
        AromaFont *font,
        const char *icon_code,
        AromaFont *icon_font)
    {
        AromaNode *btn = aroma_ui_button(parent, text, x, y, width, height, on_click, user_data, font);
        if (btn && icon_code && icon_font)
        {
            aroma_button_set_icon(btn, icon_code, icon_font);
        }
        return btn;
    }












    static inline AromaNode *aroma_ui_label(
        AromaNode *parent,
        const char *text,
        int x, int y,
        AromaLabelStyle style,
        AromaFont *font)
    {
        if (!parent)
            return NULL;
        AromaNode *label = aroma_label_create(parent, text, x, y, style);
        if (label && font)
        {
            aroma_label_set_font(label, font);
        }
        return label;
    }

    static inline AromaNode *aroma_ui_container(
        AromaNode *parent,
        int x, int y, int width, int height,
        AromaLayoutMode layout_mode,
        AromaFlexDirection flex_dir,
        AromaJustifyContent justify,
        AromaAlignItems align
    )
    {
        AromaNode *cont = aroma_container_create(parent, x, y, width, height);
        if (cont && layout_mode != AROMA_LAYOUT_MODE_NONE)
        {
            aroma_node_set_layout_mode(cont, layout_mode);
            aroma_node_set_flex_direction(cont, flex_dir);
            aroma_node_set_justify_content(cont, justify);
            aroma_node_set_align_items(cont, align);
        }
        return cont;
    }















    static inline AromaNode *aroma_ui_scrollable_container(
        AromaNode *parent,
        int x, int y, int width, int height,
        AromaScrollDirection direction)
    {
        AromaNode *c = aroma_container_create(parent, x, y, width, height);
        if (c)
        {
            aroma_container_set_scrollable(c, true);



            aroma_node_set_layout_mode(c, AROMA_LAYOUT_MODE_FLEX);
            aroma_node_set_flex_direction(c, AROMA_FLEX_COLUMN);
            aroma_node_set_justify_content(c, AROMA_JUSTIFY_START);
            aroma_node_set_align_items(c, AROMA_ALIGN_CENTER);

            aroma_container_set_scroll_direction(c, direction);
        }
        return c;
    }

    static inline AromaNode *aroma_ui_image(
        AromaNode *parent,
        const char *path,
        int x, int y, int width, int height)
    {
        return aroma_image_create(parent, path, x, y, width, height);
    }

    static inline AromaNode *aroma_ui_image_mem(
        AromaNode *parent,
        unsigned char *data,
        size_t len,
        int x, int y, int width, int height)
    {
        return aroma_image_create_from_memory(parent, data, len, x, y, width, height);
    }













    static inline AromaNode *aroma_ui_icon(
        AromaNode *parent,
        const char *icon_code,
        int x, int y, int size,
        uint32_t color,
        AromaFont *font)
    {
        AromaNode *node = aroma_icon_create(parent, x, y, size);
        if (node)
        {
            if (icon_code && font)
            {
                aroma_icon_set_text(node, icon_code, font);
            }
            aroma_icon_set_color(node, color);
        }
        return node;
    }















    static inline AromaNode *aroma_ui_checkbox(
        AromaNode *parent,
        const char *label,
        int x, int y, int width, int height,
        void (*callback)(bool, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *cb = aroma_checkbox_create(parent, label, x, y, width, height);
        if (cb)
        {
            if (callback)
                aroma_checkbox_set_callback(cb, callback, user_data);
            if (font)
                aroma_checkbox_set_font(cb, font);
            aroma_checkbox_setup_events(cb, aroma_ui_request_redraw, NULL);
        }
        return cb;
    }
















    static inline AromaNode *aroma_ui_radiobutton(
        AromaNode *parent,
        const char *label,
        int x, int y, int width, int height,
        int group_id,
        void (*callback)(void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *rb = aroma_radiobutton_create(parent, label, x, y, width, height, group_id);
        if (rb)
        {
            if (callback)
                aroma_radiobutton_set_callback(rb, callback, user_data);
            if (font)
                aroma_radiobutton_set_font(rb, font);
            aroma_radio_button_setup_events(rb, aroma_ui_request_redraw, NULL);
        }
        return rb;
    }














    static inline AromaNode *aroma_ui_switch(
        AromaNode *parent,
        int x, int y, int width, int height,
        bool initial_state,
        bool (*on_change)(AromaNode *, void *),
        void *user_data)
    {
        AromaNode *sw = aroma_switch_create(parent, x, y, width, height, initial_state);
        if (sw)
        {
            if (on_change)
                aroma_switch_set_on_change(sw, on_change, user_data);
            aroma_switch_setup_events(sw, aroma_ui_request_redraw, NULL);
        }
        return sw;
    }
















    static inline AromaNode *aroma_ui_slider(
        AromaNode *parent,
        int x, int y, int width, int height,
        int min, int max, int value,
        bool (*on_change)(AromaNode *, void *),
        void *user_data)
    {
        AromaNode *sl = aroma_slider_create(parent, x, y, width, height, min, max, value);
        if (sl)
        {
            if (on_change)
                aroma_slider_set_on_change(sl, on_change, user_data);
            aroma_slider_setup_events(sl, aroma_ui_request_redraw, NULL);
        }
        return sl;
    }















    static inline AromaNode *aroma_ui_textbox(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char *placeholder,
        bool (*on_text_changed)(AromaNode *, const char *, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *tb = aroma_textbox_create(parent, x, y, width, height);
        if (tb)
        {
            if (placeholder)
                aroma_textbox_set_placeholder(tb, placeholder);
            if (on_text_changed)
                aroma_textbox_set_on_text_changed(tb, on_text_changed, user_data);
            if (font)
                aroma_textbox_set_font(tb, font);
            aroma_textbox_setup_events(tb, aroma_ui_request_redraw, NULL, NULL);
        }
        return tb;
    }















    static inline AromaNode *aroma_ui_iconbutton(
        AromaNode *parent,
        const char *icon,
        int x, int y,
        int size,
        AromaIconButtonVariant variant,
        void (*callback)(void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *btn = aroma_iconbutton_create(parent, icon, x, y, size, variant);
        if (btn)
        {
            if (callback)
                aroma_iconbutton_set_callback(btn, callback, user_data);
            if (font)
                aroma_iconbutton_set_font(btn, font);
        }
        return btn;
    }














    static inline AromaNode *aroma_ui_chip(
        AromaNode *parent,
        const char *label,
        int x, int y,
        AromaChipType type,
        void (*callback)(void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *chip = aroma_chip_create(parent, x, y, label, type);
        if (chip)
        {
            if (callback)
                aroma_chip_set_callback(chip, callback, user_data);
            if (font)
                aroma_chip_set_font(chip, font);
        }
        return chip;
    }

    static inline AromaNode *aroma_ui_chip_with_icon(
        AromaNode *parent,
        const char *label,
        int x, int y,
        AromaChipType type,
        void (*callback)(void *),
        void *user_data,
        AromaFont *font,
        const char *icon_code,
        AromaFont *icon_font)
    {
        AromaNode *chip = aroma_ui_chip(parent, label, x, y, type, callback, user_data, font);
        if (chip && icon_code && icon_font)
        {
            aroma_chip_set_icon(chip, icon_code, icon_font);
        }
        return chip;
    }












    static inline AromaNode *aroma_ui_card(
        AromaNode *parent,
        int x, int y, int width, int height,
        AromaCardType type)
    {
        return aroma_card_create(parent, x, y, width, height, type);
    }
















    static inline AromaNode *aroma_ui_frosted_card(
        AromaNode *parent,
        int x, int y, int width, int height,
        float blur_radius)
    {
        AromaNode *card = aroma_card_create(parent, x, y, width, height,
                                            CARD_TYPE_GLASS);
        if (card)
        {
            aroma_card_set_backdrop_blur(card, blur_radius);
        }
        return card;
    }













    static inline AromaNode *aroma_ui_progressbar(
        AromaNode *parent,
        int x, int y, int width, int height,
        AromaProgressType type,
        float progress)
    {
        AromaNode *pb = aroma_progressbar_create(parent, x, y, width, height, type);
        if (pb)
        {
            aroma_progressbar_set_progress(pb, progress);
        }
        return pb;
    }











    static inline AromaNode *aroma_ui_divider(
        AromaNode *parent,
        int x, int y, int length,
        AromaDividerOrientation orientation)
    {
        return aroma_divider_create(parent, x, y, length, orientation);
    }




















    static inline AromaNode *aroma_ui_listview(
        AromaNode *parent,
        int x, int y, int width, int height,
        void (*callback)(int, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *sc = aroma_container_create(parent, x, y, width, height);
        if (!sc)
            return NULL;
        aroma_container_set_scrollable(sc, true);
        aroma_container_set_scroll_direction(sc, AROMA_SCROLL_VERTICAL);

        aroma_node_set_layout_mode(sc, AROMA_LAYOUT_MODE_FLEX);
        aroma_node_set_flex_direction(sc, AROMA_FLEX_COLUMN);
        int list_width = width;
        int list_height = height;
#ifdef __ANDROID__


        AromaRect *container_rect = aroma_node_get_rect(sc);
        if (container_rect)
        {
            list_width = aroma_android_px_to_dp(container_rect->width);
            list_height = aroma_android_px_to_dp(container_rect->height);
        }
#endif
        AromaNode *lv = aroma_listview_create(sc, 0, 0,
                                              list_width, list_height);
        if (lv)
        {
            if (callback)
                aroma_listview_set_callback(lv, callback, user_data);
            if (font)
                aroma_listview_set_font(lv, font);
        }
        return lv;
    }













    static inline AromaNode *aroma_ui_table(
        AromaNode *parent,
        int x, int y, int width, int height,
        int num_cols,
        void (*callback)(int, void *),
        void *user_data)
    {
        AromaNode *sc = aroma_container_create(parent, x, y, width, height);
        if (!sc)
            return NULL;
        aroma_container_set_scrollable(sc, true);
        aroma_container_set_scroll_direction(sc, AROMA_SCROLL_VERTICAL);

        aroma_node_set_layout_mode(sc, AROMA_LAYOUT_MODE_FLEX);
        aroma_node_set_flex_direction(sc, AROMA_FLEX_COLUMN);

        AromaNode *tbl = aroma_table_create(sc, 0, 0, width, height, num_cols);
        if (tbl)
        {
            if (callback)
                aroma_table_set_callback(tbl, callback, user_data);
        }
        return tbl;
    }










    static inline AromaNode *aroma_ui_menu(
        AromaNode *parent,
        int x, int y,
        AromaFont *font)
    {
        AromaNode *menu = aroma_menu_create(parent, x, y);
        if (menu && font)
        {
            aroma_menu_set_font(menu, font);
        }
        return menu;
    }













    static inline AromaNode *aroma_ui_dialog(
        AromaNode *parent,
        const char *title,
        const char *message,
        int width, int height,
        AromaDialogType type,
        AromaFont *font)
    {
        AromaNode *dlg = aroma_dialog_create(parent, title, message, width, height, type);
        if (dlg && font)
        {
            aroma_dialog_set_font(dlg, font);
        }
        return dlg;
    }
















    static inline AromaNode *aroma_ui_tabs(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels, int count,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *tabs = aroma_tabs_create(parent, x, y, width, height, labels, count);
        if (tabs)
        {
            if (on_change)
                aroma_tabs_set_on_change(tabs, on_change, user_data);
            if (font)
                aroma_tabs_set_font(tabs, font);
            aroma_tabs_setup_events(tabs, aroma_ui_request_redraw, NULL);
        }
        return tabs;
    }

    static inline AromaNode *aroma_ui_tabs_with_icons(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels,
        const char **icons,
        int count,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font,
        AromaFont *icon_font)
    {
        AromaNode *tabs = aroma_ui_tabs(parent, x, y, width, height, labels, count, on_change, user_data, font);
        if (tabs && icons && icon_font)
        {
            for (int i = 0; i < count; i++)
            {
                if (icons[i])
                {
                    aroma_tabs_set_icon(tabs, i, icons[i], icon_font);
                }
            }
        }
        return tabs;
    }








    static inline AromaNode *aroma_ui_tabbar(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels,
        const char **icons,
        int count,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font,
        AromaFont *icon_font)
    {
        AromaNode *tabs = aroma_ui_tabs_with_icons(parent, x, y, width, height, labels, icons,
                                                   count, on_change, user_data, font, icon_font);
        if (tabs)
            aroma_tabs_set_variant(tabs, TABS_VARIANT_BAR);
        return tabs;
    }
















    static inline AromaNode *aroma_ui_segmented(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels, int count,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *seg = aroma_segmented_create(parent, x, y, width, height, labels, count);
        if (seg)
        {
            if (on_change)
                aroma_segmented_set_on_change(seg, on_change, user_data);
            if (font)
                aroma_segmented_set_font(seg, font);
            aroma_segmented_setup_events(seg, aroma_ui_request_redraw, NULL);
        }
        return seg;
    }










    static inline AromaNode *aroma_ui_snackbar(
        AromaNode *parent,
        const char *message,
        int duration_ms,
        AromaFont *font)
    {
        AromaNode *snk = aroma_snackbar_create(parent, message, duration_ms);
        if (snk && font)
        {
            aroma_snackbar_set_font(snk, font);
        }
        return snk;
    }












    static inline AromaNode *aroma_ui_tooltip(
        AromaNode *parent,
        const char *text,
        int x, int y,
        AromaTooltipPosition pos,
        AromaFont *font)
    {
        AromaNode *tt = aroma_tooltip_create(parent, text, x, y, pos);
        if (tt && font)
        {
            aroma_tooltip_set_font(tt, font);
        }
        return tt;
    }
















    static inline AromaNode *aroma_ui_sidebar(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels, int count,
        void (*on_select)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *sb = aroma_sidebar_create(parent, x, y, width, height, labels, count);
        if (sb)
        {
            if (on_select)
                aroma_sidebar_set_on_select(sb, on_select, user_data);
            if (font)
                aroma_sidebar_set_font(sb, font);
            aroma_sidebar_setup_events(sb, aroma_ui_request_redraw, NULL);
        }
        return sb;
    }

    static inline AromaNode *aroma_ui_sidebar_with_icons(
        AromaNode *parent,
        int x, int y, int width, int height,
        const char **labels,
        const char **icons,
        int count,
        void (*on_select)(AromaNode *, int, void *),
        void *user_data,
        AromaFont *font,
        AromaFont *icon_font)
    {
        AromaNode *sb = aroma_ui_sidebar(parent, x, y, width, height, labels, count, on_select, user_data, font);
        if (sb && icons && icon_font)
        {
            for (int i = 0; i < count; i++)
            {
                if (icons[i])
                {
                    aroma_sidebar_set_icon(sb, i, icons[i], icon_font);
                }
            }
        }
        return sb;
    }

















    static inline AromaNode *aroma_ui_dropdown(
        AromaNode *parent,
        int x, int y, int width, int height,
        char **options,
        int option_count,
        void (*on_selection_changed)(int, const char *, void *),
        void *user_data,
        AromaFont *font)
    {
        AromaNode *dd = aroma_dropdown_create(parent, x, y, width, height);
        if (dd)
        {
            if (options && option_count > 0)
            {
                for (int i = 0; i < option_count; i++)
                {
                    aroma_dropdown_add_option(dd, options[i]);
                }
            }
            if (on_selection_changed)
                aroma_dropdown_set_on_change(dd, on_selection_changed, user_data);
            if (font)
                aroma_dropdown_set_font(dd, font);
            aroma_dropdown_setup_events(dd, aroma_ui_request_redraw, NULL);
        }
        return dd;
    }











    static inline AromaNode *aroma_ui_debug_overlay(
        AromaNode *parent,
        int x, int y, int width,
        AromaFont *font)
    {
        AromaNode *overlay = aroma_debug_overlay_create(parent, x, y, width);
        if (overlay && font)
        {
            aroma_debug_overlay_set_font(overlay, font);
        }
        return overlay;
    }

    static inline AromaNode *aroma_ui_calendar(
        AromaNode *parent, int x, int y, int width, int height,
        void (*on_select)(AromaNode *, int, int, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *c = aroma_calendar_create(parent, x, y, width, height);
        if (c)
        {
            if (on_select)
                aroma_calendar_set_on_select(c, on_select, user_data);
            if (font)
                aroma_calendar_set_font(c, font);
            aroma_calendar_setup_events(c, aroma_ui_request_redraw, NULL);
        }
        return c;
    }

    static inline AromaNode *aroma_ui_datepicker(
        AromaNode *parent, int x, int y, int width, int height,
        int year, int month, int day,
        void (*on_change)(AromaNode *, int, int, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *d = aroma_datepicker_create(parent, x, y, width, height,
                                               year, month, day);
        if (d)
        {
            if (on_change)
                aroma_datepicker_set_on_change(d, on_change, user_data);
            if (font)
                aroma_datepicker_set_font(d, font);
            aroma_datepicker_setup_events(d, aroma_ui_request_redraw, NULL);
        }
        return d;
    }

    static inline AromaNode *aroma_ui_timepicker(
        AromaNode *parent, int x, int y, int width, int height,
        int hour, int minute,
        void (*on_change)(AromaNode *, int, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *t = aroma_timepicker_create(parent, x, y, width, height,
                                               hour, minute);
        if (t)
        {
            if (on_change)
                aroma_timepicker_set_on_change(t, on_change, user_data);
            if (font)
                aroma_timepicker_set_font(t, font);
            aroma_timepicker_setup_events(t, aroma_ui_request_redraw, NULL);
        }
        return t;
    }

    static inline AromaNode *aroma_ui_stepper_numeric(
        AromaNode *parent, int x, int y, int width, int height,
        int min_val, int max_val, int value, int step,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *s = aroma_stepper_create_numeric(parent, x, y, width,
                                                    height, min_val, max_val,
                                                    value, step);
        if (s)
        {
            if (on_change)
                aroma_stepper_set_on_change(s, on_change, user_data);
            if (font)
                aroma_stepper_set_font(s, font);
            aroma_stepper_setup_events(s, aroma_ui_request_redraw, NULL);
        }
        return s;
    }

    static inline AromaNode *aroma_ui_stepper_steps(
        AromaNode *parent, int x, int y, int width, int height,
        const char **labels, int count,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *s = aroma_stepper_create_steps(parent, x, y, width, height,
                                                  labels, count);
        if (s)
        {
            if (on_change)
                aroma_stepper_set_on_change(s, on_change, user_data);
            if (font)
                aroma_stepper_set_font(s, font);
            aroma_stepper_setup_events(s, aroma_ui_request_redraw, NULL);
        }
        return s;
    }

    static inline AromaNode *aroma_ui_carousel(
        AromaNode *parent, int x, int y, int width, int height,
        void (*on_change)(AromaNode *, int, void *),
        void *user_data, AromaFont *font)
    {
        AromaNode *c = aroma_carousel_create(parent, x, y, width, height);
        if (c)
        {
            if (on_change)
                aroma_carousel_set_on_change(c, on_change, user_data);
            if (font)
                aroma_carousel_set_font(c, font);
            aroma_carousel_setup_events(c, aroma_ui_request_redraw, NULL);
        }
        return c;
    }













    typedef bool (*AromaBackCallback)(void *user_data);












    void aroma_ui_set_back_callback(AromaBackCallback callback, void *user_data);




    void aroma_ui_clear_back_callback(void);





    bool aroma_ui_has_back_callback(void);















    bool aroma_ui_handle_back_press(void);



#ifdef __ANDROID__

    struct android_app;





    void aroma_android_set_app(struct android_app *state);
#endif

#ifdef __cplusplus
}
#endif
#endif
