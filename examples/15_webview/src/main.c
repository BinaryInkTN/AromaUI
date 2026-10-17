#include <aroma.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;
static AromaNode *g_web;
static AromaNode *g_addr;

#define PAGE_CSS "margin:0;padding:16px;font-family:sans-serif;background:#ffffff;color:#222;"
#define H1_BLUE "<h1 style=\"color:#0066cc;\">"

static const char *kDocs =
    "<html><head><title>Docs</title></head>"
    "<body style=\"" PAGE_CSS "\">"
    H1_BLUE "Aroma WebView</h1>"
    "<p>Every page here is <b>offline</b>: inline HTML rendered by the "
    "vendored litehtml engine. No system browser, no network.</p>"
    "<ul>"
    "<li><b>Styling</b> - CSS text, colors, borders, gradients, tables</li>"
    "<li><b>JS</b> - scripts, DOM, events, native bridge</li>"
    "<li><b>Forms</b> - inputs, checkboxes, radios, selects</li>"
    "</ul>"
    "<p>Use the buttons below to browse. Back and Reload walk history.</p>"
    "</body></html>";

static const char *kStyle =
    "<html><head><title>Styling</title></head>"
    "<body style=\"" PAGE_CSS "\">"
    H1_BLUE "Styling</h1>"
    "<p style=\"color:#666;\">Cascading stylesheets, computed locally.</p>"
    "<div style=\"background:linear-gradient(#eef,#9db8dd);"
    "border:2px solid #0066cc;padding:8px;\">gradient box</div>"
    "<table border=\"1\" cellpadding=\"6\" style=\"margin-top:8px;\">"
    "<tr><td>litehtml</td><td>tables work</td></tr>"
    "<tr><td>row 2</td><td>with borders</td></tr></table>"
    "<ul><li>disc item</li><li>another item</li></ul>"
    "<ol><li>first</li><li>second</li></ol>"
    "</body></html>";

static const char *kJs =
    "<html><head><title>JavaScript</title></head>"
    "<body style=\"" PAGE_CSS "\">"
    H1_BLUE "JavaScript</h1>"
    "<p>Counter: <b id=\"count\">0</b> "
    "<button id=\"tap\" type=\"button\">Tap me</button></p>"
    "<p><button id=\"stylebtn\" type=\"button\">Recolor box</button></p>"
    "<div id=\"box\" style=\"width:120px;height:40px;background:#cc0000;\"></div>"
    "<script>"
    "var n = 0;"
    "document.getElementById('tap').addEventListener('click', function(){"
    "  n++;"
    "  document.getElementById('count').textContent = n;"
    "  nativeToast('tap ' + n);"
    "});"
    "document.getElementById('stylebtn').addEventListener('click', function(){"
    "  var b = document.getElementById('box');"
    "  b.style.setProperty('background', b.style.getPropertyValue('background') === '#cc0000' ? '#00aa00' : '#cc0000');"
    "});"
    "</script>"
    "<p>QuickJS runs scripts, timers, DOM, events and bridges.</p>"
    "</body></html>";

static const char *kForms =
    "<html><head><title>Forms</title></head>"
    "<body style=\"" PAGE_CSS "\">"
    H1_BLUE "Forms</h1>"
    "<form id=\"f\">"
    "<p>Name <input type=\"text\" name=\"name\" id=\"name\" value=\"Ada\"></p>"
    "<p>Pass <input type=\"password\" name=\"pw\" id=\"pw\" value=\"s3cret\"></p>"
    "<p><input type=\"checkbox\" name=\"sub\" id=\"sub\" checked> Subscribe</p>"
    "<p><input type=\"radio\" name=\"plan\" value=\"free\" checked> Free "
    "<input type=\"radio\" name=\"plan\" value=\"pro\"> Pro</p>"
    "<p><select name=\"color\" id=\"color\">"
    "<option value=\"r\">Red</option><option value=\"g\" selected>Green</option>"
    "<option value=\"b\">Blue</option></select></p>"
    "<p><textarea name=\"bio\" id=\"bio\" rows=\"3\" cols=\"24\">hello</textarea></p>"
    "<p><button id=\"preview\" type=\"button\">Preview</button> "
    "<input type=\"reset\" value=\"Reset\"></p>"
    "</form>"
    "<script>"
    "document.getElementById('preview').addEventListener('click', function(){"
    "  var q = function(id){ return document.getElementById(id).value; };"
    "  nativeToast(q('name') + '/' + q('color') + '/' + q('bio'));"
    "});"
    "</script>"
    "<p>Tap a field, then type on your keyboard.</p>"
    "</body></html>";

static void show_addr(void)
{
    if (!g_addr || !g_web)
        return;
    const char *url = aroma_webview_get_url(g_web);
    const char *title = aroma_webview_get_title(g_web);
    char buf[160];
    if (url && url[0])
        snprintf(buf, sizeof(buf), "%s", url);
    else if (title && title[0])
        snprintf(buf, sizeof(buf), "%s", title);
    else
        snprintf(buf, sizeof(buf), "local page");
    aroma_label_set_text(g_addr, buf);
}

static void show(const char *html)
{
    if (!g_web)
        return;
    aroma_webview_load_html(g_web, html, NULL);
    show_addr();
}

static void page_docs_cb(void *u) { (void)u; show(kDocs); }
static void page_style_cb(void *u) { (void)u; show(kStyle); }
static void page_js_cb(void *u) { (void)u; show(kJs); }
static void page_forms_cb(void *u) { (void)u; show(kForms); }
static void back_cb(void *u)
{
    (void)u;
    if (g_web && aroma_webview_go_back(g_web))
        show_addr();
}
static void forward_cb(void *u)
{
    (void)u;
    if (g_web && aroma_webview_go_forward(g_web))
        show_addr();
}
static void reload_cb(void *u)
{
    (void)u;
    if (g_web && aroma_webview_reload(g_web))
        show_addr();
}

static void toast_bridge(AromaNode *node, const char *name, const char *arg,
                         char *out, size_t outlen, void *user)
{
    (void)node;
    (void)name;
    (void)user;
    printf("webview: %s\n", arg);
#ifdef __ANDROID__
    // Real system toast so the JS `nativeToast(...)` bridge is visible on
    // device (printf only reaches the terminal on desktop).
    aroma_android_toast(arg, false);
#endif
    snprintf(out, outlen, "ok");
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("page_docs", INCENSE_CALLBACK_VOID_PTR, page_docs_cb, NULL);
    IncenseRegisterCallback("page_style", INCENSE_CALLBACK_VOID_PTR, page_style_cb, NULL);
    IncenseRegisterCallback("page_js", INCENSE_CALLBACK_VOID_PTR, page_js_cb, NULL);
    IncenseRegisterCallback("page_forms", INCENSE_CALLBACK_VOID_PTR, page_forms_cb, NULL);
    IncenseRegisterCallback("back", INCENSE_CALLBACK_VOID_PTR, back_cb, NULL);
    IncenseRegisterCallback("forward", INCENSE_CALLBACK_VOID_PTR, forward_cb, NULL);
    IncenseRegisterCallback("reload", INCENSE_CALLBACK_VOID_PTR, reload_cb, NULL);
    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    if (!window) {
        int count = 0;
        const IncenseError *errors = IncenseGetErrors(&count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
        aroma_font_destroy(g_font);
        aroma_font_destroy(g_icon_font);
        aroma_ui_shutdown();
        return 1;
    }
    g_web = IncenseFindWidget(g_registry, "web");
    g_addr = IncenseFindWidget(g_registry, "addr");
    if (g_web)
        aroma_webview_add_js_bridge(g_web, "nativeToast", toast_bridge, NULL);
    show(kDocs);
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
    IncenseFreeRegistry(g_registry);
    aroma_font_destroy(g_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
#endif
