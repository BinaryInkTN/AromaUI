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

// Queue an off-thread raster (see implementation notes). Coalesces to the
// newest scroll position. `seq` tags the request so the widget can reject
// stale frames when a newer request superseded it. Returns false when no
// worker exists: fall back to aroma_webpage_render().
bool aroma_webpage_render_async(AromaWebPage *page, int scroll_x, int scroll_y,
                                unsigned seq);

// Non-blocking poll for a finished async frame. Returns NULL while the
// worker is still rasterizing. `out_seq` receives the job sequence the
// frame was rendered for: upload only when it matches the latest request.
// Pointer valid until next render/resize/load.
const unsigned char *aroma_webpage_poll_async(AromaWebPage *page, int *out_w,
                                              int *out_h, unsigned *out_seq);

// Advisory, non-consuming: true when a finished async frame is waiting for
// poll_async. Used as an idle wake-up hint (invalidate -> draw -> poll).
bool aroma_webpage_async_ready(AromaWebPage *page);

void aroma_webpage_content_size(AromaWebPage *page, int *w, int *h);

const unsigned char *aroma_webpage_pixels(AromaWebPage *page, int *w, int *h);

const char *aroma_webpage_eval(AromaWebPage *page, const char *code);

typedef void (*AromaWebPageBridgeFn)(const char *arg, char *out, size_t outlen,
                                     void *user);
bool aroma_webpage_add_bridge(AromaWebPage *page, const char *name,
                              AromaWebPageBridgeFn fn, void *user);

bool aroma_webpage_run_timers(AromaWebPage *page);

bool aroma_webpage_key(AromaWebPage *page, unsigned key_code);

// Editable-focus integration for platform IMEs:
// - has_focus_control: a text/password/textarea control holds page focus.
// - text_input: commit UTF-8 text (IME) into the focused control's cursor.
// - blur: drop page focus (focus loss / IME dismissed).
bool aroma_webpage_has_focus_control(AromaWebPage *page);
bool aroma_webpage_text_input(AromaWebPage *page, const char *utf8);
void aroma_webpage_blur(AromaWebPage *page);

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

void aroma_webpage_set_device_scale(AromaWebPage *page, float scale);
float aroma_webpage_get_device_scale(AromaWebPage *page);

#ifdef __cplusplus
}
#endif

#endif
