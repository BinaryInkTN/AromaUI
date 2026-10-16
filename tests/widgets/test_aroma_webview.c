#include "test_aroma_webview.h"
#include "widgets/aroma_webview.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "aroma_event.h"
#include "aroma_ui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

#ifndef AROMA_WEBVIEW_FIXTURE_DIR
#define AROMA_WEBVIEW_FIXTURE_DIR "assets/webview"
#endif

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_webview_create_validation(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_webview_create(NULL, 0, 0, 100, 100) == NULL,
          "webview rejects NULL parent");
    CHECK(aroma_webview_create(root, 0, 0, 0, 100) == NULL,
          "webview rejects zero width");
    CHECK(aroma_webview_create(root, 0, 0, 100, -5) == NULL,
          "webview rejects negative height");

    AromaNode *wv = aroma_webview_create(root, 10, 20, 320, 240);
    CHECK(wv != NULL, "webview created");
    if (wv) {
        CHECK(aroma_webview_get_url(wv) == NULL ||
                  aroma_webview_get_url(wv)[0] == '\0',
              "no url initially");
        CHECK(!aroma_webview_can_go_back(wv), "no back initially");
        CHECK(!aroma_webview_can_go_forward(wv), "no forward initially");
        CHECK(!aroma_webview_go_back(wv), "go_back fails when empty");
        CHECK(!aroma_webview_load_url(wv, NULL), "load_url rejects NULL");
        CHECK(!aroma_webview_load_url(wv, ""), "load_url rejects empty");
        CHECK(!aroma_webview_load_html(wv, NULL, NULL), "load_html rejects NULL");
    }

    __destroy_node(root);
    __node_system_destroy();
}

static const char *kPageA =
    "<html><head><title>Alpha</title></head>"
    "<body style=\"margin:0;\"><h1>Alpha page</h1>"
    "<div style=\"height:600px;\">tall</div></body></html>";
static const char *kPageB =
    "<html><head><title>Beta</title></head>"
    "<body style=\"margin:0;\"><p>Beta page</p></body></html>";

static void test_webview_html_navigation(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *wv = aroma_webview_create(root, 0, 0, 320, 240);
    CHECK(wv != NULL, "nav node created");
    if (!wv) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    CHECK(aroma_webview_load_html(wv, kPageA, NULL), "load_html A works");
    const char *title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Alpha") == 0, "title A reported");
    int cw = 0, ch = 0;
    aroma_webview_get_content_size(wv, &cw, &ch);
    CHECK(cw == 320, "content width matches viewport");
    CHECK(ch >= 600, "tall content measured");

    aroma_webview_scroll_to(wv, 0, 99999);
    int sx = -1, sy = -1;
    aroma_webview_get_scroll(wv, &sx, &sy);
    CHECK(sx == 0 && sy == ch - 240, "scroll clamped to content");

    CHECK(aroma_webview_load_html(wv, kPageB, NULL), "load_html B works");
    title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Beta") == 0, "title B reported");
    aroma_webview_get_scroll(wv, &sx, &sy);
    CHECK(sx == 0 && sy == 0, "scroll resets on navigate");

    CHECK(aroma_webview_can_go_back(wv), "can go back after two loads");
    CHECK(aroma_webview_go_back(wv), "go_back works");
    title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Alpha") == 0, "back restores A");
    CHECK(aroma_webview_can_go_forward(wv), "can go forward");
    CHECK(aroma_webview_go_forward(wv), "go_forward works");
    title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Beta") == 0, "forward restores B");

    CHECK(aroma_webview_reload(wv), "reload works");

    aroma_webview_set_text_scale(wv, 1.5f);
    CHECK(aroma_webview_get_text_scale(wv) == 1.5f, "text scale set");
    aroma_webview_set_text_scale(wv, 1.0f);

    aroma_webview_set_size(wv, 200, 200);
    aroma_webview_get_content_size(wv, &cw, &ch);
    CHECK(cw == 200, "content width follows resize");
    aroma_webview_set_position(wv, 5, 6);
    aroma_webview_scroll_by(wv, 0, 0);

    aroma_webview_set_on_navigate(wv, NULL, NULL);
    aroma_webview_set_on_title(wv, NULL, NULL);
    aroma_webview_set_on_error(wv, NULL, NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_webview_file_pages(void); // fwd

static int s_bridge_calls = 0;
static void test_bridge(AromaNode *node, const char *name, const char *arg,
                        char *out, size_t outlen, void *user)
{
    (void)node;
    (void)user;
    s_bridge_calls++;
    snprintf(out, outlen, "echo:%s:%s", name, arg);
}

static const char *kJsPage =
    "<html><head><title>JS</title></head>"
    "<body style=\"margin:0;\">"
    "<div id=\"x\">old</div>"
    "<p class=\"n\">a</p><p class=\"n\">b</p>"
    "<button id=\"b\">tap</button>"
    "<script>"
    "window.__n = 0;"
    "document.getElementById('b').addEventListener('click', function(){ window.__n++; });"
    "setTimeout(function(){ document.getElementById('x').textContent = 'timed'; }, 30);"
    "</script>"
    "</body></html>";

static void js_eval_is(AromaNode *wv, const char *code, const char *want, const char *name)
{
    const char *got = aroma_webview_eval_js(wv, code);
    CHECK(got && strcmp(got, want) == 0, name);
}

static void test_webview_js(void)
{
    __node_system_init();
    aroma_event_system_init();
    aroma_timer_init();
    AromaNode *root = make_root();
    aroma_event_set_root(root);
    AromaNode *wv = aroma_webview_create(root, 0, 0, 320, 240);
    CHECK(wv != NULL, "js node created");
    if (!wv) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_webview_load_html(wv, kJsPage, NULL), "js page loads");

    js_eval_is(wv, "1 + 2", "3", "js arithmetic evaluates");
    js_eval_is(wv, "document.getElementById('x').textContent", "old", "js reads DOM text");
    js_eval_is(wv, "document.querySelectorAll('p.n').length", "2", "js selector counts");
    js_eval_is(wv, "document.querySelector('h9')", "null", "js missing selector is null");

    js_eval_is(wv, "document.getElementById('x').textContent = 'new'; 'ok'", "ok",
               "js write runs");
    js_eval_is(wv, "document.getElementById('x').textContent", "new", "js write persists");

    js_eval_is(wv, "document.getElementById('b').click(); window.__n", "1",
               "js click fires listener");
    js_eval_is(wv, "document.getElementById('b').click(); window.__n", "2",
               "js click fires again");

    js_eval_is(wv, "document.cookie = 'a=1'; document.cookie", "a=1", "js cookies round-trip");
    js_eval_is(wv, "localStorage.setItem('k','v'); localStorage.getItem('k')", "v",
               "js localStorage round-trip");

    s_bridge_calls = 0;
    CHECK(aroma_webview_add_js_bridge(wv, "nativeEcho", test_bridge, NULL),
          "js bridge registers");
    js_eval_is(wv, "nativeEcho('hi')", "echo:nativeEcho:hi", "js bridge round-trip");
    CHECK(s_bridge_calls == 1, "js bridge invoked once");

    usleep(60 * 1000);
    aroma_timer_tick(aroma_time_now_ms());
    usleep(80 * 1000);
    aroma_timer_tick(aroma_time_now_ms());
    js_eval_is(wv, "document.getElementById('x').textContent", "timed",
               "js setTimeout fires via pump");

    // forms: typing through key events, checkbox toggle, GET submit
    char action[1024];
    snprintf(action, sizeof(action), "file://%s/page2.html", AROMA_WEBVIEW_FIXTURE_DIR);
    char form_html[2048];
    snprintf(form_html, sizeof(form_html),
             "<html><head><title>Form</title></head><body style=\"margin:0;\">"
             "<form id=\"f\" action=\"%s\">"
             "<input type=\"text\" name=\"q\" id=\"q\" value=\"\">"
             "<input type=\"checkbox\" name=\"c\" id=\"c\" value=\"yes\">"
             "<input type=\"submit\" id=\"s\" value=\"Go\">"
             "</form></body></html>",
             action);
    CHECK(aroma_webview_load_html(wv, form_html, NULL), "js form page loads");
    js_eval_is(wv, "document.getElementById('q').click(); 'focused'", "focused",
               "js input focuses");
    const char *keys = "hi";
    for (size_t i = 0; i < strlen(keys); i++) {
        AromaEvent *ev = aroma_event_create_key(EVENT_TYPE_KEY_PRESS, wv->node_id,
                                                (uint32_t)keys[i], 0);
        if (ev) {
            aroma_event_dispatch(ev);
            aroma_event_destroy(ev);
        }
    }
    js_eval_is(wv, "document.getElementById('q').value", "hi", "js key events type text");
    js_eval_is(wv, "document.getElementById('c').click(); "
                   "document.getElementById('c').checked ? 'on' : 'off'",
               "on", "js checkbox toggles");
    js_eval_is(wv, "document.getElementById('s').click(); 'sent'", "sent",
               "js submit clicks");
    const char *title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Page Two") == 0, "js form submit navigates");
    const char *u = aroma_webview_get_url(wv);
    CHECK(u && strstr(u, "q=hi") != NULL && strstr(u, "c=yes") != NULL,
          "js submit carries fields");

    // reload spam must keep a coherent page (use-after-free regression)
    CHECK(aroma_webview_load_html(wv, kJsPage, NULL), "js reload base loads");
    for (int i = 0; i < 25; i++) {
        if (!aroma_webview_reload(wv)) {
            CHECK(0, "js reload spam stays successful");
            break;
        }
    }
    title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "JS") == 0, "js title survives reload spam");
    js_eval_is(wv, "window.__n", "0", "js state resets cleanly on reload");
    js_eval_is(wv, "document.getElementById('x').textContent", "old",
               "js DOM intact after reload spam");

    // native mouse path: real CLICK+RELEASE events through the widget
    const char *click_html =
        "<html><head><title>C</title></head><body style=\"margin:0;\">"
        "<button id=\"t\" type=\"button\" style=\"width:100px;height:40px;margin:0;\">Tap</button>"
        "<script>window.__n = 0;"
        "document.getElementById('t').addEventListener('click', function(){ window.__n++; });"
        "</script></body></html>";
    CHECK(aroma_webview_load_html(wv, click_html, NULL), "native click page loads");
    AromaEvent *dn = aroma_event_create(EVENT_TYPE_MOUSE_CLICK, wv->node_id);
    AromaEvent *up = aroma_event_create(EVENT_TYPE_MOUSE_RELEASE, wv->node_id);
    CHECK(dn && up, "native click events created");
    if (dn && up) {
        dn->data.mouse.x = 50;
        dn->data.mouse.y = 20;
        up->data.mouse.x = 50;
        up->data.mouse.y = 20;
        aroma_event_dispatch(dn);
        aroma_event_dispatch(up);
        aroma_event_destroy(dn);
        aroma_event_destroy(up);
    }
    js_eval_is(wv, "window.__n", "1", "native mouse click presses button");
    CHECK(aroma_ui_get_focused_node() == wv, "native click focuses webview for keys");

    // fragment edits on detached nodes (createElement flows must not crash)
    js_eval_is(wv,
               "var d = document.createElement('div');"
               "d.textContent = 'made';"
               "d.textContent;",
               "made", "js detached textContent round-trips");
    js_eval_is(wv,
               "var e = document.createElement('div');"
               "e.innerHTML = '<b>bold</b>';"
               "e.textContent;",
               "bold", "js detached innerHTML parses");
    js_eval_is(wv,
               "var u = document.createElement('ul');"
               "u.innerHTML = '<li>x</li>';"
               "document.body.appendChild(u);"
               "document.body.children.length > 0 ? 'ok' : 'bad'",
               "ok", "js detached innerHTML then attach works");

    __destroy_node(root);
    __node_system_destroy();
}

static void test_webview_file_pages(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *wv = aroma_webview_create(root, 0, 0, 320, 240);
    CHECK(wv != NULL, "file node created");
    if (!wv) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    char url[1024];
    snprintf(url, sizeof(url), "file://%s/page1.html", AROMA_WEBVIEW_FIXTURE_DIR);
    CHECK(aroma_webview_load_url(wv, url), "file page1 loads");
    const char *title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Page One") == 0, "file title parsed");
    const char *u = aroma_webview_get_url(wv);
    CHECK(u && strstr(u, "page1.html") != NULL, "url remembered");
    int cw = 0, ch = 0;
    aroma_webview_get_content_size(wv, &cw, &ch);
    CHECK(ch >= 600, "file page content measured (incl. spacer)");

    snprintf(url, sizeof(url), "file://%s/page2.html", AROMA_WEBVIEW_FIXTURE_DIR);
    CHECK(aroma_webview_load_url(wv, url), "file page2 loads");
    CHECK(aroma_webview_can_go_back(wv), "file history has back");
    CHECK(aroma_webview_go_back(wv), "file go_back works");
    title = aroma_webview_get_title(wv);
    CHECK(title && strcmp(title, "Page One") == 0, "file back restores title");

    CHECK(!aroma_webview_load_url(wv, "file:///nonexistent/missing.html"),
          "missing file fails cleanly");

    __destroy_node(root);
    __node_system_destroy();
}

void run_webview_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma WebView Tests ===\n");
    test_webview_create_validation();
    test_webview_html_navigation();
    test_webview_file_pages();
    test_webview_js();
    printf("Aroma WebView: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
