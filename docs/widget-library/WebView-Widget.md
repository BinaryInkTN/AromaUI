# WebView Widget

Renders real HTML/CSS pages inside your UI: local strings, local files, or live websites, with links, history, scrolling, and text scaling. No system browser required: pages render with the **vendored litehtml engine** (`vendors/litehtml`) into a GPU texture.

## Getting Started

```c
AromaNode *web = aroma_webview_create(root, 20, 84, 320, 360);
aroma_webview_load_html(web,
    "<html><head><title>Hi</title></head>"
    "<body><h1>Hello WebView</h1></body></html>", NULL);
```

Load a live page instead:

```c
AromaNode *web = aroma_ui_webview(root, "https://example.com", 20, 84, 320, 360);
```

## What It Supports

- **HTML + CSS**: text, headings, lists, tables, borders, backgrounds, linear gradients, images (`PNG/JPG/GIF/BMP`, plus `data:` URLs)
- **JavaScript**: QuickJS engine runs `<script>` tags (inline + `src`), with `window`, `document`, `console`, `setTimeout`/`setInterval`, `location`, `localStorage`, and cookies
- **DOM**: `getElementById`, `querySelector(All)`, `getElementsBy*`, `createElement`, `innerHTML`/`textContent`, attributes, `style`/`classList`, events with bubbling + `preventDefault`
- **Forms**: text/password inputs (keyboard editable), checkboxes, radios, selects, textareas, buttons, GET submission
- **Native bridge**: expose C functions to JS (`aroma_webview_add_js_bridge`) and evaluate JS from C (`aroma_webview_eval_js`)
- **Links**: tapping a link navigates; Back/Forward walk the history
- **Scrolling**: mouse drag, mouse wheel, and touch drag
- **Text scaling**: accessibility-style zoom via `aroma_webview_set_text_scale`
- **Offline pages**: `load_html` needs no network at all

Known limitations (v1): no `fetch`/`XHR` in JS, no form POST, no `<video>`/`<audio>` tags, no border-radius clipping, radial/conic gradients fall back to solid fills, `removeEventListener` drops the most recent matching listener, `document.title` is read-only, `<select multiple>` submits all selected values. Pages fetch synchronously, so large sites pause the UI briefly while loading.

## API Reference

| Function | Purpose |
|---|---|
| `aroma_webview_create(parent, x, y, w, h)` | Create the widget |
| `aroma_webview_load_url(node, url)` | Load `http(s)://`, `file://`, or plain path |
| `aroma_webview_load_html(node, html, base_url)` | Render an HTML string |
| `aroma_webview_reload` | Reload the current page |
| `aroma_webview_go_back / go_forward / can_go_back / can_go_forward` | History |
| `aroma_webview_get_url / get_title` | Current address and `<title>` |
| `aroma_webview_set_text_scale / get_text_scale` | Text zoom (0.25–4.0) |
| `aroma_webview_scroll_to / scroll_by / get_scroll` | Programmatic scrolling |
| `aroma_webview_get_content_size` | Full page size (for scrollbars) |
| `aroma_webview_set_on_navigate / on_title / on_error / on_load` | Lifecycle callbacks |
| `aroma_webview_eval_js(node, code)` | Run JS, returns the stringified result |
| `aroma_webview_add_js_bridge(node, name, cb, user)` | Expose C as `window.<name>(str)` |
| `aroma_webview_set_size / set_position` | Geometry (re-renders content) |

## JavaScript Bridge Example

```c
static void toast(AromaNode *node, const char *name, const char *arg,
                  char *out, size_t outlen, void *user)
{
    (void)node; (void)name; (void)user;
    printf("JS says: %s\n", arg);
    snprintf(out, outlen, "ack");
}

aroma_webview_add_js_bridge(web, "nativeToast", toast, NULL);
aroma_webview_load_html(web,
    "<button onclick=\"nativeToast('hi')\">tap</button>", NULL);
```

Inside the page, `document.cookie` persists per page, `localStorage` is an
in-memory per-page store, and `setTimeout`/`setInterval` are pumped on the
UI loop (about every 30 ms).

## Incense (declarative UI)

```aroma
WebView {
    id: "web"
    url: "https://example.com"
    x: 20 y: 84 width: 320 height: 360
}
```

Supported properties: `url` (or `src`), `html` (inline page source), `text_scale`, plus the standard `x`, `y`, `width`, `height`, `z_index` / `visible` / animation props.

## How It Works

The page renders in software to an RGBA buffer (FreeType text, bilinear images, CSS boxes), which streams into a single reused GPU texture. Scrolling just re-renders the visible slice and updates the texture in place. Remote resources use the same HTTP cache as the image widget; `file://` pages load straight from disk.

## Example

See `examples/15_webview`: offline pages (Docs, Styling, JS, Forms) with history and reload buttons.
