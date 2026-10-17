#ifndef AROMA_WEBVIEW_H
#define AROMA_WEBVIEW_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AromaWebViewNavigateCb)(AromaNode *node, const char *url, void *user_data);
typedef void (*AromaWebViewTitleCb)(AromaNode *node, const char *title, void *user_data);
typedef void (*AromaWebViewErrorCb)(AromaNode *node, const char *message, void *user_data);
typedef void (*AromaWebViewLoadCb)(AromaNode *node, void *user_data);
typedef void (*AromaWebViewJsBridgeCb)(AromaNode *node, const char *name, const char *arg,
                                       char *out, size_t outlen, void *user_data);

// EVENT_TYPE_CUSTOM payload: UTF-8 text committed by the platform IME.
// data is a heap string owned by the event (freed via its free_data hook).
#define AROMA_WEBVIEW_TEXT_INPUT_EVENT 0x57545631u

AromaNode *aroma_webview_create(AromaNode *parent, int x, int y, int width, int height);

bool aroma_webview_load_url(AromaNode *webview_node, const char *url);
bool aroma_webview_load_html(AromaNode *webview_node, const char *html,
                             const char *base_url);
bool aroma_webview_reload(AromaNode *webview_node);

bool aroma_webview_can_go_back(AromaNode *webview_node);
bool aroma_webview_can_go_forward(AromaNode *webview_node);
bool aroma_webview_go_back(AromaNode *webview_node);
bool aroma_webview_go_forward(AromaNode *webview_node);

const char *aroma_webview_get_url(AromaNode *webview_node);
const char *aroma_webview_get_title(AromaNode *webview_node);

void aroma_webview_set_text_scale(AromaNode *webview_node, float scale);
float aroma_webview_get_text_scale(AromaNode *webview_node);

void aroma_webview_scroll_to(AromaNode *webview_node, int x, int y);
void aroma_webview_scroll_by(AromaNode *webview_node, int dx, int dy);
void aroma_webview_get_scroll(AromaNode *webview_node, int *x, int *y);
void aroma_webview_get_content_size(AromaNode *webview_node, int *w, int *h);

void aroma_webview_set_on_navigate(AromaNode *webview_node,
                                   AromaWebViewNavigateCb cb, void *user_data);
void aroma_webview_set_on_title(AromaNode *webview_node,
                               AromaWebViewTitleCb cb, void *user_data);
void aroma_webview_set_on_error(AromaNode *webview_node,
                               AromaWebViewErrorCb cb, void *user_data);
void aroma_webview_set_on_load(AromaNode *webview_node,
                               AromaWebViewLoadCb cb, void *user_data);

const char *aroma_webview_eval_js(AromaNode *webview_node, const char *code);
bool aroma_webview_add_js_bridge(AromaNode *webview_node, const char *name,
                                 AromaWebViewJsBridgeCb cb, void *user_data);

void aroma_webview_set_size(AromaNode *webview_node, int width, int height);
void aroma_webview_set_position(AromaNode *webview_node, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
