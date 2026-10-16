#ifndef AROMA_WEBVIEW_RENDER_H
#define AROMA_WEBVIEW_RENDER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaWebPage AromaWebPage;

AromaWebPage *aroma_webpage_create(int width, int height);
void aroma_webpage_destroy(AromaWebPage *page);
void aroma_webpage_resize(AromaWebPage *page, int width, int height);

bool aroma_webpage_load_html(AromaWebPage *page, const char *html,
                             const char *base_url);
bool aroma_webpage_load_url(AromaWebPage *page, const char *url);

const unsigned char *aroma_webpage_render(AromaWebPage *page, int scroll_x,
                                          int scroll_y, int *out_w, int *out_h);

void aroma_webpage_content_size(AromaWebPage *page, int *w, int *h);

const unsigned char *aroma_webpage_pixels(AromaWebPage *page, int *w, int *h);

const char *aroma_webpage_eval(AromaWebPage *page, const char *code);

typedef void (*AromaWebPageBridgeFn)(const char *arg, char *out, size_t outlen,
                                     void *user);
bool aroma_webpage_add_bridge(AromaWebPage *page, const char *name,
                              AromaWebPageBridgeFn fn, void *user);

bool aroma_webpage_run_timers(AromaWebPage *page);

bool aroma_webpage_key(AromaWebPage *page, unsigned key_code);

bool aroma_webpage_dom_dirty(AromaWebPage *page);

void aroma_webpage_set_view_scroll(AromaWebPage *page, int x, int y);

unsigned aroma_webpage_nav_gen(AromaWebPage *page);

bool aroma_webpage_click(AromaWebPage *page, int x, int y);

const char *aroma_webpage_url(AromaWebPage *page);
const char *aroma_webpage_title(AromaWebPage *page);

bool aroma_webpage_can_go_back(AromaWebPage *page);
bool aroma_webpage_can_go_forward(AromaWebPage *page);
bool aroma_webpage_go_back(AromaWebPage *page);
bool aroma_webpage_go_forward(AromaWebPage *page);

void aroma_webpage_set_text_scale(AromaWebPage *page, float scale);
float aroma_webpage_text_scale(AromaWebPage *page);

#ifdef __cplusplus
}
#endif

#endif
