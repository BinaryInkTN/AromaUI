




















#include "widgets/aroma_tooltip.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_dp.h"
#include <string.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_TOOLTIP_TEXT_MAX 128
#define AROMA_TOOLTIP_WIDTH_DP 140
#define AROMA_TOOLTIP_HEIGHT_DP 32
#define AROMA_TOOLTIP_CORNER_RADIUS_DP 6
#define AROMA_TOOLTIP_PADDING_X_DP 8
#define AROMA_TOOLTIP_TEXT_Y_OFFSET_DP 20

#ifdef __ANDROID__
static inline int tooltip_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float tooltip_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int tooltip_dp(int dp) { return dp; }
static inline float tooltip_dp_f(float dp) { return dp; }
#endif

typedef struct   AromaTooltip {
    AromaRect rect;
    char text[AROMA_TOOLTIP_TEXT_MAX];
    AromaTooltipPosition position;
    bool visible;
    AromaFont* font;
    float corner_radius;
    float text_scale;
    uint32_t bg_color;
    bool use_theme_colors;
} AromaTooltip;

AromaNode* aroma_tooltip_create(AromaNode* parent, const char* text, int x, int y, AromaTooltipPosition position)
{
    if (!parent || !text) return NULL;



#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
#endif

    AromaTooltip* tip = (AromaTooltip*)aroma_widget_alloc(sizeof(AromaTooltip));
    if (!tip) return NULL;

    memset(tip, 0, sizeof(AromaTooltip));
    tip->rect.x = x;
    tip->rect.y = y;
    tip->rect.width = tooltip_dp(AROMA_TOOLTIP_WIDTH_DP);
    tip->rect.height = tooltip_dp(AROMA_TOOLTIP_HEIGHT_DP);
    tip->position = position;
    tip->visible = false;
    strncpy(tip->text, text, AROMA_TOOLTIP_TEXT_MAX - 1);
    tip->text[AROMA_TOOLTIP_TEXT_MAX - 1] = '\0';
    AromaTheme theme = aroma_theme_get_global();
    tip->corner_radius = tooltip_dp_f((float)AROMA_TOOLTIP_CORNER_RADIUS_DP);
    tip->text_scale = 1.0f;
    tip->bg_color = aroma_color_adjust(theme.colors.text_primary, -0.6f);
    tip->use_theme_colors = true;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, tip);
    if (!node) {
        aroma_widget_free(tip);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_tooltip_draw);

    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

void aroma_tooltip_set_text(AromaNode* tooltip_node, const char* text)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr || !text) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    strncpy(tip->text, text, AROMA_TOOLTIP_TEXT_MAX - 1);
    tip->text[AROMA_TOOLTIP_TEXT_MAX - 1] = '\0';
    aroma_node_invalidate(tooltip_node);
}

void aroma_tooltip_show(AromaNode* tooltip_node, int delay_ms)
{
    (void)delay_ms;
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->visible = true;
    aroma_node_invalidate(tooltip_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_tooltip_hide(AromaNode* tooltip_node)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->visible = false;
    aroma_node_invalidate(tooltip_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_tooltip_set_font(AromaNode* tooltip_node, AromaFont* font)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    tip->font = font;
}

void aroma_tooltip_draw(AromaNode* tooltip_node, size_t window_id)
{
    if (!tooltip_node || !tooltip_node->node_widget_ptr) return;
    AromaTooltip* tip = (AromaTooltip*)tooltip_node->node_widget_ptr;
    if (!tip->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    if (aroma_node_is_hidden(tooltip_node)) return;
    AromaTheme theme = aroma_theme_get_global();

    if (tip->use_theme_colors) {
        tip->bg_color = aroma_color_adjust(theme.colors.text_primary, -0.6f);
    }

    gfx->fill_rectangle(window_id, tip->rect.x, tip->rect.y, tip->rect.width, tip->rect.height,
                        tip->bg_color, true, tip->corner_radius);

    if (tip->font && gfx->render_text) {
        gfx->render_text(window_id, tip->font, tip->text, tip->rect.x + tooltip_dp(AROMA_TOOLTIP_PADDING_X_DP), tip->rect.y + tooltip_dp(AROMA_TOOLTIP_TEXT_Y_OFFSET_DP), theme.colors.surface, tip->text_scale);
    }
}

void aroma_tooltip_destroy(AromaNode* tooltip_node)
{
    if (!tooltip_node) return;
    if (tooltip_node->node_widget_ptr) {
        aroma_widget_free(tooltip_node->node_widget_ptr);
        tooltip_node->node_widget_ptr = NULL;
    }
    __destroy_node(tooltip_node);
}