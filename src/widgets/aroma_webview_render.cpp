#include "aroma_webview_render.h"

#include <litehtml.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <litehtml/render_item.h>
#include <litehtml/render_image.h>
#include <litehtml/el_text.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "backends/graphics/utils/stb_image.h"

#include "aroma_http.h"
#include "aroma_native_utils.h"
#include "aroma_ubuntu_font.h"
#include "core/aroma_logger.h"

#ifdef AROMA_WEBVIEW_JS
#include <quickjs.h>
#endif
#include <chrono>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

class AromaControl;
struct AromaWebPage;
#ifdef AROMA_WEBVIEW_JS
class JsEngine;
#endif

namespace {

int px(litehtml::pixel_t v) { return (int)v; }

struct IRect {
    int x, y, w, h;
    bool empty() const { return w <= 0 || h <= 0; }
};

IRect intersect_rect(const IRect &a, const IRect &b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

uint32_t utf8_next(const char *&p, const char *end)
{
    if (p >= end)
        return 0;
    unsigned char c = (unsigned char)*p++;
    if (c < 0x80)
        return c;
    uint32_t cp = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else
        return 0xFFFD;
    for (int i = 0; i < extra; i++) {
        if (p >= end)
            return 0xFFFD;
        unsigned char d = (unsigned char)*p++;
        if ((d & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (d & 0x3F);
    }
    return cp;
}

bool base64_decode(const std::string &in, std::vector<uint8_t> &out)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    uint32_t bits = 0;
    int nbits = 0;
    for (char c : in) {
        if (c == '=')
            break;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        int v = val(c);
        if (v < 0)
            return false;
        bits = (bits << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            out.push_back((uint8_t)(bits >> nbits));
            bits &= (nbits == 0) ? 0 : ((1u << nbits) - 1);
        }
    }
    return true;
}

std::string url_unescape(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size() &&
            isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out.push_back((char)strtol(hex, nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

bool read_local_file(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > (long)(64 * 1024 * 1024)) {
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    bool ok = size == 0 || fread(out.data(), 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    return ok;
}

bool fetch_bytes(const std::string &url, std::vector<uint8_t> &out)
{
    if (url.compare(0, 5, "data:") == 0) {
        size_t comma = url.find(',');
        if (comma == std::string::npos)
            return false;
        std::string meta = url.substr(5, comma - 5);
        std::string data = url.substr(comma + 1);
        if (meta.find(";base64") != std::string::npos)
            return base64_decode(data, out);
        std::string raw = url_unescape(data);
        out.assign(raw.begin(), raw.end());
        return true;
    }

    std::string noq = url;
    size_t q = noq.find_first_of("?#");
    bool had_query = q != std::string::npos;
    if (had_query)
        noq.erase(q);
    if (url.compare(0, 7, "file://") == 0)
        return read_local_file(noq.substr(7), out);
    if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0) {
        char cache[1024];
        aroma_http_cache_path_for_url(url.c_str(), cache, sizeof(cache));
        if (!cache[0])
            return false;
        FILE *probe = fopen(cache, "rb");
        if (!probe) {
            if (!aroma_http_fetch_to_file(url.c_str(), cache))
                return false;
        } else {
            fclose(probe);
        }
        return read_local_file(cache, out);
    }

    char resolved[1024];
    const char *p = aroma_resolve_asset_path(noq.c_str(), resolved, sizeof(resolved));
    if (p && read_local_file(p, out))
        return true;
    return read_local_file(noq, out);
}

std::string fetch_text(const std::string &url)
{
    std::vector<uint8_t> bytes;
    if (!fetch_bytes(url, bytes))
        return {};
    return std::string(bytes.begin(), bytes.end());
}

static std::string cookie_jar_str(const std::map<std::string, std::string> &jar)
{
    std::string out;
    for (const auto &kv : jar) {
        if (!out.empty())
            out += "; ";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

static void cookie_jar_store(std::map<std::string, std::string> &jar, const char *setc)
{
    if (!setc || !setc[0])
        return;

    std::string s = setc;
    size_t p = 0;
    while (p < s.size()) {
        size_t semi = s.find(';', p);
        std::string pair = s.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string name = pair.substr(0, eq);
            std::string value = pair.substr(eq + 1);
            while (!name.empty() && isspace((unsigned char)name.front()))
                name.erase(name.begin());
            while (!name.empty() && isspace((unsigned char)name.back()))
                name.pop_back();
            while (!value.empty() && isspace((unsigned char)value.front()))
                value.erase(value.begin());
            while (!value.empty() && isspace((unsigned char)value.back()))
                value.pop_back();
            if (!name.empty())
                jar[name] = value;
        }
        if (semi == std::string::npos)
            break;
        p = semi + 1;
    }
}

static bool fetch_jar_bytes(std::map<std::string, std::string> *jar, const std::string &url,
                            std::vector<uint8_t> &out)
{
    bool is_http = url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
    if (!is_http || !jar)
        return fetch_bytes(url, out);
    char cache[1024];
    aroma_http_cache_path_for_url(url.c_str(), cache, sizeof(cache));
    if (!cache[0])
        return false;
    FILE *probe = fopen(cache, "rb");
    if (!probe) {
        std::string hdr = cookie_jar_str(*jar);
        char setc[2048] = {0};
        if (!aroma_http_fetch_to_file_cookies(url.c_str(), cache,
                                              hdr.empty() ? nullptr : hdr.c_str(),
                                              setc, sizeof(setc)))
            return false;
        cookie_jar_store(*jar, setc);
    } else {
        fclose(probe);
    }
    return read_local_file(cache, out);
}

static std::string fetch_jar_text(std::map<std::string, std::string> *jar, const std::string &url)
{
    std::vector<uint8_t> bytes;
    if (!fetch_jar_bytes(jar, url, bytes))
        return {};
    return std::string(bytes.begin(), bytes.end());
}

std::string join_url_path(const std::string &base, const std::string &rel)
{
    if (rel.empty())
        return base;
    if (rel.compare(0, 7, "http://") == 0 || rel.compare(0, 8, "https://") == 0 ||
        rel.compare(0, 7, "file://") == 0 || rel.compare(0, 5, "data:") == 0 ||
        (!rel.empty() && rel[0] == '/'))
        return rel;

    size_t scheme = base.find("://");
    if (scheme != std::string::npos) {
        size_t slash = base.rfind('/');
        if (slash != std::string::npos && slash > scheme + 2)
            return base.substr(0, slash + 1) + rel;
        return base + "/" + rel;
    }
    size_t slash = base.rfind('/');
    if (slash != std::string::npos)
        return base.substr(0, slash + 1) + rel;
    return rel;
}

struct FontEnv {
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    int refs = 0;
};

FontEnv &font_env()
{
    static FontEnv env;
    return env;
}

bool font_acquire()
{
    FontEnv &e = font_env();
    if (e.refs++ > 0)
        return e.face != nullptr;
    if (FT_Init_FreeType(&e.lib))
        return false;
    if (FT_New_Memory_Face(e.lib, aroma_ubuntu_ttf, (FT_Long)aroma_ubuntu_ttf_len, 0, &e.face)) {
        FT_Done_FreeType(e.lib);
        e.lib = nullptr;
        return false;
    }
    return true;
}

void font_release()
{
    FontEnv &e = font_env();
    if (--e.refs > 0)
        return;
    if (e.face)
        FT_Done_Face(e.face);
    if (e.lib)
        FT_Done_FreeType(e.lib);
    e.face = nullptr;
    e.lib = nullptr;
}

struct LiteFont {
    int px = 12;
    int ascent = 10;
    int descent = 2;
    bool bold = false;
    bool italic = false;
    int deco = 0;
    litehtml::web_color deco_color;
    int deco_thickness = 1;
};

}

class AromaContainer : public litehtml::document_container
{
  public:
    struct Image {
        std::vector<uint8_t> px;
        int w = 0;
        int h = 0;
    };

    int view_w = 1;
    int view_h = 1;
    float text_scale = 1.0f;
    std::string base_url;
    std::string pending_nav;
    std::string title;
    std::vector<uint8_t> *buf = nullptr;
    std::vector<IRect> clip_stack;
    std::map<std::string, Image> images;
    std::map<std::string, LiteFont *> fonts;

    explicit AromaContainer(int w, int h) : view_w(w), view_h(h) {}
    ~AromaContainer() override
    {
        for (auto &kv : fonts)
            delete kv.second;
    }

    IRect clip_now(int bw, int bh) const
    {
        IRect r{0, 0, bw, bh};
        for (const IRect &c : clip_stack)
            r = intersect_rect(r, c);
        return r;
    }

    void blend_px(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                  int bw, int bh, const IRect &clip)
    {
        if (a == 0)
            return;
        if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h)
            return;
        if (x < 0 || y < 0 || x >= bw || y >= bh)
            return;
        uint8_t *p = buf->data() + ((size_t)y * (size_t)bw + (size_t)x) * 4u;
        if (a == 255) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
            return;
        }
        unsigned inv = 255 - a;
        p[0] = (uint8_t)((r * a + p[0] * inv + 127) / 255);
        p[1] = (uint8_t)((g * a + p[1] * inv + 127) / 255);
        p[2] = (uint8_t)((b * a + p[2] * inv + 127) / 255);
        p[3] = 255;
    }

    void fill_rect(int x, int y, int w, int h, const litehtml::web_color &c)
    {
        if (!buf || w <= 0 || h <= 0 || c.alpha == 0)
            return;
        int bw = view_w, bh = view_h;
        IRect clip = clip_now(bw, bh);
        int x0 = x > clip.x ? x : clip.x;
        int y0 = y > clip.y ? y : clip.y;
        int x1 = (x + w) < (clip.x + clip.w) ? (x + w) : (clip.x + clip.w);
        int y1 = (y + h) < (clip.y + clip.h) ? (y + h) : (clip.y + clip.h);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > bw) x1 = bw;
        if (y1 > bh) y1 = bh;
        for (int yy = y0; yy < y1; yy++) {
            uint8_t *row = buf->data() + ((size_t)yy * (size_t)bw + (size_t)x0) * 4u;
            for (int xx = x0; xx < x1; xx++, row += 4) {
                if (c.alpha == 255) {
                    row[0] = c.red;
                    row[1] = c.green;
                    row[2] = c.blue;
                    row[3] = 255;
                } else {
                    unsigned inv = 255 - c.alpha;
                    row[0] = (uint8_t)((c.red * c.alpha + row[0] * inv + 127) / 255);
                    row[1] = (uint8_t)((c.green * c.alpha + row[1] * inv + 127) / 255);
                    row[2] = (uint8_t)((c.blue * c.alpha + row[2] * inv + 127) / 255);
                    row[3] = 255;
                }
            }
        }
    }

    void blit_scaled(const Image &img, int dx, int dy, int dw, int dh)
    {
        if (!buf || dw <= 0 || dh <= 0 || img.px.empty())
            return;
        IRect clip = clip_now(view_w, view_h);
        for (int y = 0; y < dh; y++) {
            int sy = dy + y;
            if (sy < clip.y || sy >= clip.y + clip.h || sy < 0 || sy >= view_h)
                continue;
            float v = (dh == 1) ? 0.0f : (float)y * (float)(img.h - 1) / (float)(dh - 1);
            int y0 = (int)v;
            int y1 = y0 + 1 < img.h ? y0 + 1 : y0;
            float fy = v - (float)y0;
            for (int x = 0; x < dw; x++) {
                int sx = dx + x;
                if (sx < clip.x || sx >= clip.x + clip.w || sx < 0 || sx >= view_w)
                    continue;
                float u = (dw == 1) ? 0.0f : (float)x * (float)(img.w - 1) / (float)(dw - 1);
                int x0 = (int)u;
                int x1 = x0 + 1 < img.w ? x0 + 1 : x0;
                float fx = u - (float)x0;
                const uint8_t *p00 = img.px.data() + ((size_t)y0 * img.w + x0) * 4;
                const uint8_t *p10 = img.px.data() + ((size_t)y0 * img.w + x1) * 4;
                const uint8_t *p01 = img.px.data() + ((size_t)y1 * img.w + x0) * 4;
                const uint8_t *p11 = img.px.data() + ((size_t)y1 * img.w + x1) * 4;
                float r = (p00[0] * (1 - fx) + p10[0] * fx) * (1 - fy) +
                          (p01[0] * (1 - fx) + p11[0] * fx) * fy;
                float g = (p00[1] * (1 - fx) + p10[1] * fx) * (1 - fy) +
                          (p01[1] * (1 - fx) + p11[1] * fx) * fy;
                float b = (p00[2] * (1 - fx) + p10[2] * fx) * (1 - fy) +
                          (p01[2] * (1 - fx) + p11[2] * fx) * fy;
                float a = (p00[3] * (1 - fx) + p10[3] * fx) * (1 - fy) +
                          (p01[3] * (1 - fx) + p11[3] * fx) * fy;
                blend_px(sx, sy, (uint8_t)(r + 0.5f), (uint8_t)(g + 0.5f),
                         (uint8_t)(b + 0.5f), (uint8_t)(a + 0.5f), view_w, view_h, clip);
            }
        }
    }

    void draw_disc(int cx, int cy, int r, const litehtml::web_color &c, bool fill)
    {
        if (!buf || r <= 0)
            return;
        IRect clip = clip_now(view_w, view_h);
        for (int y = cy - r; y <= cy + r; y++) {
            for (int x = cx - r; x <= cx + r; x++) {
                int ddx = x - cx, ddy = y - cy;
                float d = sqrtf((float)(ddx * ddx + ddy * ddy));
                if (fill) {
                    if (d <= (float)r + 0.5f)
                        blend_px(x, y, c.red, c.green, c.blue, c.alpha, view_w, view_h, clip);
                } else {
                    if (d >= (float)r - 0.75f && d <= (float)r + 0.5f)
                        blend_px(x, y, c.red, c.green, c.blue, c.alpha, view_w, view_h, clip);
                }
            }
        }
    }

    std::string absolute_url(const char *src, const char *baseurl)
    {
        std::string s = src ? src : "";
        std::string b;
        if (baseurl && baseurl[0])
            b = baseurl;
        else if (!base_url_.empty())
            b = base_url_;
        else
            b = base_url;
        if (s.empty())
            return b;
        if (s.compare(0, 7, "http://") == 0 || s.compare(0, 8, "https://") == 0 ||
            s.compare(0, 7, "file://") == 0 || s.compare(0, 5, "data:") == 0)
            return s;
        return join_url_path(b, s);
    }

    const Image *ensure_image(const std::string &abs);

    litehtml::uint_ptr create_font(const litehtml::font_description &descr,
                                   const litehtml::document *doc,
                                   litehtml::font_metrics *fm) override
    {
        (void)doc;
        int size = px(descr.size);
        if (size < 4)
            size = 4;
        if (size > 256)
            size = 256;
        LiteFont *f = new (std::nothrow) LiteFont();
        if (!f)
            return 0;
        f->px = size;
        f->bold = descr.weight > 500;
        f->italic = (descr.style == litehtml::font_style_italic);
        f->deco = descr.decoration_line;
        f->deco_color = descr.decoration_color;
        f->deco_thickness = size / 14 + 1;
        FT_Face face = font_env().face;
        if (face) {
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size);
            f->ascent = (int)(face->size->metrics.ascender >> 6);
            f->descent = (int)-(face->size->metrics.descender >> 6);
            if (f->descent < 0)
                f->descent = -f->descent;
            if (f->ascent <= 0)
                f->ascent = size;
        } else {
            f->ascent = (size * 4) / 5;
            f->descent = size / 5;
        }
        if (fm) {
            fm->ascent = f->ascent;
            fm->descent = f->descent;
            fm->height = f->ascent + f->descent;
            fm->x_height = (size * 5) / 10;
            fm->font_size = size;
        }
        return (litehtml::uint_ptr)f;
    }

    void delete_font(litehtml::uint_ptr hFont) override
    {
        delete (LiteFont *)hFont;
    }

    static uint32_t adv_of(FT_Face face, uint32_t cp, uint32_t prev, bool kern)
    {
        FT_UInt idx = FT_Get_Char_Index(face, cp);
        if (idx == 0)
            idx = FT_Get_Char_Index(face, '?');
        FT_Pos dx = 0;
        if (kern && prev != 0 && idx != 0) {
            FT_Vector k = {0, 0};
            FT_UInt pidx = FT_Get_Char_Index(face, prev);
            if (pidx != 0 && !FT_Get_Kerning(face, pidx, idx, FT_KERNING_DEFAULT, &k))
                dx = k.x >> 6;
        }
        FT_Pos adv = 0;
        if (idx != 0 && !FT_Load_Glyph(face, idx, FT_LOAD_DEFAULT))
            adv = face->glyph->advance.x >> 6;
        if (adv <= 0)
            adv = 0;
        return (uint32_t)(dx + adv);
    }

    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr hFont) override
    {
        LiteFont *f = (LiteFont *)hFont;
        if (!text || !f)
            return 0;
        FT_Face face = font_env().face;
        const char *p = text;
        const char *end = text + strlen(text);
        if (!face) {
            int n = 0;
            while (p < end) {
                uint32_t cp = utf8_next(p, end);
                (void)cp;
                n++;
            }
            return (n * f->px * 6) / 10;
        }
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px);
        bool kern = FT_HAS_KERNING(face) != 0;
        uint32_t prev = 0;
        int w = 0;
        while (p < end) {
            uint32_t cp = utf8_next(p, end);
            w += (int)adv_of(face, cp, prev, kern);
            prev = cp;
        }
        return w;
    }

    void blit_glyph(FT_Bitmap *bmp, int gx, int gy, const litehtml::web_color &c,
                    int shear_q, int shear_n)
    {
        if (!buf || !bmp || !bmp->buffer)
            return;
        IRect clip = clip_now(view_w, view_h);
        unsigned rows = bmp->rows;
        int pitch = bmp->pitch;
        for (unsigned row = 0; row < rows; row++) {
            int shear = shear_n ? (int)(((int)rows - 1 - (int)row) * shear_q / shear_n) : 0;
            for (unsigned col = 0; col < bmp->width; col++) {
                uint8_t cov = bmp->buffer[row * pitch + col];
                if (!cov)
                    continue;
                unsigned a = (unsigned)c.alpha * cov / 255;
                blend_px(gx + (int)col + shear, gy + (int)row,
                         c.red, c.green, c.blue, (uint8_t)a, view_w, view_h, clip);
            }
        }
    }

    void draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr hFont,
                   litehtml::web_color color, const litehtml::position &pos) override
    {
        (void)hdc;
        LiteFont *f = (LiteFont *)hFont;
        if (!text || !f || !buf)
            return;
        int baseline = px(pos.y) + px(pos.height) - f->descent;
        FT_Face face = font_env().face;
        if (!face) {
            fill_rect(px(pos.x), baseline - f->ascent, px(text_width(text, hFont)),
                      f->ascent + f->descent, color);
            return;
        }
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)f->px);
        bool kern = FT_HAS_KERNING(face) != 0;
        const char *p = text;
        const char *end = text + strlen(text);
        int pen = px(pos.x);
        uint32_t prev = 0;
        int shear_q = f->italic ? 22 : 0;
        while (p < end) {
            uint32_t cp = utf8_next(p, end);
            FT_UInt idx = FT_Get_Char_Index(face, cp);
            if (idx == 0)
                idx = FT_Get_Char_Index(face, '?');
            if (kern && prev != 0 && idx != 0) {
                FT_Vector k = {0, 0};
                FT_UInt pidx = FT_Get_Char_Index(face, prev);
                if (pidx != 0 && !FT_Get_Kerning(face, pidx, idx, FT_KERNING_DEFAULT, &k))
                    pen += (int)(k.x >> 6);
            }
            if (idx != 0 && !FT_Load_Glyph(face, idx, FT_LOAD_DEFAULT) &&
                !FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
                FT_GlyphSlot g = face->glyph;
                int gx = pen + g->bitmap_left;
                int gy = baseline - g->bitmap_top;
                blit_glyph(&g->bitmap, gx, gy, color, shear_q, 100);
                if (f->bold)
                    blit_glyph(&g->bitmap, gx + 1, gy, color, shear_q, 100);
                pen += (int)(g->advance.x >> 6);
            }
            prev = cp;
        }
        if (f->deco) {
            litehtml::web_color dc = (f->deco_color.alpha) ? f->deco_color : color;
            int w = pen - px(pos.x);
            if (f->deco & litehtml::text_decoration_line_underline)
                fill_rect(px(pos.x), baseline + f->descent / 3, w, f->deco_thickness, dc);
            if (f->deco & litehtml::text_decoration_line_line_through)
                fill_rect(px(pos.x), baseline - f->ascent / 3, w, f->deco_thickness, dc);
            if (f->deco & litehtml::text_decoration_line_overline)
                fill_rect(px(pos.x), baseline - f->ascent, w, f->deco_thickness, dc);
        }
    }

    litehtml::pixel_t pt_to_px(float pt) const override
    {
        return (int)(pt * 96.0f / 72.0f * text_scale + 0.5f);
    }

    litehtml::pixel_t get_default_font_size() const override
    {
        return (int)(16.0f * text_scale + 0.5f);
    }

    const char *get_default_font_name() const override { return "sans-serif"; }

    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override
    {
        (void)hdc;
        int cx = px(marker.pos.x) + px(marker.pos.width) / 2;
        int cy = px(marker.pos.y) + px(marker.pos.height) / 2;
        int r = px(marker.pos.width) / 3;
        if (r < 2)
            r = 2;
        switch (marker.marker_type) {
        case litehtml::list_style_type_disc:
            draw_disc(cx, cy, r, marker.color, true);
            break;
        case litehtml::list_style_type_circle:
            draw_disc(cx, cy, r, marker.color, false);
            break;
        case litehtml::list_style_type_square:
            fill_rect(cx - r, cy - r, r * 2 + 1, r * 2 + 1, marker.color);
            break;
        default: {
            char num[32];
            if (marker.marker_type == litehtml::list_style_type_lower_alpha ||
                marker.marker_type == litehtml::list_style_type_lower_latin) {
                int n = marker.index <= 0 ? 1 : marker.index;
                std::string s;
                while (n > 0) {
                    n--;
                    s.insert(s.begin(), (char)('a' + (n % 26)));
                    n /= 26;
                }
                s += ".";
                snprintf(num, sizeof(num), "%s", s.c_str());
            } else {
                snprintf(num, sizeof(num), "%d.", marker.index <= 0 ? 1 : marker.index);
            }
            litehtml::position p = marker.pos;
            if (marker.font) {
                draw_text(hdc, num, marker.font, marker.color, p);
            } else {
                fill_rect(px(p.x), cy - 1, px(p.width), 2, marker.color);
            }
            break;
        }
        }
    }

    void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override
    {
        (void)redraw_on_ready;
        if (!src || !src[0])
            return;
        ensure_image(absolute_url(src, baseurl));
    }

    void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override
    {
        sz.width = 0;
        sz.height = 0;
        if (!src || !src[0])
            return;
        const Image *img = ensure_image(absolute_url(src, baseurl));
        if (img && !img->px.empty()) {
            sz.width = img->w;
            sz.height = img->h;
        }
    }

    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                    const std::string &url, const std::string &base_url) override
    {
        (void)hdc;
        const Image *img = ensure_image(absolute_url(url.c_str(), base_url.c_str()));
        if (!img || img->px.empty())
            return;
        int ox = px(layer.origin_box.x);
        int oy = px(layer.origin_box.y);
        int ow = px(layer.origin_box.width);
        int oh = px(layer.origin_box.height);
        if (ow <= 0 || oh <= 0) {
            ow = img->w;
            oh = img->h;
        }
        if (layer.repeat == litehtml::background_repeat_no_repeat) {
            blit_scaled(*img, ox, oy, ow, oh);
            return;
        }
        int cx = px(layer.clip_box.x);
        int cy = px(layer.clip_box.y);
        int cw = px(layer.clip_box.width);
        int ch = px(layer.clip_box.height);
        if (cw <= 0 || ch <= 0 || ow <= 0 || oh <= 0)
            return;
        for (int y = oy; y < cy + ch; y += oh) {
            for (int x = ox; x < cx + cw; x += ow) {
                if (y + oh <= cy || x + ow <= cx)
                    continue;
                blit_scaled(*img, x, y, ow, oh);
            }
        }
    }

    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override
    {
        (void)hdc;
        fill_rect(px(layer.clip_box.x), px(layer.clip_box.y),
                  px(layer.clip_box.width), px(layer.clip_box.height), color);
    }

    void draw_linear_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer &layer,
                              const litehtml::background_layer::linear_gradient &grad) override
    {
        (void)hdc;
        int x = px(layer.clip_box.x), y = px(layer.clip_box.y);
        int w = px(layer.clip_box.width), h = px(layer.clip_box.height);
        if (w <= 0 || h <= 0 || grad.color_points.empty()) {
            if (!grad.color_points.empty())
                draw_solid_fill(hdc, layer, grad.color_points.front().color);
            return;
        }
        float sx = grad.start.x, sy = grad.start.y;
        float ex = grad.end.x, ey = grad.end.y;
        float dx = ex - sx, dy = ey - sy;
        float len2 = dx * dx + dy * dy;
        for (int yy = 0; yy < h; yy++) {
            float t;
            if (len2 < 1e-6f) {
                t = (float)yy / (float)(h > 1 ? h - 1 : 1);
            } else {
                float rx = (float)x - sx, ry = (float)(y + yy) - sy;
                t = (rx * dx + ry * dy) / len2;
            }
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            const auto &pts = grad.color_points;
            litehtml::web_color c0 = pts.front().color, c1 = pts.back().color;
            float t0 = 0.0f, t1 = 1.0f;
            for (size_t i = 0; i < pts.size(); i++) {
                if (pts[i].offset <= t) {
                    c0 = pts[i].color;
                    t0 = pts[i].offset;
                }
                if (pts[i].offset >= t) {
                    c1 = pts[i].color;
                    t1 = pts[i].offset;
                    break;
                }
            }
            float f = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
            litehtml::web_color c;
            c.red = (uint8_t)(c0.red + (c1.red - c0.red) * f + 0.5f);
            c.green = (uint8_t)(c0.green + (c1.green - c0.green) * f + 0.5f);
            c.blue = (uint8_t)(c0.blue + (c1.blue - c0.blue) * f + 0.5f);
            c.alpha = (uint8_t)(c0.alpha + (c1.alpha - c0.alpha) * f + 0.5f);
            fill_rect(x, y + yy, w, 1, c);
        }
    }

    void draw_radial_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer &layer,
                              const litehtml::background_layer::radial_gradient &grad) override
    {
        (void)hdc;
        (void)grad;
        auto *g = (const litehtml::background_layer::gradient_base *)&grad;
        if (!g->color_points.empty())
            draw_solid_fill(hdc, layer, g->color_points.front().color);
    }

    void draw_conic_gradient(litehtml::uint_ptr hdc,
                             const litehtml::background_layer &layer,
                             const litehtml::background_layer::conic_gradient &grad) override
    {
        (void)hdc;
        (void)grad;
        auto *g = (const litehtml::background_layer::gradient_base *)&grad;
        if (!g->color_points.empty())
            draw_solid_fill(hdc, layer, g->color_points.front().color);
    }

    static bool border_visible(const litehtml::border &b)
    {
        return px(b.width) > 0 && b.style != litehtml::border_style_none &&
               b.style != litehtml::border_style_hidden;
    }

    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                      const litehtml::position &draw_pos, bool root) override
    {
        (void)hdc;
        (void)root;
        int x = px(draw_pos.x), y = px(draw_pos.y);
        int w = px(draw_pos.width), h = px(draw_pos.height);
        int tw = px(borders.top.width), bw = px(borders.bottom.width);
        int lw = px(borders.left.width), rw = px(borders.right.width);
        if (border_visible(borders.top))
            fill_rect(x, y, w, tw, borders.top.color);
        if (border_visible(borders.bottom))
            fill_rect(x, y + h - bw, w, bw, borders.bottom.color);
        if (border_visible(borders.left))
            fill_rect(x, y + tw, lw, h - tw - bw, borders.left.color);
        if (border_visible(borders.right))
            fill_rect(x + w - rw, y + tw, rw, h - tw - bw, borders.right.color);
    }

    void set_caption(const char *caption) override
    {
        title = caption ? caption : "";
    }

    void set_base_url(const char *base_url) override
    {
        if (base_url)
            base_url_ = base_url;
    }

    void link(const std::shared_ptr<litehtml::document> &doc,
              const litehtml::element::ptr &el) override
    {
        (void)doc;
        if (!el)
            return;
        const char *src = el->get_attr("src");
        if (src && src[0])
            load_image(src, base_url_.c_str(), false);
    }

    void on_anchor_click(const char *url, const litehtml::element::ptr &el) override;

    void on_mouse_event(const litehtml::element::ptr &el, litehtml::mouse_event event) override
    {
        (void)el;
        (void)event;
    }

    void set_cursor(const char *cursor) override { (void)cursor; }

    void transform_text(std::string &text, litehtml::text_transform tt) override
    {
        if (tt == litehtml::text_transform_none)
            return;
        bool cap_next = (tt == litehtml::text_transform_capitalize);
        for (size_t i = 0; i < text.size(); i++) {
            unsigned char c = (unsigned char)text[i];
            if (c >= 0x80)
                continue;
            if (tt == litehtml::text_transform_uppercase)
                text[i] = (char)toupper(c);
            else if (tt == litehtml::text_transform_lowercase)
                text[i] = (char)tolower(c);
            else if (tt == litehtml::text_transform_capitalize) {
                if (cap_next && isalpha(c)) {
                    text[i] = (char)toupper(c);
                    cap_next = false;
                } else if (isspace(c)) {
                    cap_next = true;
                }
            }
        }
    }

    void import_css(std::string &text, const std::string &url,
                    std::string &baseurl) override;

    void set_clip(const litehtml::position &pos,
                  const litehtml::border_radiuses &bdr_radius) override
    {
        (void)bdr_radius;
        clip_stack.push_back({px(pos.x), px(pos.y), px(pos.width), px(pos.height)});
    }

    void del_clip() override
    {
        if (!clip_stack.empty())
            clip_stack.pop_back();
    }

    void get_viewport(litehtml::position &viewport) const override
    {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = view_w;
        viewport.height = view_h;
    }

    litehtml::element::ptr create_element(const char *tag_name,
                                          const litehtml::string_map &attributes,
                                          const std::shared_ptr<litehtml::document> &doc) override;

    void get_media_features(litehtml::media_features &media) const override
    {
        media.type = litehtml::media_type_screen;
        media.width = view_w;
        media.height = view_h;
        media.device_width = view_w;
        media.device_height = view_h;
        media.color = 8;
        media.monochrome = 0;
        media.color_index = 0;
        media.resolution = 96;
    }

    void get_language(std::string &language, std::string &culture) const override
    {
        language = "en";
        culture = "";
    }

    std::vector<litehtml::element::ptr> controls;
    AromaControl *focused = nullptr;
    AromaWebPage *owner = nullptr;

    void draw_focus_ring(int x, int y, int w, int h, const litehtml::web_color &c)
    {
        fill_rect(x, y, w, 1, c);
        fill_rect(x, y + h - 1, w, 1, c);
        fill_rect(x, y, 1, h, c);
        fill_rect(x + w - 1, y, 1, h, c);
    }

    void forget_controls()
    {
        controls.clear();
        focused = nullptr;
    }

  private:
    std::string base_url_;
};

namespace {

void collect_text(const litehtml::element::ptr &el, std::string &out)
{
    if (!el)
        return;
    if (el->is_text()) {
        el->get_text(out);
        return;
    }
    for (const auto &ch : el->children())
        collect_text(ch, out);
}

std::string element_text(const litehtml::element::ptr &el)
{
    std::string out;
    for (const auto &ch : el->children())
        collect_text(ch, out);
    return out;
}

std::string html_escape(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;";
            break;
        default: out += c; break;
        }
    }
    return out;
}

std::string url_encode(const std::string &in)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else if (c == ' ')
            out += '+';
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static void clear_children(const litehtml::element::ptr &el)
{
    if (!el)
        return;
    std::vector<litehtml::element::ptr> kids;
    for (const auto &ch : el->children())
        kids.push_back(ch);
    for (const auto &ch : kids)
        el->removeChild(ch);
}

void serialize_node(const litehtml::element::ptr &el, std::string &out);

bool is_void_tag(const char *tag)
{
    static const char *const voids[] = {"area", "base", "br", "col", "embed",
                                        "hr", "img", "input", "link", "meta",
                                        "source", "track", "wbr", nullptr};
    for (int i = 0; voids[i]; i++)
        if (!strcasecmp(tag, voids[i]))
            return true;
    return false;
}

void serialize_node(const litehtml::element::ptr &el, std::string &out)
{
    if (!el)
        return;
    if (el->is_text()) {
        std::string t;
        el->get_text(t);
        out += html_escape(t);
        return;
    }
    const char *tag = el->get_tagName();
    if (!tag || !tag[0])
        return;
    out += '<';
    out += tag;
    for (const auto &kv : el->dump_get_attrs()) {
        out += ' ';
        out += std::get<0>(kv);
        out += "=\"";
        out += html_escape(std::get<1>(kv));
        out += '"';
    }
    if (is_void_tag(tag)) {
        out += '>';
        return;
    }
    out += '>';
    for (const auto &ch : el->children())
        serialize_node(ch, out);
    out += "</";
    out += tag;
    out += '>';
}

std::string serialize_children(const litehtml::element::ptr &el)
{
    std::string out;
    if (!el)
        return out;
    for (const auto &ch : el->children())
        serialize_node(ch, out);
    return out;
}

struct SelCompound {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attrs;
    bool has_value = false;
};

bool match_compound(const litehtml::element::ptr &el, const SelCompound &s)
{
    if (!el || el->is_text())
        return false;
    if (!s.tag.empty() && s.tag != "*" && strcasecmp(el->get_tagName(), s.tag.c_str()))
        return false;
    if (!s.id.empty()) {
        const char *v = el->get_attr("id");
        if (!v || s.id != v)
            return false;
    }
    for (const auto &c : s.classes) {
        const char *v = el->get_attr("class");
        bool found = false;
        if (v) {
            std::string cls = v;
            size_t p = 0;
            while (p < cls.size()) {
                while (p < cls.size() && isspace((unsigned char)cls[p]))
                    p++;
                size_t q = p;
                while (q < cls.size() && !isspace((unsigned char)cls[q]))
                    q++;
                if (q > p && cls.compare(p, q - p, c) == 0) {
                    found = true;
                    break;
                }
                p = q;
            }
        }
        if (!found)
            return false;
    }
    for (const auto &a : s.attrs) {
        const char *v = el->get_attr(a.first.c_str());
        if (!v)
            return false;
        if (a.second.size() && a.second != v)
            return false;
    }
    return true;
}

bool parse_compound(const std::string &tok, SelCompound &out)
{
    out = SelCompound();
    size_t i = 0;

    size_t t = i;
    while (i < tok.size() && tok[i] != '#' && tok[i] != '.' && tok[i] != '[')
        i++;
    if (i > t)
        out.tag = tok.substr(t, i - t);
    while (i < tok.size()) {
        if (tok[i] == '#') {
            size_t j = ++i;
            while (i < tok.size() && tok[i] != '.' && tok[i] != '[')
                i++;
            out.id = tok.substr(j, i - j);
        } else if (tok[i] == '.') {
            size_t j = ++i;
            while (i < tok.size() && tok[i] != '.' && tok[i] != '#' && tok[i] != '[')
                i++;
            if (i > j)
                out.classes.push_back(tok.substr(j, i - j));
        } else if (tok[i] == '[') {
            size_t j = ++i;
            while (i < tok.size() && tok[i] != ']')
                i++;
            std::string inner = tok.substr(j, i - j);
            if (i < tok.size())
                i++;
            size_t eq = inner.find('=');
            if (eq == std::string::npos) {
                out.attrs.emplace_back(inner, "");
            } else {
                std::string v = inner.substr(eq + 1);
                if (v.size() >= 2 &&
                    ((v.front() == '"' && v.back() == '"') ||
                     (v.front() == '\'' && v.back() == '\'')))
                    v = v.substr(1, v.size() - 2);
                out.attrs.emplace_back(inner.substr(0, eq), v);
            }
        } else {
            i++;
        }
    }
    return !(out.tag.empty() && out.id.empty() && out.classes.empty() && out.attrs.empty());
}

struct SelStep {
    SelCompound compound;
    bool child = false;
};

bool parse_selector(const std::string &sel, std::vector<SelStep> &steps)
{
    steps.clear();
    size_t i = 0;
    bool child = false;
    while (i < sel.size()) {
        while (i < sel.size() && isspace((unsigned char)sel[i]))
            i++;
        if (i < sel.size() && sel[i] == '>') {
            child = true;
            i++;
            continue;
        }
        size_t j = i;
        while (j < sel.size() && !isspace((unsigned char)sel[j]) && sel[j] != '>')
            j++;
        if (j > i) {
            SelStep st;
            st.child = child;
            child = false;
            if (!parse_compound(sel.substr(i, j - i), st.compound))
                return false;
            steps.push_back(std::move(st));
        }
        i = j;
    }
    return !steps.empty();
}

bool match_ancestors(const litehtml::element::ptr &el, const std::vector<SelStep> &steps, int idx)
{

    auto cur = el->parent();
    for (int s = idx - 1; s >= 0; s--) {
        if (steps[s + 1].child) {
            if (!cur || !match_compound(cur, steps[s].compound))
                return false;
            cur = cur->parent();
        } else {
            while (cur && !match_compound(cur, steps[s].compound))
                cur = cur->parent();
            if (!cur)
                return false;
            cur = cur->parent();
        }
    }
    return true;
}

void query_walk(const litehtml::element::ptr &el, const std::vector<SelStep> &steps,
                std::vector<litehtml::element::ptr> &out, bool first_only)
{
    if (!el)
        return;
    for (const auto &ch : el->children()) {
        if (!ch->is_text() && match_compound(ch, steps.back().compound) &&
            match_ancestors(ch, steps, (int)steps.size() - 1)) {
            out.push_back(ch);
            if (first_only)
                return;
        }
        query_walk(ch, steps, out, first_only);
        if (first_only && !out.empty())
            return;
    }
}

}

class AromaScript : public litehtml::html_tag
{
  public:
    explicit AromaScript(const litehtml::document::ptr &doc) : litehtml::html_tag(doc)
    {
        m_css.set_display(litehtml::display_none);
    }
};

class AromaControl : public litehtml::html_tag
{
  public:
    enum Kind { Text, Password, Checkbox, Radio, Submit, Button, Hidden, File };
    Kind kind = Text;
    std::string ctl_value;
    std::string ctl_name;
    std::string ctl_type = "text";
    bool checked = false;
    bool disabled = false;
    bool readonly = false;
    int size_attr = 20;
    int maxlen = -1;
    int rows_attr = 2;
    int cols_attr = 20;
    bool dirty = false;
    int cursor = 0;
    bool value_init = false;
    void ensure_value()
    {
        if (value_init)
            return;
        value_init = true;
        if (is_textarea && ctl_value.empty()) {
            ctl_value = element_text(shared_from_this());

            while (!ctl_value.empty() &&
                   (ctl_value.back() == '\n' || ctl_value.back() == '\r'))
                ctl_value.pop_back();
            cursor = (int)ctl_value.size();
        }
    }
    bool is_textarea = false;
    bool is_select = false;
    struct Option {
        std::string value;
        std::string label;
        bool selected = false;
    };
    std::vector<Option> options;
    int sel_index = -1;

    AromaControl(const litehtml::document::ptr &doc, const char *tag, bool textarea, bool select)
        : litehtml::html_tag(doc), is_textarea(textarea), is_select(select)
    {
        (void)tag;
        m_css.set_display(litehtml::display_inline_block);
    }

    static const char *canon_type(const char *t)
    {
        if (!t || !t[0])
            return "text";
        return t;
    }

    void parse_attributes() override
    {
        const char *tag = get_tagName();
        bool is_button_tag = tag && !strcasecmp(tag, "button");
        const char *t = get_attr("type", is_textarea ? "textarea"
                                 : (is_select ? "select-one" : (is_button_tag ? "submit" : "text")));
        std::string lt = t ? t : "text";
        for (auto &c : lt)
            c = (char)tolower((unsigned char)c);
        ctl_type = lt;
        if (is_textarea)
            kind = Text;
        else if (is_select)
            kind = Button;
        else if (lt == "password")
            kind = Password;
        else if (lt == "checkbox")
            kind = Checkbox;
        else if (lt == "radio")
            kind = Radio;
        else if (lt == "submit")
            kind = Submit;
        else if (lt == "button" || lt == "reset" || lt == "image")
            kind = Button;
        else if (lt == "hidden")
            kind = Hidden;
        else if (lt == "file")
            kind = File;
        else
            kind = Text;
        const char *n = get_attr("name");
        ctl_name = n ? n : "";
        const char *v = get_attr("value");
        ctl_value = v ? v : "";
        checked = get_attr("checked") != nullptr;
        disabled = get_attr("disabled") != nullptr;
        readonly = get_attr("readonly") != nullptr;
        const char *sz = get_attr("size");
        if (sz && sz[0])
            size_attr = atoi(sz);
        if (size_attr < 1)
            size_attr = 1;
        if (size_attr > 80)
            size_attr = 80;
        const char *ml = get_attr("maxlength");
        maxlen = (ml && ml[0]) ? atoi(ml) : -1;
        const char *r = get_attr("rows");
        if (r && r[0])
            rows_attr = atoi(r);
        if (rows_attr < 1)
            rows_attr = 1;
        const char *co = get_attr("cols");
        if (co && co[0])
            cols_attr = atoi(co);
        if (cols_attr < 1)
            cols_attr = 1;
        cursor = (int)ctl_value.size();
        if (kind == Hidden)
            m_css.set_display(litehtml::display_none);
        if (is_select)
            collect_options();
    }

    void collect_options()
    {
        options.clear();
        sel_index = -1;
        collect_options_in(shared_from_this());
        if (sel_index < 0 && !options.empty()) {
            options[0].selected = true;
            sel_index = 0;
        }
    }

    void collect_options_in(const litehtml::element::ptr &el)
    {
        for (const auto &ch : el->children()) {
            if (ch->is_text())
                continue;
            const char *tag = ch->get_tagName();
            if (tag && !strcasecmp(tag, "option")) {
                Option o;
                const char *vv = ch->get_attr("value");
                std::string label = element_text(ch);
                o.value = vv ? vv : label;
                o.label = label;
                o.selected = ch->get_attr("selected") != nullptr;
                if (o.selected)
                    sel_index = (int)options.size();
                options.push_back(std::move(o));
            } else {
                collect_options_in(ch);
            }
        }
    }

    bool is_replaced() const override { return true; }

    std::shared_ptr<litehtml::render_item> create_render_item(
        const std::shared_ptr<litehtml::render_item> &parent_ri) override
    {
        auto ret = std::make_shared<litehtml::render_item_image>(shared_from_this());
        ret->parent(parent_ri);
        return ret;
    }

    litehtml::uint_ptr ui_font()
    {
        auto doc = get_document();
        litehtml::font_description descr;
        descr.size = doc->container()->get_default_font_size();
        descr.weight = 400;
        litehtml::font_metrics fm;
        return doc->get_font(descr, &fm);
    }

    int font_px()
    {
        auto doc = get_document();
        return (int)doc->container()->get_default_font_size();
    }

    void get_content_size(litehtml::size &sz, litehtml::pixel_t max_width) override
    {
        (void)max_width;
        int fs = font_px();
        if (kind == Hidden) {
            sz.width = 0;
            sz.height = 0;
            return;
        }
        if (is_select) {
            int w = 40;
            auto doc = get_document();
            for (const auto &o : options) {
                litehtml::uint_ptr f = ui_font();
                int tw = (int)doc->container()->text_width(o.label.c_str(), f);
                if (tw > w)
                    w = tw;
            }
            sz.width = w + fs + 14;
            sz.height = fs + 12;
            return;
        }
        if (is_textarea) {
            auto doc = get_document();
            litehtml::uint_ptr f = ui_font();
            std::string zeros(cols_attr > 0 ? (size_t)cols_attr : 20, '0');
            sz.width = (int)doc->container()->text_width(zeros.c_str(), f) + 12;
            sz.height = (fs + 6) * rows_attr + 8;
            return;
        }
        if (kind == Checkbox || kind == Radio) {
            sz.width = fs;
            sz.height = fs;
            if (px(sz.width) < 12)
                sz.width = 12;
            if (px(sz.height) < 12)
                sz.height = 12;
            return;
        }
        if (kind == Submit || kind == Button) {
            std::string label = button_label();
            auto doc = get_document();
            litehtml::uint_ptr f = ui_font();
            sz.width = (int)doc->container()->text_width(label.c_str(), f) + 24;
            sz.height = fs + 12;
            return;
        }
        auto doc = get_document();
        litehtml::uint_ptr f = ui_font();
        std::string zeros(size_attr > 0 ? (size_t)size_attr : 20, '0');
        sz.width = (int)doc->container()->text_width(zeros.c_str(), f) + 12;
        sz.height = fs + 12;
    }

    std::string button_label()
    {
        const char *v = get_attr("value");
        if (v && v[0])
            return v;
        std::string t = element_text(shared_from_this());
        if (!t.empty())
            return t;
        if (kind == Submit)
            return "Submit";
        return "Button";
    }

    litehtml::position my_box(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
                              const std::shared_ptr<litehtml::render_item> &ri)
    {
        (void)hdc;
        litehtml::position pos = ri->pos();
        pos.x += x;
        pos.y += y;
        pos.round();
        return pos;
    }

    void draw(litehtml::uint_ptr hdc, litehtml::pixel_t x, litehtml::pixel_t y,
              const litehtml::position *clip,
              const std::shared_ptr<litehtml::render_item> &ri) override
    {
        if (kind == Hidden)
            return;
        html_tag::draw(hdc, x, y, clip, ri);
        auto *cont = get_document()->container();
        auto *ac = static_cast<AromaContainer *>(cont);
        litehtml::position pos = my_box(hdc, x, y, ri);
        int bx = (int)pos.x, by = (int)pos.y;
        int bw = (int)pos.width, bh = (int)pos.height;
        litehtml::web_color white{255, 255, 255, 255};
        litehtml::web_color face{240, 240, 240, 255};
        litehtml::web_color ink{20, 20, 20, 255};
        litehtml::web_color gray{136, 136, 136, 255};
        litehtml::web_color focus{40, 120, 220, 255};
        if (kind == Checkbox || kind == Radio) {
            int s = bw < bh ? bw : bh;
            if (s > 16)
                s = 16;
            int ox = bx + (bw - s) / 2, oy = by + (bh - s) / 2;
            ac->fill_rect(ox, oy, s, s, white);
            litehtml::borders b;
            b.top.width = b.bottom.width = b.left.width = b.right.width = 1;
            b.top.color = b.bottom.color = b.left.color = b.right.color = gray;
            b.top.style = b.bottom.style = b.left.style = b.right.style =
                litehtml::border_style_solid;
            litehtml::position bp{ox, oy, s, s};
            ac->draw_borders(hdc, b, bp, false);
            if (checked) {
                if (kind == Checkbox) {
                    int m = s / 4;
                    ac->fill_rect(ox + m, oy + m, s - 2 * m, s - 2 * m, ink);
                } else {
                    ac->draw_disc(ox + s / 2, oy + s / 2, s / 2 - 2, ink, true);
                }
            }
            if (ac->focused == this)
                ac->draw_focus_ring(bx - 2, by - 2, bw + 4, bh + 4, focus);
            return;
        }
        if (kind == Submit || kind == Button) {
            ac->fill_rect(bx, by, bw, bh, disabled ? gray : face);
            litehtml::borders b;
            b.top.width = b.bottom.width = b.left.width = b.right.width = 1;
            litehtml::web_color bc = disabled ? gray : ink;
            b.top.color = b.bottom.color = b.left.color = b.right.color = bc;
            b.top.style = b.bottom.style = b.left.style = b.right.style =
                litehtml::border_style_solid;
            litehtml::position bp{bx, by, bw, bh};
            ac->draw_borders(hdc, b, bp, false);
            std::string label = button_label();
            litehtml::uint_ptr f = ui_font();
            int tw = (int)cont->text_width(label.c_str(), f);
            int fs = font_px();
            litehtml::position tp{bx + (bw - tw) / 2, by + (bh - fs) / 2, tw, fs + 4};
            ac->draw_text(hdc, label.c_str(), f, disabled ? white : ink, tp);
            return;
        }

        ac->fill_rect(bx, by, bw, bh, disabled ? face : white);
        litehtml::borders b;
        b.top.width = b.bottom.width = b.left.width = b.right.width = 1;
        b.top.color = b.bottom.color = b.left.color = b.right.color = gray;
        b.top.style = b.bottom.style = b.left.style = b.right.style =
            litehtml::border_style_solid;
        litehtml::position bp{bx, by, bw, bh};
        ac->draw_borders(hdc, b, bp, false);
        if (ac->focused == this)
            ac->draw_focus_ring(bx - 2, by - 2, bw + 4, bh + 4, focus);
        int fs = font_px();
        int ty = by + (bh - fs) / 2;
        litehtml::uint_ptr f = ui_font();
        if (is_select) {
            std::string cur = (sel_index >= 0 && sel_index < (int)options.size())
                                  ? options[sel_index].label
                                  : "";
            litehtml::position tp{bx + 5, ty, bw - 20, fs + 4};
            ac->draw_text(hdc, cur.c_str(), f, ink, tp);

            int ax = bx + bw - 12, ay = by + bh / 2;
            for (int r = 0; r < 4; r++)
                ac->fill_rect(ax - r, ay - 3 + r, r * 2 + 1, 1, ink);
            return;
        }
        if (is_textarea) {
            ensure_value();
            std::string v = ctl_value;
            size_t p = 0;
            int ly = by + 4;
            while (p <= v.size() && ly + fs <= by + bh) {
                size_t nl = v.find('\n', p);
                std::string line = v.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                litehtml::position tp{bx + 5, ly, bw - 10, fs + 2};
                ac->draw_text(hdc, line.c_str(), f, ink, tp);
                ly += fs + 2;
                if (nl == std::string::npos)
                    break;
                p = nl + 1;
            }
            return;
        }
        std::string shown = (kind == Password) ? std::string(ctl_value.size(), '*') : ctl_value;
        litehtml::position tp{bx + 5, ty, bw - 10, fs + 4};
        ac->draw_text(hdc, shown.c_str(), f, ink, tp);
        if (ac->focused == this) {
            int cx = bx + 5 + (int)cont->text_width(shown.substr(0, (size_t)cursor).c_str(), f);
            ac->fill_rect(cx, ty, 1, fs + 2, ink);
        }
    }

    void on_click() override;

    void activate(AromaWebPage *page);

    std::string submit_value()
    {
        ensure_value();
        if (kind == Checkbox || kind == Radio) {
            if (!checked)
                return "";
            const char *v = get_attr("value");
            return v && v[0] ? v : "on";
        }
        if (is_select) {
            if (sel_index >= 0 && sel_index < (int)options.size())
                return options[sel_index].value;
            return "";
        }
        if (is_textarea)
            return ctl_value;
        return ctl_value;
    }
};

class AromaFormHelper
{
  public:
    static litehtml::element::ptr find_form(const litehtml::element::ptr &el)
    {
        auto cur = el->parent();
        while (cur) {
            const char *tag = cur->get_tagName();
            if (tag && !strcasecmp(tag, "form"))
                return cur;
            cur = cur->parent();
        }
        return nullptr;
    }

    static void collect_controls(const litehtml::element::ptr &el,
                                 std::vector<AromaControl *> &out)
    {
        for (const auto &ch : el->children()) {
            if (ch->is_text())
                continue;
            if (auto *c = dynamic_cast<AromaControl *>(ch.get()))
                out.push_back(c);
            collect_controls(ch, out);
        }
    }
};

struct AromaWebPage;
static void js_engine_reset(AromaWebPage *page);
static void js_engine_free(AromaWebPage *page);
static void js_run_page_scripts(AromaWebPage *page);
static void js_fire_load_events(AromaWebPage *page);
static bool js_dispatch_click(AromaWebPage *page, const litehtml::element::ptr &el);
static void js_dispatch_event(AromaWebPage *page, const litehtml::element::ptr &el,
                              const char *type);
static void js_click_element(AromaWebPage *page, const litehtml::element::ptr &el);
#ifdef AROMA_WEBVIEW_JS
static JSValue js_event_prevent(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_event_prevented_get(JSContext *ctx, JSValueConst t);
#endif
static bool js_run_timers(AromaWebPage *page);
static void page_relayout(AromaWebPage *page);
static void page_submit_form(AromaWebPage *page, AromaControl *from,
                             const litehtml::element::ptr &form);
static bool page_navigate(AromaWebPage *page, const std::string &url);
static bool page_process_pending(AromaWebPage *page);

struct AromaWebPage {
    AromaContainer *cont = nullptr;
    litehtml::document::ptr doc;
    std::vector<uint8_t> buf;
    int view_w = 1;
    int view_h = 1;
    int content_w = 0;
    int content_h = 0;
    std::string url;
    std::string source_html;
    std::string source_base;
    struct Hist {
        std::string url;
        std::string html;
        std::string base;
    };
    std::vector<Hist> hist;
    int hindex = -1;
    bool fonts_on = false;
    bool dom_dirty = false;
    unsigned nav_gen = 0;
    std::map<std::string, std::string> cookies;
    std::map<std::string, std::string> storage;
    std::string last_js_result;
    struct Bridge {
        std::string name;
        void *fn = nullptr;
        void *user = nullptr;
    };
    std::vector<Bridge> bridges;
    bool pending_reload = false;
    bool in_doc_call = false;
    bool layout_pending = false;
    bool scroll_request = false;
    int pending_scroll_x = 0;
    int pending_scroll_y = 0;
    int view_scroll_x = 0;
    int view_scroll_y = 0;
#ifdef AROMA_WEBVIEW_JS
    JsEngine *js = nullptr;
#endif
};

const AromaContainer::Image *AromaContainer::ensure_image(const std::string &abs)
{
    auto it = images.find(abs);
    if (it != images.end())
        return &it->second;
    Image img;
    std::vector<uint8_t> raw;
    bool got = owner ? fetch_jar_bytes(&owner->cookies, abs, raw)
                     : fetch_bytes(abs, raw);
    if (got && !raw.empty()) {
        int w = 0, h = 0, comp = 0;
        unsigned char *px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &comp, 4);
        if (px && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            img.px.assign(px, px + (size_t)w * (size_t)h * 4u);
            img.w = w;
            img.h = h;
        }
        if (px)
            stbi_image_free(px);
    }
    auto placed = images.emplace(abs, std::move(img));
    return &placed.first->second;
}

void AromaContainer::import_css(std::string &text, const std::string &url,
                                std::string &baseurl)
{
    std::string abs = absolute_url(url.c_str(), nullptr);
    text = owner ? fetch_jar_text(&owner->cookies, abs) : fetch_text(abs);
    if (text.empty())
        return;
    size_t scheme = abs.find("://");
    if (scheme != std::string::npos) {
        size_t slash = abs.rfind('/');
        baseurl = (slash != std::string::npos && slash > scheme + 2)
                      ? abs.substr(0, slash + 1)
                      : abs;
    } else {
        size_t slash = abs.rfind('/');
        baseurl = (slash != std::string::npos) ? abs.substr(0, slash + 1) : abs;
    }
}

static void page_clear_doc(AromaWebPage *page)
{
    page->doc.reset();
    page->cont->images.clear();
    page->cont->clip_stack.clear();
    page->cont->pending_nav.clear();
    page->cont->title.clear();
    page->cont->forget_controls();
}

static bool page_build(AromaWebPage *page, const std::string &html, const std::string &base)
{
    page_clear_doc(page);
    page->cont->base_url = base;
    try {
        page->doc = litehtml::document::createFromString(std::string(html), page->cont);
    } catch (...) {
        page->doc.reset();
    }
    if (!page->doc)
        return false;
    page->source_html = html;
    page->source_base = base;
    page->doc->render(page->view_w);
    page->content_w = px(page->doc->width());
    page->content_h = px(page->doc->height());
    if (page->content_w < 0)
        page->content_w = 0;
    if (page->content_h < 0)
        page->content_h = 0;
    js_engine_reset(page);

    js_run_page_scripts(page);
    js_fire_load_events(page);
    page->dom_dirty = true;
    return true;
}

static void page_push_history(AromaWebPage *page, const std::string &url,
                              const std::string &html, const std::string &base)
{
    if (page->hindex >= 0 && page->hindex < (int)page->hist.size()) {
        const AromaWebPage::Hist &cur = page->hist[(size_t)page->hindex];
        if (cur.url == url && cur.html == html)
            return;
    }
    if (page->hindex + 1 < (int)page->hist.size())
        page->hist.resize((size_t)page->hindex + 1);
    if (page->hist.size() > 64)
        page->hist.erase(page->hist.begin());
    page->hist.push_back({url, html, base});
    page->hindex = (int)page->hist.size() - 1;
}

#ifdef AROMA_WEBVIEW_JS

static int64_t js_now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class JsEngine
{
  public:
    AromaWebPage *page = nullptr;
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;

    struct Timer {
        int id = 0;
        bool repeat = false;
        int64_t interval = 0;
        int64_t next = 0;
        JSValue func = JS_UNDEFINED;
    };
    std::vector<Timer> timers;
    int next_timer_id = 1;

    std::map<litehtml::element *, std::map<std::string, std::vector<JSValue>>> listeners;
    std::vector<JSValue> doc_listeners;
    std::map<std::string, std::vector<JSValue>> doc_events;
    std::map<std::string, std::vector<JSValue>> win_events;

    static JSClassID el_class_id;
    static bool class_ids_init;

    explicit JsEngine(AromaWebPage *p) : page(p) {}
    ~JsEngine() { shutdown(); }

    static void init_class_ids()
    {
        if (!class_ids_init) {
            JS_NewClassID(&el_class_id);
            class_ids_init = true;
        }
    }

    static void el_finalizer(JSRuntime *, JSValue val)
    {
        delete (std::shared_ptr<litehtml::element> *)JS_GetOpaque(val, el_class_id);
    }

    bool reset()
    {
        shutdown();
        init_class_ids();
        rt = JS_NewRuntime();
        if (!rt)
            return false;
        ctx = JS_NewContext(rt);
        if (!ctx) {
            JS_FreeRuntime(rt);
            rt = nullptr;
            return false;
        }
        JS_SetContextOpaque(ctx, this);
        static const JSClassDef el_class = {"Element", el_finalizer};
        if (JS_NewClass(rt, el_class_id, &el_class) < 0)
            return false;
        JSValue proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, proto, el_proto_funcs(),
                                   el_proto_count());
        JS_SetClassProto(ctx, el_class_id, proto);
        install_globals();
        install_bridges();
        return true;
    }

    void shutdown()
    {
        clear_listeners();
        clear_timers();
        if (ctx) {
            JS_FreeContext(ctx);
            ctx = nullptr;
        }
        if (rt) {
            JS_FreeRuntime(rt);
            rt = nullptr;
        }
    }

    void clear_timers()
    {
        if (ctx) {
            for (auto &t : timers)
                JS_FreeValue(ctx, t.func);
        }
        timers.clear();
    }

    void clear_listeners()
    {
        if (ctx) {
            for (auto &kv : listeners)
                for (auto &tk : kv.second)
                    for (JSValue f : tk.second)
                        JS_FreeValue(ctx, f);
            for (auto &tk : doc_events)
                for (JSValue f : tk.second)
                    JS_FreeValue(ctx, f);
            for (auto &tk : win_events)
                for (JSValue f : tk.second)
                    JS_FreeValue(ctx, f);
        }
        listeners.clear();
        doc_events.clear();
        win_events.clear();
    }

    void log_exception(const char *where)
    {
        JSValue e = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, e);
        LOG_INFO("aroma_webview: JS exception in %s: %s", where, msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, e);
        call_onerror(where);
    }

    void call_onerror(const char *where)
    {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue fn = JS_GetPropertyStr(ctx, global, "onerror");
        if (JS_IsFunction(ctx, fn)) {
            JSValue arg = JS_NewString(ctx, where);
            JSValue r = JS_Call(ctx, fn, global, 1, &arg);
            JS_FreeValue(ctx, arg);
            if (JS_IsException(r)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
            }
            JS_FreeValue(ctx, r);
        }
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, global);
    }

    bool eval(const char *code, const char *file, std::string *out_result)
    {
        if (!ctx || !code)
            return false;
        JSValue res = JS_Eval(ctx, code, strlen(code), file ? file : "<page>",
                              JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(res)) {
            log_exception(file ? file : "eval");
            return false;
        }
        if (out_result) {
            const char *s = JS_ToCString(ctx, res);
            *out_result = s ? s : "undefined";
            if (s)
                JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, res);
        return true;
    }

    JSValue call_func(JSValue func, int argc, JSValue *argv)
    {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue r = JS_Call(ctx, func, global, argc, argv);
        JS_FreeValue(ctx, global);
        if (JS_IsException(r)) {
            log_exception("listener");
            return JS_UNDEFINED;
        }
        return r;
    }

    bool pump_timers()
    {
        if (!ctx || timers.empty())
            return false;
        int64_t now = js_now_ms();
        bool fired = false;
        for (size_t i = 0; i < timers.size();) {
            Timer &t = timers[i];
            if (now < t.next) {
                i++;
                continue;
            }
            fired = true;
            JSValue r = call_func(t.func, 0, nullptr);
            JS_FreeValue(ctx, r);
            if (t.repeat) {
                t.next = now + t.interval;
                i++;
            } else {
                JS_FreeValue(ctx, t.func);
                timers.erase(timers.begin() + i);
            }
        }
        return fired;
    }

    JSValue wrap_element(const litehtml::element::ptr &el)
    {
        if (!el)
            return JS_NULL;
        JSValue o = JS_NewObjectClass(ctx, el_class_id);
        if (JS_IsException(o))
            return JS_EXCEPTION;
        JS_SetOpaque(o, new std::shared_ptr<litehtml::element>(el));
        return o;
    }

    litehtml::element::ptr unwrap_element(JSValueConst v)
    {
        auto *sp = (std::shared_ptr<litehtml::element> *)JS_GetOpaque2(ctx, v, el_class_id);
        return sp ? *sp : nullptr;
    }

    bool is_wrapped_element(JSValueConst v)
    {
        return JS_GetOpaque2(ctx, v, el_class_id) != nullptr;
    }

    void install_globals();
    void install_bridges();
    static const JSCFunctionListEntry *el_proto_funcs();
    static int el_proto_count();
};

JSClassID JsEngine::el_class_id = 0;
bool JsEngine::class_ids_init = false;

#ifdef AROMA_WEBVIEW_JS

static JSClassID style_class_id = 0;
static JSClassID clslist_class_id = 0;
static JSClassID event_class_id = 0;

struct JsStyle {
    std::shared_ptr<litehtml::element> el;
};
struct JsCls {
    std::shared_ptr<litehtml::element> el;
};
struct JsEvent {
    std::string type;
    std::shared_ptr<litehtml::element> target;
    bool prevented = false;
};

static void js_style_final(JSRuntime *, JSValue v)
{
    delete (JsStyle *)JS_GetOpaque(v, style_class_id);
}
static void js_cls_final(JSRuntime *, JSValue v)
{
    delete (JsCls *)JS_GetOpaque(v, clslist_class_id);
}
static void js_event_final(JSRuntime *, JSValue v)
{
    delete (JsEvent *)JS_GetOpaque(v, event_class_id);
}

static void js_register_aux_classes(JSContext *ctx, JSRuntime *rt)
{
    static bool done = false;
    if (!done) {
        JS_NewClassID(&style_class_id);
        JS_NewClassID(&clslist_class_id);
        JS_NewClassID(&event_class_id);
        done = true;
    }
    static const JSClassDef sdef = {"Style", js_style_final};
    static const JSClassDef cdef = {"ClsList", js_cls_final};
    static const JSClassDef edef = {"Event", js_event_final};
    JS_NewClass(rt, style_class_id, &sdef);
    JS_NewClass(rt, clslist_class_id, &cdef);
    JS_NewClass(rt, event_class_id, &edef);
}

static std::string js_string(JSContext *ctx, JSValueConst v);
static int js_int(JSContext *ctx, JSValueConst v, int def = 0);
static JSValue js_console_log(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_set_timeout(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_set_interval(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_clear_timer(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_alert(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_win_open(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_win_add_listener(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue js_make_location(JSContext *ctx);
static JSValue js_make_storage(JSContext *ctx);

static JsEngine *js_engine(JSContext *ctx)
{
    return (JsEngine *)JS_GetContextOpaque(ctx);
}

static litehtml::element::ptr js_doc_root(JSContext *ctx)
{
    JsEngine *eng = js_engine(ctx);
    if (!eng->page->doc)
        return nullptr;
    return eng->page->doc->root();
}

static void js_collect_by_tag(const litehtml::element::ptr &el, const char *tag,
                              std::vector<litehtml::element::ptr> &out)
{
    if (!el)
        return;
    for (const auto &ch : el->children()) {
        if (ch->is_text())
            continue;
        if (!strcasecmp(ch->get_tagName(), tag))
            out.push_back(ch);
        js_collect_by_tag(ch, tag, out);
    }
}

static litehtml::element::ptr js_find_body(JSContext *ctx)
{
    auto root = js_doc_root(ctx);
    if (!root)
        return nullptr;
    std::vector<litehtml::element::ptr> out;
    js_collect_by_tag(root, "body", out);
    return out.empty() ? nullptr : out[0];
}

static JSValue js_wrap_list(JSContext *ctx,
                            const std::vector<litehtml::element::ptr> &els)
{
    JsEngine *eng = js_engine(ctx);
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < els.size(); i++) {
        JSValue w = eng->wrap_element(els[i]);
        if (JS_IsException(w)) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, w);
    }
    return arr;
}

static JSValue js_query(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, bool all)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1)
        return all ? JS_NewArray(ctx) : JS_NULL;
    std::string sel = js_string(ctx, argv[0]);
    std::vector<SelStep> steps;
    auto root = js_doc_root(ctx);
    if (!parse_selector(sel, steps) || !root)
        return all ? JS_NewArray(ctx) : JS_NULL;
    std::vector<litehtml::element::ptr> out;
    query_walk(root, steps, out, !all);
    if (!all)
        return out.empty() ? JS_NULL : eng->wrap_element(out[0]);
    return js_wrap_list(ctx, out);
}

static JSValue js_query_selector(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_query(ctx, t, argc, argv, false);
}

static JSValue js_query_selector_all(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_query(ctx, t, argc, argv, true);
}

static JSValue js_get_element_by_id(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1)
        return JS_NULL;
    std::string id = js_string(ctx, argv[0]);
    auto root = js_doc_root(ctx);
    if (!root || id.empty())
        return JS_NULL;
    std::vector<SelStep> steps;
    SelStep st;
    st.compound.id = id;
    steps.push_back(st);
    std::vector<litehtml::element::ptr> out;
    query_walk(root, steps, out, true);
    return out.empty() ? JS_NULL : eng->wrap_element(out[0]);
}

static JSValue js_get_by_tag(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int mode)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1)
        return JS_NewArray(ctx);
    std::string key = js_string(ctx, argv[0]);
    auto root = js_doc_root(ctx);
    std::vector<litehtml::element::ptr> out;
    if (root && !key.empty()) {
        if (mode == 0) {
            js_collect_by_tag(root, key.c_str(), out);
        } else {
            std::vector<SelStep> steps;
            SelStep st;
            st.compound.classes.push_back(key);
            steps.push_back(st);
            query_walk(root, steps, out, false);
        }
    }
    return js_wrap_list(ctx, out);
}

static JSValue js_get_by_tagname(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_get_by_tag(ctx, t, argc, argv, 0);
}

static JSValue js_get_by_classname(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_get_by_tag(ctx, t, argc, argv, 1);
}

static JSValue js_doc_create_element(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1 || !eng->page->doc)
        return JS_NULL;
    std::string tag = js_string(ctx, argv[0]);
    if (tag.empty())
        return JS_NULL;
    litehtml::string_map attrs;
    auto el = eng->page->doc->create_element(tag.c_str(), attrs);
    if (!el)
        return JS_NULL;
    el->parse_attributes();
    return eng->wrap_element(el);
}

static JSValue js_doc_title_get(JSContext *ctx, JSValueConst)
{
    return JS_NewString(ctx, js_engine(ctx)->page->cont->title.c_str());
}

static JSValue js_doc_cookie_get(JSContext *ctx, JSValueConst)
{
    JsEngine *eng = js_engine(ctx);
    std::string out;
    for (const auto &kv : eng->page->cookies) {
        if (!out.empty())
            out += "; ";
        out += kv.first + "=" + kv.second;
    }
    return JS_NewString(ctx, out.c_str());
}

static void js_cookie_set(AromaWebPage *page, const std::string &kv)
{
    size_t semi = kv.find(';');
    std::string pair = kv.substr(0, semi);
    size_t eq = pair.find('=');
    if (eq == std::string::npos)
        return;
    std::string name = pair.substr(0, eq);
    std::string value = pair.substr(eq + 1);
    while (!name.empty() && isspace((unsigned char)name.front()))
        name.erase(name.begin());
    while (!name.empty() && isspace((unsigned char)name.back()))
        name.pop_back();
    while (!value.empty() && isspace((unsigned char)value.front()))
        value.erase(value.begin());
    while (!value.empty() && isspace((unsigned char)value.back()))
        value.pop_back();
    if (!name.empty())
        page->cookies[name] = value;
}

static JSValue js_doc_cookie_set(JSContext *ctx, JSValueConst, JSValueConst val)
{
    js_cookie_set(js_engine(ctx)->page, js_string(ctx, val));
    return JS_UNDEFINED;
}

static JSValue js_doc_write(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto body = js_find_body(ctx);
    if (!body || !eng->page->doc)
        return JS_UNDEFINED;
    std::string html;
    for (int i = 0; i < argc; i++)
        html += js_string(ctx, argv[i]);
    if (html.empty())
        return JS_UNDEFINED;
    eng->page->doc->append_children_from_string(*body, html.c_str(), false);
    body->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_doc_add_listener(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_UNDEFINED;
    eng->doc_events[js_string(ctx, argv[0])].push_back(JS_DupValue(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue js_doc_body(JSContext *ctx, JSValueConst)
{
    JsEngine *eng = js_engine(ctx);
    auto body = js_find_body(ctx);
    return body ? eng->wrap_element(body) : JS_NULL;
}

static JSValue js_doc_el(JSContext *ctx, JSValueConst)
{
    JsEngine *eng = js_engine(ctx);
    auto root = js_doc_root(ctx);
    return root ? eng->wrap_element(root) : JS_NULL;
}

static JSValue js_win_add_listener(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_UNDEFINED;
    eng->win_events[js_string(ctx, argv[0])].push_back(JS_DupValue(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue js_win_inner_w(JSContext *ctx, JSValueConst)
{
    return JS_NewInt32(ctx, js_engine(ctx)->page->view_w);
}

static JSValue js_win_inner_h(JSContext *ctx, JSValueConst)
{
    return JS_NewInt32(ctx, js_engine(ctx)->page->view_h);
}

static JSValue js_win_scroll_x(JSContext *ctx, JSValueConst)
{
    return JS_NewInt32(ctx, js_engine(ctx)->page->view_scroll_x);
}

static JSValue js_win_scroll_y(JSContext *ctx, JSValueConst)
{
    return JS_NewInt32(ctx, js_engine(ctx)->page->view_scroll_y);
}

static JSValue js_win_scroll_to(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    int x = argc > 0 ? js_int(ctx, argv[0]) : 0;
    int y = argc > 1 ? js_int(ctx, argv[1]) : 0;
    eng->page->pending_scroll_x = x;
    eng->page->pending_scroll_y = y;
    eng->page->scroll_request = true;
    return JS_UNDEFINED;
}

static JSValue js_win_open(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    (void)ctx;
    return JS_NULL;
}

void JsEngine::install_globals()
{
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "info", JS_NewCFunction(ctx, js_console_log, "info", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_SetPropertyStr(ctx, global, "setTimeout",
                      JS_NewCFunction(ctx, js_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval",
                      JS_NewCFunction(ctx, js_set_interval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout",
                      JS_NewCFunction(ctx, js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval",
                      JS_NewCFunction(ctx, js_clear_timer, "clearInterval", 1));
    JS_SetPropertyStr(ctx, global, "alert", JS_NewCFunction(ctx, js_alert, "alert", 1));
    JS_SetPropertyStr(ctx, global, "open", JS_NewCFunction(ctx, js_win_open, "open", 1));
    JS_SetPropertyStr(ctx, global, "addEventListener",
                      JS_NewCFunction(ctx, js_win_add_listener, "addEventListener", 2));
    static const JSCFunctionListEntry win_gets[] = {
        JS_CGETSET_DEF("innerWidth", js_win_inner_w, nullptr),
        JS_CGETSET_DEF("innerHeight", js_win_inner_h, nullptr),
        JS_CGETSET_DEF("scrollX", js_win_scroll_x, nullptr),
        JS_CGETSET_DEF("scrollY", js_win_scroll_y, nullptr),
    };
    JS_SetPropertyFunctionList(ctx, global, win_gets, 4);
    JS_SetPropertyStr(ctx, global, "scrollTo",
                      JS_NewCFunction(ctx, js_win_scroll_to, "scrollTo", 2));
    JSValue nav = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, nav, "userAgent", JS_NewString(ctx, "AromaWebView/1.0"));
    JS_SetPropertyStr(ctx, global, "navigator", nav);
    JS_SetPropertyStr(ctx, global, "location", js_make_location(ctx));
    JS_SetPropertyStr(ctx, global, "localStorage", js_make_storage(ctx));
    JS_SetPropertyStr(ctx, global, "sessionStorage", js_make_storage(ctx));
    JSValue doc = JS_NewObject(ctx);
    static const JSCFunctionListEntry doc_funcs[] = {
        JS_CFUNC_DEF("getElementById", 1, js_get_element_by_id),
        JS_CFUNC_DEF("querySelector", 1, js_query_selector),
        JS_CFUNC_DEF("querySelectorAll", 1, js_query_selector_all),
        JS_CFUNC_DEF("getElementsByTagName", 1, js_get_by_tagname),
        JS_CFUNC_DEF("getElementsByClassName", 1, js_get_by_classname),
        JS_CFUNC_DEF("createElement", 1, js_doc_create_element),
        JS_CFUNC_DEF("write", 1, js_doc_write),
        JS_CFUNC_DEF("addEventListener", 2, js_doc_add_listener),
        JS_CGETSET_DEF("title", js_doc_title_get, nullptr),
        JS_CGETSET_DEF("cookie", js_doc_cookie_get, js_doc_cookie_set),
        JS_CGETSET_DEF("body", js_doc_body, nullptr),
        JS_CGETSET_DEF("documentElement", js_doc_el, nullptr),
    };
    JS_SetPropertyFunctionList(ctx, doc, doc_funcs, 12);
    JS_SetPropertyStr(ctx, global, "document", doc);
    JS_FreeValue(ctx, global);
}

static litehtml::element::ptr js_this_el(JSContext *ctx, JSValueConst this_val)
{
    JsEngine *eng = js_engine(ctx);
    return eng ? eng->unwrap_element(this_val) : nullptr;
}

static JSValue js_el_attr_get(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_NULL;
    const char *v = el->get_attr(js_string(ctx, argv[0]).c_str());
    return v ? JS_NewString(ctx, v) : JS_NULL;
}

static JSValue js_el_attr_has(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_NewBool(ctx, 0);
    return JS_NewBool(ctx, el->get_attr(js_string(ctx, argv[0]).c_str()) != nullptr);
}

static JSValue js_el_attr_set(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 2)
        return JS_UNDEFINED;
    std::string name = js_string(ctx, argv[0]);
    std::string val = js_string(ctx, argv[1]);
    el->set_attr(name.c_str(), val.c_str());

    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (!strcasecmp(name.c_str(), "value") && !ctl->is_select) {
            ctl->ctl_value = val;
            ctl->cursor = (int)val.size();
        } else if (!strcasecmp(name.c_str(), "checked")) {
            ctl->checked = true;
        }
    }
    el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_el_attr_remove(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_UNDEFINED;
    std::string name = js_string(ctx, argv[0]);
    el->set_attr(name.c_str(), nullptr);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (!strcasecmp(name.c_str(), "checked"))
            ctl->checked = false;
        else if (!strcasecmp(name.c_str(), "value") && !ctl->is_select) {
            ctl->ctl_value.clear();
            ctl->cursor = 0;
        }
    }
    el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_el_add_listener(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_UNDEFINED;
    eng->listeners[el.get()][js_string(ctx, argv[0])].push_back(JS_DupValue(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue js_el_remove_listener(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_UNDEFINED;
    auto it = eng->listeners.find(el.get());
    if (it == eng->listeners.end())
        return JS_UNDEFINED;
    auto jt = it->second.find(js_string(ctx, argv[0]));
    if (jt == it->second.end() || jt->second.empty())
        return JS_UNDEFINED;
    JS_FreeValue(ctx, jt->second.back());
    jt->second.pop_back();
    return JS_UNDEFINED;
}

static JSValue js_el_tag_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    const char *tag = el->get_tagName();
    std::string up = tag ? tag : "";
    for (auto &c : up)
        c = (char)toupper((unsigned char)c);
    return JS_NewString(ctx, up.c_str());
}

static JSValue js_el_text_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NewString(ctx, "");
    return JS_NewString(ctx, element_text(el).c_str());
}

static bool is_attached(AromaWebPage *page, const litehtml::element::ptr &el)
{
    if (!page || !page->doc || !el)
        return false;
    auto root = page->doc->root();
    auto cur = el;
    while (cur) {
        if (cur.get() == root.get())
            return true;
        cur = cur->parent();
    }
    return false;
}

static void attach_render_tree(AromaWebPage *page, const litehtml::element::ptr &el)
{
    if (!page || !page->doc || !el || !is_attached(page, el))
        return;
    std::vector<litehtml::element::ptr> chain;
    auto cur = el;
    while (cur && !cur->get_render_item()) {
        chain.push_back(cur);
        cur = cur->parent();
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        auto par = (*it)->parent();
        auto pri = par ? par->get_render_item() : nullptr;
        if (!pri) {
            auto root = page->doc->root();
            pri = root ? root->get_render_item() : nullptr;
        }
        if (!pri)
            return;
        auto ri = (*it)->create_render_item(pri);
        if (ri) {
            ri = ri->init();
            pri->add_child(ri);
        }
    }
    for (const auto &ch : el->children())
        attach_render_tree(page, ch);
}


static litehtml::element::ptr scratch_div(AromaWebPage *page)
{
    // Hidden attached scaffold for parsing fragments meant for detached nodes.
    auto body = page->doc->root();
    std::vector<litehtml::element::ptr> found;
    {
        std::vector<SelStep> steps;
        SelStep st;
        st.compound.id = "__aroma_scratch";
        steps.push_back(st);
        query_walk(body, steps, found, true);
    }
    if (!found.empty())
        return found.front();
    litehtml::string_map attrs;
    auto el = page->doc->create_element("div", attrs);
    if (!el)
        return nullptr;
    // NOTE: no display:none here: hidden elements own no render item,
    // and the fragment parser requires one. The scratch is always empty
    // outside parsing, so its empty block box has zero height.
    el->set_attr("id", "__aroma_scratch");
    el->parse_attributes();
    auto root = page->doc->root();
    std::vector<litehtml::element::ptr> bodies;
    js_collect_by_tag(root, "body", bodies);
    if (bodies.empty())
        return nullptr;
    if (!bodies.front()->appendChild(el))
        return nullptr;
    el->compute_styles(true);
    return el;
}

static bool set_inner_html(AromaWebPage *page, const litehtml::element::ptr &el,
                           const std::string &html, bool replace)
{
    if (!page || !page->doc || !el)
        return false;
    if (html.empty()) {
        clear_children(el);
        return true;
    }
    if (is_attached(page, el)) {
        attach_render_tree(page, el);
        if (el->get_render_item()) {
            page->doc->append_children_from_string(*el, html.c_str(), replace);
            return true;
        }
        // Item-less target (e.g. display:none): fall through to scratch.
    }
    auto scratch = scratch_div(page);
    if (!scratch)
        return false;
    if (replace)
        clear_children(el);
    attach_render_tree(page, scratch);
    page->doc->append_children_from_string(*scratch, html.c_str(), true);
    std::vector<litehtml::element::ptr> kids;
    for (const auto &ch : scratch->children())
        kids.push_back(ch);
    for (const auto &ch : kids) {
        if (el->appendChild(ch)) {
            ch->compute_styles(true);
            attach_render_tree(page, ch);
        }
    }
    return true;
}


static JSValue js_el_text_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    std::string str = js_string(ctx, val);
    clear_children(el);
    if (!str.empty()) {
        auto tn = std::make_shared<litehtml::el_text>(str.c_str(), eng->page->doc);
        if (el->appendChild(tn))
            attach_render_tree(eng->page, tn);
    }
    el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_el_html_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NewString(ctx, "");
    return JS_NewString(ctx, serialize_children(el).c_str());
}

static JSValue js_el_html_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    std::string str = js_string(ctx, val);
    set_inner_html(eng->page, el, str, true);
    el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_el_value_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NewString(ctx, "");
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (ctl->is_select) {
            if (ctl->sel_index >= 0 && ctl->sel_index < (int)ctl->options.size())
                return JS_NewString(ctx, ctl->options[(size_t)ctl->sel_index].value.c_str());
            return JS_NewString(ctx, "");
        }
        ctl->ensure_value();
        return JS_NewString(ctx, ctl->ctl_value.c_str());
    }
    const char *v = el->get_attr("value");
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_el_value_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    std::string s = js_string(ctx, val);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (ctl->is_select) {
            for (size_t i = 0; i < ctl->options.size(); i++) {
                if (ctl->options[i].value == s) {
                    ctl->sel_index = (int)i;
                    break;
                }
            }
        } else {
            ctl->ctl_value = s;
            ctl->cursor = (int)s.size();
        }
        ctl->dirty = true;
    } else {
        el->set_attr("value", s.c_str());
    }
    eng->page->dom_dirty = true;
    return JS_UNDEFINED;
}

static JSValue js_el_checked_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get()))
        return JS_NewBool(ctx, ctl->checked ? 1 : 0);
    return JS_NewBool(ctx, 0);
}

static JSValue js_el_checked_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        int32_t b = 0;
        JS_ToInt32(ctx, &b, val);
        ctl->checked = b != 0;
        eng->page->dom_dirty = true;
    }
    return JS_UNDEFINED;
}

static JSValue js_el_selidx_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get()))
        if (ctl->is_select)
            return JS_NewInt32(ctx, ctl->sel_index);
    return JS_NewInt32(ctx, -1);
}

static JSValue js_el_selidx_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (ctl->is_select) {
            int idx = js_int(ctx, val, -1);
            if (idx >= 0 && idx < (int)ctl->options.size())
                ctl->sel_index = idx;
            eng->page->dom_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_el_disabled_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get()))
        return JS_NewBool(ctx, ctl->disabled ? 1 : 0);
    return JS_NewBool(ctx, 0);
}

static JSValue js_el_parent(JSContext *ctx, JSValueConst t)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    return eng->wrap_element(el->parent());
}

static JSValue js_el_children(JSContext *ctx, JSValueConst t)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    std::vector<litehtml::element::ptr> out;
    if (el) {
        for (const auto &ch : el->children())
            if (!ch->is_text())
                out.push_back(ch);
    }
    return js_wrap_list(ctx, out);
}

static JSValue js_el_sibling(JSContext *ctx, JSValueConst t, bool next)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    auto par = el->parent();
    if (!par)
        return JS_NULL;
    bool found = false;
    litehtml::element::ptr cand;
    for (const auto &ch : par->children()) {
        if (ch->is_text())
            continue;
        if (ch.get() == el.get()) {
            found = true;
            if (!next && cand)
                return eng->wrap_element(cand);
            cand.reset();
            continue;
        }
        if (found && next)
            return eng->wrap_element(ch);
        cand = ch;
    }
    return JS_NULL;
}

static JSValue js_el_next(JSContext *ctx, JSValueConst t)
{
    return js_el_sibling(ctx, t, true);
}

static JSValue js_el_prev(JSContext *ctx, JSValueConst t)
{
    return js_el_sibling(ctx, t, false);
}

static JSValue js_el_first(JSContext *ctx, JSValueConst t)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (el) {
        for (const auto &ch : el->children())
            if (!ch->is_text())
                return eng->wrap_element(ch);
    }
    return JS_NULL;
}

static JSValue js_el_by_tag(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    std::vector<litehtml::element::ptr> out;
    if (el && argc > 0)
        js_collect_by_tag(el, js_string(ctx, argv[0]).c_str(), out);
    return js_wrap_list(ctx, out);
}

static JSValue js_el_query(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv, bool all)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return all ? JS_NewArray(ctx) : JS_NULL;
    std::vector<SelStep> steps;
    if (!parse_selector(js_string(ctx, argv[0]), steps))
        return all ? JS_NewArray(ctx) : JS_NULL;
    std::vector<litehtml::element::ptr> out;
    query_walk(el, steps, out, !all);
    if (!all)
        return out.empty() ? JS_NULL : eng->wrap_element(out[0]);
    return js_wrap_list(ctx, out);
}

static JSValue js_el_query_one(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_el_query(ctx, t, argc, argv, false);
}

static JSValue js_el_query_all(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_el_query(ctx, t, argc, argv, true);
}

static JSValue js_el_matches(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_NewBool(ctx, 0);
    std::vector<SelStep> steps;
    if (!parse_selector(js_string(ctx, argv[0]), steps) || steps.size() != 1)
        return JS_NewBool(ctx, 0);
    return JS_NewBool(ctx, match_compound(el, steps.back().compound) ? 1 : 0);
}

static JSValue js_el_closest(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_NULL;
    std::vector<SelStep> steps;
    if (!parse_selector(js_string(ctx, argv[0]), steps) || steps.size() != 1)
        return JS_NULL;
    auto cur = el;
    while (cur) {
        if (match_compound(cur, steps.back().compound))
            return eng->wrap_element(cur);
        cur = cur->parent();
    }
    return JS_NULL;
}

static JSValue js_el_append(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el || argc < 1)
        return JS_UNDEFINED;
    if (eng->is_wrapped_element(argv[0])) {
        auto child = eng->unwrap_element(argv[0]);
        if (child && el->appendChild(child)) {
            child->compute_styles(true);
            attach_render_tree(eng->page, child);
            page_relayout(eng->page);
        }
    } else {
        std::string html = js_string(ctx, argv[0]);
        if (!html.empty() && set_inner_html(eng->page, el, html, false)) {
            el->compute_styles(true);
            page_relayout(eng->page);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_el_remove(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    auto par = el->parent();
    if (par && par->removeChild(el))
        page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JsStyle *js_this_style(JSContext *ctx, JSValueConst t)
{
    return (JsStyle *)JS_GetOpaque2(ctx, t, style_class_id);
}

static JSValue js_style_csstext_get(JSContext *ctx, JSValueConst t)
{
    JsStyle *s = js_this_style(ctx, t);
    if (!s || !s->el)
        return JS_NewString(ctx, "");
    const char *v = s->el->get_attr("style");
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_style_csstext_set(JSContext *ctx, JSValueConst t, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    JsStyle *s = js_this_style(ctx, t);
    if (!s || !s->el)
        return JS_UNDEFINED;
    s->el->set_attr("style", js_string(ctx, val).c_str());
    s->el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_style_setprop(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    JsStyle *s = js_this_style(ctx, t);
    if (!s || !s->el || argc < 2)
        return JS_UNDEFINED;
    std::string cur = js_string(ctx, JS_GetPropertyStr(ctx, t, "cssText"));
    std::string prop = js_string(ctx, argv[0]);
    std::string val = js_string(ctx, argv[1]);

    std::string out;
    bool done = false;
    size_t p = 0;
    while (p < cur.size()) {
        size_t semi = cur.find(';', p);
        std::string decl = cur.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        size_t colon = decl.find(':');
        std::string name = colon == std::string::npos ? decl : decl.substr(0, colon);
        while (!name.empty() && isspace((unsigned char)name.front()))
            name.erase(name.begin());
        while (!name.empty() && isspace((unsigned char)name.back()))
            name.pop_back();
        if (!name.empty() && strcasecmp(name.c_str(), prop.c_str()) != 0) {
            if (!out.empty())
                out += "; ";
            out += decl;
        } else if (!name.empty()) {
            if (!out.empty())
                out += "; ";
            out += prop + ": " + val;
            done = true;
        }
        if (semi == std::string::npos)
            break;
        p = semi + 1;
    }
    if (!done) {
        if (!out.empty())
            out += "; ";
        out += prop + ": " + val;
    }
    s->el->set_attr("style", out.c_str());
    s->el->compute_styles(true);
    page_relayout(eng->page);
    return JS_UNDEFINED;
}

static JSValue js_style_getprop(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsStyle *s = js_this_style(ctx, t);
    if (!s || !s->el || argc < 1)
        return JS_NewString(ctx, "");
    std::string cur = js_string(ctx, JS_GetPropertyStr(ctx, t, "cssText"));
    std::string prop = js_string(ctx, argv[0]);
    size_t p = 0;
    while (p < cur.size()) {
        size_t semi = cur.find(';', p);
        std::string decl = cur.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        size_t colon = decl.find(':');
        if (colon != std::string::npos) {
            std::string name = decl.substr(0, colon);
            while (!name.empty() && isspace((unsigned char)name.front()))
                name.erase(name.begin());
            while (!name.empty() && isspace((unsigned char)name.back()))
                name.pop_back();
            if (!strcasecmp(name.c_str(), prop.c_str())) {
                std::string v = decl.substr(colon + 1);
                while (!v.empty() && isspace((unsigned char)v.front()))
                    v.erase(v.begin());
                while (!v.empty() && isspace((unsigned char)v.back()))
                    v.pop_back();
                return JS_NewString(ctx, v.c_str());
            }
        }
        if (semi == std::string::npos)
            break;
        p = semi + 1;
    }
    return JS_NewString(ctx, "");
}

static JSValue js_make_style(JSContext *ctx, const litehtml::element::ptr &el)
{
    JSValue o = JS_NewObjectClass(ctx, style_class_id);
    if (JS_IsException(o))
        return o;
    JsStyle *s = new (std::nothrow) JsStyle();
    if (!s) {
        JS_FreeValue(ctx, o);
        return JS_EXCEPTION;
    }
    s->el = el;
    JS_SetOpaque(o, s);
    static const JSCFunctionListEntry defs[] = {
        JS_CGETSET_DEF("cssText", js_style_csstext_get, js_style_csstext_set),
        JS_CFUNC_DEF("setProperty", 2, js_style_setprop),
        JS_CFUNC_DEF("getPropertyValue", 1, js_style_getprop),
    };
    JS_SetPropertyFunctionList(ctx, o, defs, 3);
    return o;
}

static JsCls *js_this_cls(JSContext *ctx, JSValueConst t)
{
    return (JsCls *)JS_GetOpaque2(ctx, t, clslist_class_id);
}

static std::vector<std::string> js_cls_split(const char *v)
{
    std::vector<std::string> out;
    if (!v)
        return out;
    std::string s = v;
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && isspace((unsigned char)s[p]))
            p++;
        size_t q = p;
        while (q < s.size() && !isspace((unsigned char)s[q]))
            q++;
        if (q > p)
            out.push_back(s.substr(p, q - p));
        p = q;
    }
    return out;
}

static void js_cls_write(JSContext *ctx, const std::shared_ptr<litehtml::element> &el,
                         const std::vector<std::string> &cls)
{
    std::string joined;
    for (size_t i = 0; i < cls.size(); i++) {
        if (i)
            joined += ' ';
        joined += cls[i];
    }
    el->set_attr("class", joined.c_str());
    el->compute_styles(true);
    js_engine(ctx)->page->dom_dirty = true;
}

static JSValue js_cls_add(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsCls *c = js_this_cls(ctx, t);
    if (!c || !c->el || argc < 1)
        return JS_UNDEFINED;
    auto cls = js_cls_split(c->el->get_attr("class"));
    std::string add = js_string(ctx, argv[0]);
    if (std::find(cls.begin(), cls.end(), add) == cls.end()) {
        cls.push_back(add);
        js_cls_write(ctx, c->el, cls);
    }
    return JS_UNDEFINED;
}

static JSValue js_cls_remove(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsCls *c = js_this_cls(ctx, t);
    if (!c || !c->el || argc < 1)
        return JS_UNDEFINED;
    auto cls = js_cls_split(c->el->get_attr("class"));
    std::string del = js_string(ctx, argv[0]);
    cls.erase(std::remove(cls.begin(), cls.end(), del), cls.end());
    js_cls_write(ctx, c->el, cls);
    return JS_UNDEFINED;
}

static JSValue js_cls_toggle(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsCls *c = js_this_cls(ctx, t);
    if (!c || !c->el || argc < 1)
        return JS_NewBool(ctx, 0);
    auto cls = js_cls_split(c->el->get_attr("class"));
    std::string name = js_string(ctx, argv[0]);
    bool has = std::find(cls.begin(), cls.end(), name) != cls.end();
    if (has)
        cls.erase(std::remove(cls.begin(), cls.end(), name), cls.end());
    else
        cls.push_back(name);
    js_cls_write(ctx, c->el, cls);
    return JS_NewBool(ctx, has ? 0 : 1);
}

static JSValue js_cls_contains(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    JsCls *c = js_this_cls(ctx, t);
    if (!c || !c->el || argc < 1)
        return JS_NewBool(ctx, 0);
    auto cls = js_cls_split(c->el->get_attr("class"));
    std::string name = js_string(ctx, argv[0]);
    return JS_NewBool(ctx, std::find(cls.begin(), cls.end(), name) != cls.end() ? 1 : 0);
}

static JSValue js_make_clslist(JSContext *ctx, const litehtml::element::ptr &el)
{
    JSValue o = JS_NewObjectClass(ctx, clslist_class_id);
    if (JS_IsException(o))
        return o;
    JsCls *c = new (std::nothrow) JsCls();
    if (!c) {
        JS_FreeValue(ctx, o);
        return JS_EXCEPTION;
    }
    c->el = el;
    JS_SetOpaque(o, c);
    static const JSCFunctionListEntry defs[] = {
        JS_CFUNC_DEF("add", 1, js_cls_add),
        JS_CFUNC_DEF("remove", 1, js_cls_remove),
        JS_CFUNC_DEF("toggle", 1, js_cls_toggle),
        JS_CFUNC_DEF("contains", 1, js_cls_contains),
    };
    JS_SetPropertyFunctionList(ctx, o, defs, 4);
    return o;
}

static JSValue js_el_id_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    const char *v = el ? el->get_attr("id") : nullptr;
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_el_class_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    const char *v = el ? el->get_attr("class") : nullptr;
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_el_name_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get()))
        return JS_NewString(ctx, ctl->ctl_name.c_str());
    const char *v = el ? el->get_attr("name") : nullptr;
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_el_type_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get()))
        return JS_NewString(ctx, ctl->ctl_type.c_str());
    const char *v = el ? el->get_attr("type") : nullptr;
    return JS_NewString(ctx, v ? v : "");
}

static JSValue js_el_style_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    return js_make_style(ctx, el);
}

static JSValue js_el_cls_get(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    return js_make_clslist(ctx, el);
}

static JSValue js_el_form_get(JSContext *ctx, JSValueConst t)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_NULL;
    auto f = AromaFormHelper::find_form(el);
    return f ? eng->wrap_element(f) : JS_NULL;
}

static JSValue js_el_options(JSContext *ctx, JSValueConst t)
{
    auto el = js_this_el(ctx, t);
    JSValue arr = JS_NewArray(ctx);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (ctl->is_select) {
            for (size_t i = 0; i < ctl->options.size(); i++) {
                JSValue o = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, o, "value",
                                  JS_NewString(ctx, ctl->options[i].value.c_str()));
                JS_SetPropertyStr(ctx, o, "text",
                                  JS_NewString(ctx, ctl->options[i].label.c_str()));
                JS_SetPropertyStr(ctx, o, "selected",
                                  JS_NewBool(ctx, (int)i == ctl->sel_index ? 1 : 0));
                JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
            }
        }
    }
    return arr;
}

static JSValue js_el_form_elements(JSContext *ctx, JSValueConst t)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    std::vector<litehtml::element::ptr> out;
    if (el) {
        std::vector<AromaControl *> ctls;
        AromaFormHelper::collect_controls(el, ctls);
        for (auto *c : ctls) {

            for (const auto &reg : eng->page->cont->controls) {
                if (reg.get() == (litehtml::element *)c) {
                    out.push_back(reg);
                    break;
                }
            }
        }
    }
    return js_wrap_list(ctx, out);
}

static JSValue js_el_form_submit(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    auto form = AromaFormHelper::find_form(el);
    if (!form)
        form = el;
    const char *tag = form->get_tagName();
    if (tag && !strcasecmp(tag, "form"))
        page_submit_form(eng->page, nullptr, form);
    return JS_UNDEFINED;
}

static JSValue js_el_focus(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        eng->page->cont->focused = ctl;
        eng->page->dom_dirty = true;
    }
    return JS_UNDEFINED;
}

static JSValue js_el_blur(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        if (eng->page->cont->focused == ctl) {
            eng->page->cont->focused = nullptr;
            eng->page->dom_dirty = true;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_el_click(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    auto el = js_this_el(ctx, t);
    if (!el)
        return JS_UNDEFINED;
    js_click_element(eng->page, el);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry *JsEngine_el_proto();
static int JsEngine_el_proto_count();

static const JSCFunctionListEntry el_proto_table[] = {
    JS_CFUNC_DEF("getAttribute", 1, js_el_attr_get),
    JS_CFUNC_DEF("hasAttribute", 1, js_el_attr_has),
    JS_CFUNC_DEF("setAttribute", 2, js_el_attr_set),
    JS_CFUNC_DEF("removeAttribute", 1, js_el_attr_remove),
    JS_CFUNC_DEF("addEventListener", 2, js_el_add_listener),
    JS_CFUNC_DEF("removeEventListener", 1, js_el_remove_listener),
    JS_CFUNC_DEF("getElementsByTagName", 1, js_el_by_tag),
    JS_CFUNC_DEF("querySelector", 1, js_el_query_one),
    JS_CFUNC_DEF("querySelectorAll", 1, js_el_query_all),
    JS_CFUNC_DEF("matches", 1, js_el_matches),
    JS_CFUNC_DEF("closest", 1, js_el_closest),
    JS_CFUNC_DEF("appendChild", 1, js_el_append),
    JS_CFUNC_DEF("remove", 0, js_el_remove),
    JS_CFUNC_DEF("click", 0, js_el_click),
    JS_CFUNC_DEF("focus", 0, js_el_focus),
    JS_CFUNC_DEF("blur", 0, js_el_blur),
    JS_CFUNC_DEF("submit", 0, js_el_form_submit),
    JS_CGETSET_DEF("tagName", js_el_tag_get, nullptr),
    JS_CGETSET_DEF("id", js_el_id_get, nullptr),
    JS_CGETSET_DEF("className", js_el_class_get, nullptr),
    JS_CGETSET_DEF("name", js_el_name_get, nullptr),
    JS_CGETSET_DEF("type", js_el_type_get, nullptr),
    JS_CGETSET_DEF("innerHTML", js_el_html_get, js_el_html_set),
    JS_CGETSET_DEF("textContent", js_el_text_get, js_el_text_set),
    JS_CGETSET_DEF("innerText", js_el_text_get, js_el_text_set),
    JS_CGETSET_DEF("value", js_el_value_get, js_el_value_set),
    JS_CGETSET_DEF("checked", js_el_checked_get, js_el_checked_set),
    JS_CGETSET_DEF("selectedIndex", js_el_selidx_get, js_el_selidx_set),
    JS_CGETSET_DEF("disabled", js_el_disabled_get, nullptr),
    JS_CGETSET_DEF("style", js_el_style_get, nullptr),
    JS_CGETSET_DEF("classList", js_el_cls_get, nullptr),
    JS_CGETSET_DEF("parentNode", js_el_parent, nullptr),
    JS_CGETSET_DEF("children", js_el_children, nullptr),
    JS_CGETSET_DEF("firstChild", js_el_first, nullptr),
    JS_CGETSET_DEF("nextSibling", js_el_next, nullptr),
    JS_CGETSET_DEF("previousSibling", js_el_prev, nullptr),
    JS_CGETSET_DEF("form", js_el_form_get, nullptr),
    JS_CGETSET_DEF("options", js_el_options, nullptr),
    JS_CGETSET_DEF("elements", js_el_form_elements, nullptr),
};

static const JSCFunctionListEntry *JsEngine_el_proto()
{
    return el_proto_table;
}

static int JsEngine_el_proto_count()
{
    return (int)(sizeof(el_proto_table) / sizeof(el_proto_table[0]));
}

const JSCFunctionListEntry *JsEngine::el_proto_funcs()
{
    return JsEngine_el_proto();
}

int JsEngine::el_proto_count()
{
    return JsEngine_el_proto_count();
}

static JSValue js_make_event(JSContext *ctx, JsEngine *eng, const char *type,
                             const litehtml::element::ptr &target)
{
    JSValue o = JS_NewObjectClass(ctx, event_class_id);
    if (JS_IsException(o))
        return o;
    JsEvent *e = new (std::nothrow) JsEvent();
    if (!e) {
        JS_FreeValue(ctx, o);
        return JS_EXCEPTION;
    }
    e->type = type ? type : "";
    e->target = target;
    JS_SetOpaque(o, e);
    JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, e->type.c_str()));
    JSValue tw = eng->wrap_element(target);
    JS_SetPropertyStr(ctx, o, "target", tw);
    static const JSCFunctionListEntry defs[] = {
        JS_CFUNC_DEF("preventDefault", 0, js_event_prevent),
        JS_CGETSET_DEF("defaultPrevented", js_event_prevented_get, nullptr),
    };
    JS_SetPropertyFunctionList(ctx, o, defs, 2);
    return o;
}

static JSValue js_event_prevent(JSContext *ctx, JSValueConst t, int, JSValueConst *)
{
    JsEvent *e = (JsEvent *)JS_GetOpaque2(ctx, t, event_class_id);
    if (e)
        e->prevented = true;
    return JS_UNDEFINED;
}

static JSValue js_event_prevented_get(JSContext *ctx, JSValueConst t)
{
    JsEvent *e = (JsEvent *)JS_GetOpaque2(ctx, t, event_class_id);
    return JS_NewBool(ctx, (e && e->prevented) ? 1 : 0);
}

static bool js_fire(JSContext *ctx, JsEngine *eng, const litehtml::element::ptr &target,
                    const char *type)
{
    bool prevented = false;
    std::vector<std::pair<JSValue, JSValue>> calls;
    auto el = target;
    while (el) {
        auto it = eng->listeners.find(el.get());
        if (it != eng->listeners.end()) {
            auto jt = it->second.find(type);
            if (jt != it->second.end()) {
                for (JSValue f : jt->second) {
                    JSValue thisv = eng->wrap_element(el);
                    calls.emplace_back(f, thisv);
                }
            }
        }

        JSValue w = eng->wrap_element(el);
        std::string prop = std::string("on") + type;
        JSValue fn = JS_GetPropertyStr(ctx, w, prop.c_str());
        if (JS_IsFunction(ctx, fn))
            calls.emplace_back(JS_DupValue(ctx, fn), w);
        else
            JS_FreeValue(ctx, w);
        JS_FreeValue(ctx, fn);
        el = el->parent();
    }
    for (auto &pr : calls) {
        JSValue ev = js_make_event(ctx, eng, type, target);
        JSValue r = JS_Call(ctx, pr.first, pr.second, 1, &ev);
        JsEvent *e = (JsEvent *)JS_GetOpaque2(ctx, ev, event_class_id);
        if (e && e->prevented)
            prevented = true;
        if (JS_IsException(r))
            eng->log_exception("event");
        else
            JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, ev);
        JS_FreeValue(ctx, pr.second);
    }
    return prevented;
}

static void js_fire_win(JSContext *ctx, JsEngine *eng, const char *type)
{
    auto it = eng->win_events.find(type);
    if (it == eng->win_events.end())
        return;
    for (JSValue f : it->second) {
        JSValue ev = js_make_event(ctx, eng, type, nullptr);
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue r = JS_Call(ctx, f, global, 1, &ev);
        JS_FreeValue(ctx, global);
        if (JS_IsException(r))
            eng->log_exception("window event");
        else
            JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, ev);
    }
}

static void js_fire_doc(JSContext *ctx, JsEngine *eng, const char *type)
{
    auto it = eng->doc_events.find(type);
    if (it == eng->doc_events.end())
        return;
    for (JSValue f : it->second) {
        JSValue ev = js_make_event(ctx, eng, type, nullptr);
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue r = JS_Call(ctx, f, global, 1, &ev);
        JS_FreeValue(ctx, global);
        if (JS_IsException(r))
            eng->log_exception("document event");
        else
            JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, ev);
    }
}

static void js_click_element(AromaWebPage *page, const litehtml::element::ptr &el)
{
    if (!page || !el)
        return;
#ifdef AROMA_WEBVIEW_JS
    if (page->js && page->js->ctx) {
        JsEngine *eng = page->js;
        bool prevented = js_fire(eng->ctx, eng, el, "click");
        if (prevented)
            return;
    }
#endif
    if (auto *ctl = dynamic_cast<AromaControl *>(el.get())) {
        ctl->activate(page);
        return;
    }
    const char *tag = el->get_tagName();
    if (tag && !strcasecmp(tag, "a")) {
        const char *href = el->get_attr("href");
        if (href && href[0] && href[0] != '#')
            page->cont->pending_nav = page->cont->absolute_url(href, nullptr);
    }
}

static void js_collect_scripts(const litehtml::element::ptr &el,
                               std::vector<litehtml::element::ptr> &out)
{
    if (!el)
        return;
    for (const auto &ch : el->children()) {
        if (ch->is_text())
            continue;
        const char *tag = ch->get_tagName();
        if (tag && !strcasecmp(tag, "script"))
            out.push_back(ch);
        js_collect_scripts(ch, out);
    }
}

static void js_run_page_scripts(AromaWebPage *page)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page->js || !page->js->ctx || !page->doc)
        return;
    JsEngine *eng = page->js;
    std::vector<litehtml::element::ptr> scripts;
    js_collect_scripts(page->doc->root(), scripts);
    int n = 0;
    for (const auto &s : scripts) {
        const char *src = s->get_attr("src");
        std::string code;
        std::string name = "<inline>";
        if (src && src[0]) {
            std::string abs = page->cont->absolute_url(src, nullptr);
            code = fetch_jar_text(&page->cookies, abs);
            name = abs;
            if (code.empty()) {
                LOG_INFO("aroma_webview: script not loaded: %s", abs.c_str());
                continue;
            }
        } else {
            code = element_text(s);
        }
        char fname[64];
        snprintf(fname, sizeof(fname), "<script:%d>", ++n);
        if (!src || !src[0])
            name = fname;
        eng->eval(code.c_str(), name.c_str(), nullptr);
    }
#else
    (void)page;
#endif
}

static void js_fire_load_events(AromaWebPage *page)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page->js || !page->js->ctx)
        return;
    JsEngine *eng = page->js;
    js_fire_doc(eng->ctx, eng, "DOMContentLoaded");
    js_fire_win(eng->ctx, eng, "load");
    JSValue global = JS_GetGlobalObject(eng->ctx);
    JSValue fn = JS_GetPropertyStr(eng->ctx, global, "onload");
    if (JS_IsFunction(eng->ctx, fn)) {
        JSValue r = JS_Call(eng->ctx, fn, global, 0, nullptr);
        if (JS_IsException(r))
            eng->log_exception("onload");
        else
            JS_FreeValue(eng->ctx, r);
    }
    JS_FreeValue(eng->ctx, fn);
    JS_FreeValue(eng->ctx, global);
#else
    (void)page;
#endif
}

static bool js_dispatch_click(AromaWebPage *page, const litehtml::element::ptr &el)
{
    if (!page || !el)
        return false;
    size_t before = page->nav_gen;
    js_click_element(page, el);
    return page->nav_gen != before;
}

static void js_dispatch_event(AromaWebPage *page, const litehtml::element::ptr &el,
                              const char *type)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page->js || !page->js->ctx || !el || !type)
        return;
    js_fire(page->js->ctx, page->js, el, type);
#else
    (void)page;
    (void)el;
    (void)type;
#endif
}

static bool js_run_timers(AromaWebPage *page)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page->js)
        return false;
    size_t before = page->nav_gen;
    bool fired = page->js->pump_timers();
    page_process_pending(page);
    if (page->nav_gen != before)
        page->dom_dirty = true;
    return fired;
#else
    (void)page;
    return false;
#endif
}

static bool page_process_pending(AromaWebPage *page)
{
    if (!page)
        return false;
    if (!page->cont->pending_nav.empty()) {
        std::string target = page->cont->pending_nav;
        page->cont->pending_nav.clear();
        return page_navigate(page, target);
    }
    if (page->pending_reload) {
        page->pending_reload = false;
        if (!page->source_html.empty()) {
            page_build(page, page->source_html, page->source_base);
            page->dom_dirty = true;
            page->nav_gen++;
            return true;
        }
    }
    return false;
}

static bool page_navigate(AromaWebPage *page, const std::string &url)
{
    if (!page || url.empty())
        return false;
    std::string text = fetch_jar_text(&page->cookies, url);
    if (text.empty())
        return false;
    if (!page_build(page, text, url))
        return false;
    page->url = url;
    page_push_history(page, url, page->source_html, url);
    page->nav_gen++;
    page->dom_dirty = true;
    return true;
}

static void page_relayout(AromaWebPage *page)
{
    if (!page || !page->doc)
        return;
    if (page->in_doc_call) {

        page->layout_pending = true;
        page->dom_dirty = true;
        return;
    }
    page->layout_pending = false;
    page->doc->render(page->view_w);
    page->content_w = px(page->doc->width());
    page->content_h = px(page->doc->height());
    if (page->content_w < 0)
        page->content_w = 0;
    if (page->content_h < 0)
        page->content_h = 0;
    page->dom_dirty = true;
}

static void page_submit_form(AromaWebPage *page, AromaControl *from,
                             const litehtml::element::ptr &form_in)
{
    if (!page)
        return;
    litehtml::element::ptr form = form_in;
    if (!form && from) {

        for (const auto &reg : page->cont->controls) {
            if (reg.get() == (litehtml::element *)from) {
                form = AromaFormHelper::find_form(reg);
                break;
            }
        }
    }
    if (!form)
        return;
#ifdef AROMA_WEBVIEW_JS
    if (page->js && page->js->ctx) {

        JsEngine *eng = page->js;
        JSValue w = eng->wrap_element(form);
        auto it = eng->listeners.find(form.get());
        bool prevented = false;
        if (it != eng->listeners.end()) {
            auto jt = it->second.find("submit");
            if (jt != it->second.end()) {
                for (JSValue f : jt->second) {
                    JSValue ev = js_make_event(eng->ctx, eng, "submit", form);
                    JSValue r = JS_Call(eng->ctx, f, w, 1, &ev);
                    JsEvent *e = (JsEvent *)JS_GetOpaque2(eng->ctx, ev, event_class_id);
                    if (e && e->prevented)
                        prevented = true;
                    if (JS_IsException(r))
                        eng->log_exception("submit");
                    else
                        JS_FreeValue(eng->ctx, r);
                    JS_FreeValue(eng->ctx, ev);
                }
            }
        }
        JS_FreeValue(eng->ctx, w);
        if (prevented)
            return;
    }
#endif
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<AromaControl *> ctls;
    AromaFormHelper::collect_controls(form, ctls);
    std::string clicked_name, clicked_value;
    if (from && (from->kind == AromaControl::Submit ||
                 (from->kind == AromaControl::Button && from->ctl_type == "submit"))) {
        clicked_name = from->ctl_name;
        const char *v = from->get_attr("value");
        clicked_value = v ? v : "";
    }
    for (auto *c : ctls) {
        if (c->disabled || c->ctl_name.empty())
            continue;
        if (c->kind == AromaControl::Checkbox || c->kind == AromaControl::Radio) {
            if (!c->checked)
                continue;
            const char *v = c->get_attr("value");
            pairs.emplace_back(c->ctl_name, v && v[0] ? v : "on");
        } else if (c->is_select) {
            if (c->sel_index >= 0 && c->sel_index < (int)c->options.size())
                pairs.emplace_back(c->ctl_name, c->options[(size_t)c->sel_index].value);
        } else if (c->kind == AromaControl::Submit || c->kind == AromaControl::Button) {
            if (c == from && !clicked_name.empty())
                pairs.emplace_back(clicked_name, clicked_value);
        } else {
            pairs.emplace_back(c->ctl_name, c->ctl_value);
        }
    }
    std::string action = form->get_attr("action") ? form->get_attr("action") : "";
    std::string target;
    if (action.empty())
        target = page->url.empty() ? page->source_base : page->url;
    else
        target = page->cont->absolute_url(action.c_str(), nullptr);

    size_t hash = target.find('#');
    if (hash != std::string::npos)
        target.erase(hash);
    std::string query;
    for (size_t i = 0; i < pairs.size(); i++) {
        if (i)
            query += '&';
        query += url_encode(pairs[i].first) + "=" + url_encode(pairs[i].second);
    }
    if (!query.empty())
        target += (target.find('?') == std::string::npos ? "?" : "&") + query;
    page->cont->pending_nav = target;
}

static int dbg_build_no = 0;
litehtml::element::ptr AromaContainer::create_element(
    const char *tag_name, const litehtml::string_map &attributes,
    const std::shared_ptr<litehtml::document> &doc)
{
    (void)attributes;
    if (!tag_name)
        return nullptr;
    if (!strcasecmp(tag_name, "script")) {
        auto el = litehtml::element::ptr(new (std::nothrow) AromaScript(doc));
        return el;
    }
    AromaControl *raw = nullptr;
    if (!strcasecmp(tag_name, "input"))
        raw = new (std::nothrow) AromaControl(doc, tag_name, false, false);
    else if (!strcasecmp(tag_name, "textarea"))
        raw = new (std::nothrow) AromaControl(doc, tag_name, true, false);
    else if (!strcasecmp(tag_name, "select"))
        raw = new (std::nothrow) AromaControl(doc, tag_name, false, true);
    else if (!strcasecmp(tag_name, "button"))
        raw = new (std::nothrow) AromaControl(doc, tag_name, false, false);
    if (!raw)
        return nullptr;
    litehtml::element::ptr el(raw);
    controls.push_back(el);
    return el;
}

void AromaContainer::on_anchor_click(const char *url, const litehtml::element::ptr &el)
{
    if (!url || !url[0] || url[0] == '#')
        return;
#ifdef AROMA_WEBVIEW_JS
    if (owner && owner->js && owner->js->ctx) {
        if (js_fire(owner->js->ctx, owner->js, el, "click"))
            return;
    }
#endif
    pending_nav = absolute_url(url, nullptr);
}

void AromaControl::activate(AromaWebPage *page)
{
    if (!page || disabled)
        return;
    auto *cont = page->cont;
    switch (kind) {
    case Checkbox:
        checked = !checked;
        dirty = true;
        js_dispatch_event(page, shared_from_this(), "input");
        js_dispatch_event(page, shared_from_this(), "change");
        page->dom_dirty = true;
        break;
    case Radio:
        if (!checked) {
            auto form = AromaFormHelper::find_form(shared_from_this());
            std::vector<AromaControl *> group;
            if (form)
                AromaFormHelper::collect_controls(form, group);
            else if (page->doc) {

                std::vector<litehtml::element::ptr> all;
                (void)all;
            }
            if (form) {
                for (auto *c : group) {
                    if (c != this && c->kind == Radio && c->ctl_name == ctl_name)
                        c->checked = false;
                }
            } else {
                for (const auto &reg : cont->controls) {
                    auto *c = dynamic_cast<AromaControl *>(reg.get());
                    if (c && c != this && c->kind == Radio && c->ctl_name == ctl_name)
                        c->checked = false;
                }
            }
            checked = true;
            dirty = true;
            js_dispatch_event(page, shared_from_this(), "input");
            js_dispatch_event(page, shared_from_this(), "change");
            page->dom_dirty = true;
        }
        break;
    case Submit:
        page_submit_form(page, this, nullptr);
        break;
    case Button:
        if (ctl_type == "submit") {
            page_submit_form(page, this, nullptr);
        } else if (ctl_type == "reset") {
            auto form = AromaFormHelper::find_form(shared_from_this());
            std::vector<AromaControl *> ctls;
            if (form)
                AromaFormHelper::collect_controls(form, ctls);
            for (auto *c : ctls) {
                const char *v = c->get_attr("value");
                c->ctl_value = v ? v : "";
                c->checked = c->get_attr("checked") != nullptr;
                if (c->is_select)
                    c->collect_options();
            }
            page->dom_dirty = true;
        }
        break;
    case Text:
    case Password:
    case File:
        cont->focused = this;
        cursor = (int)ctl_value.size();
        page->dom_dirty = true;
        break;
    default:
        break;
    }
    if (is_select) {
        if (!options.empty())
            sel_index = (sel_index + 1) % (int)options.size();
        dirty = true;
        js_dispatch_event(page, shared_from_this(), "input");
        js_dispatch_event(page, shared_from_this(), "change");
        page->dom_dirty = true;
    }
    if (is_textarea) {
        cont->focused = this;
        cursor = (int)ctl_value.size();
        page->dom_dirty = true;
    }
}

void AromaControl::on_click()
{
    auto doc = get_document();
    if (!doc)
        return;
    auto *cont = static_cast<AromaContainer *>(doc->container());
    if (cont && cont->owner)
        js_click_element(cont->owner, shared_from_this());
    else
        html_tag::on_click();
}

static void js_engine_reset(AromaWebPage *page)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page)
        return;
    js_engine_free(page);
    page->js = new (std::nothrow) JsEngine(page);
    if (page->js && !page->js->reset()) {
        delete page->js;
        page->js = nullptr;
    }
    if (page->js)
        js_register_aux_classes(page->js->ctx, page->js->rt);
#else
    (void)page;
#endif
}

static void js_engine_free(AromaWebPage *page)
{
#ifdef AROMA_WEBVIEW_JS
    if (!page)
        return;
    delete page->js;
    page->js = nullptr;
#else
    (void)page;
#endif
}

static std::string js_string(JSContext *ctx, JSValueConst v)
{
    const char *s = JS_ToCString(ctx, v);
    std::string out = s ? s : "";
    if (s)
        JS_FreeCString(ctx, s);
    return out;
}

static int js_int(JSContext *ctx, JSValueConst v, int def)
{
    int32_t i = def;
    JS_ToInt32(ctx, &i, v);
    return (int)i;
}

static JSValue js_console_log(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    std::string out;
    for (int i = 0; i < argc; i++) {
        if (i)
            out += ' ';
        out += js_string(ctx, argv[i]);
    }
    LOG_INFO("aroma_webview: console: %s", out.c_str());
    return JS_UNDEFINED;
}

static JSValue js_set_timer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                            bool repeat)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]) || eng->timers.size() > 256)
        return JS_NewInt32(ctx, 0);
    int64_t ms = 0;
    if (argc > 1)
        JS_ToInt64(ctx, &ms, argv[1]);
    if (ms < 0)
        ms = 0;
    JsEngine::Timer t;
    t.id = eng->next_timer_id++;
    t.repeat = repeat;
    t.interval = ms;
    t.next = js_now_ms() + ms;
    t.func = JS_DupValue(ctx, argv[0]);
    eng->timers.push_back(t);
    return JS_NewInt32(ctx, t.id);
}

static JSValue js_set_timeout(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_set_timer(ctx, t, argc, argv, false);
}

static JSValue js_set_interval(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
    return js_set_timer(ctx, t, argc, argv, true);
}

static JSValue js_clear_timer(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1)
        return JS_UNDEFINED;
    int id = js_int(ctx, argv[0]);
    for (size_t i = 0; i < eng->timers.size(); i++) {
        if (eng->timers[i].id == id) {
            JS_FreeValue(ctx, eng->timers[i].func);
            eng->timers.erase(eng->timers.begin() + i);
            break;
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_alert(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    if (argc > 0)
        LOG_INFO("aroma_webview: alert: %s", js_string(ctx, argv[0]).c_str());
    return JS_UNDEFINED;
}

static JSValue js_storage_get(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 1)
        return JS_NULL;
    auto it = eng->page->storage.find(js_string(ctx, argv[0]));
    if (it == eng->page->storage.end())
        return JS_NULL;
    return JS_NewString(ctx, it->second.c_str());
}

static JSValue js_storage_set(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc < 2)
        return JS_UNDEFINED;
    eng->page->storage[js_string(ctx, argv[0])] = js_string(ctx, argv[1]);
    return JS_UNDEFINED;
}

static JSValue js_storage_remove(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    if (argc > 0)
        eng->page->storage.erase(js_string(ctx, argv[0]));
    return JS_UNDEFINED;
}

static JSValue js_storage_clear(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    js_engine(ctx)->page->storage.clear();
    return JS_UNDEFINED;
}

static JSValue js_storage_key(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
{
    JsEngine *eng = js_engine(ctx);
    int idx = argc > 0 ? js_int(ctx, argv[0]) : 0;
    int i = 0;
    for (const auto &kv : eng->page->storage) {
        if (i++ == idx)
            return JS_NewString(ctx, kv.first.c_str());
    }
    return JS_NULL;
}

static JSValue js_storage_length(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    return JS_NewInt32(ctx, (int)js_engine(ctx)->page->storage.size());
}

static JSValue js_make_storage(JSContext *ctx)
{
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "getItem", JS_NewCFunction(ctx, js_storage_get, "getItem", 1));
    JS_SetPropertyStr(ctx, o, "setItem", JS_NewCFunction(ctx, js_storage_set, "setItem", 2));
    JS_SetPropertyStr(ctx, o, "removeItem",
                      JS_NewCFunction(ctx, js_storage_remove, "removeItem", 1));
    JS_SetPropertyStr(ctx, o, "clear", JS_NewCFunction(ctx, js_storage_clear, "clear", 0));
    JS_SetPropertyStr(ctx, o, "key", JS_NewCFunction(ctx, js_storage_key, "key", 1));
    JS_SetPropertyStr(ctx, o, "length", JS_NewCFunction(ctx, js_storage_length, "length", 0));
    return o;
}

static JSValue js_location_href_get(JSContext *ctx, JSValueConst)
{
    return JS_NewString(ctx, js_engine(ctx)->page->url.c_str());
}

static JSValue js_location_href_set(JSContext *ctx, JSValueConst, JSValueConst val)
{
    JsEngine *eng = js_engine(ctx);
    std::string url = js_string(ctx, val);
    if (!url.empty())
        eng->page->cont->pending_nav = eng->page->cont->absolute_url(url.c_str(), nullptr);
    return JS_UNDEFINED;
}

static JSValue js_location_reload(JSContext *ctx, JSValueConst, int, JSValueConst *)
{
    JsEngine *eng = js_engine(ctx);
    if (!eng->page->source_html.empty())
        page_build(eng->page, eng->page->source_html, eng->page->source_base);
    eng->page->dom_dirty = true;
    return JS_UNDEFINED;
}

static JSValue js_location_tostring(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
{
    (void)this_val;
    return js_location_href_get(ctx, this_val);
}

static JSValue js_make_location(JSContext *ctx)
{
    JSValue o = JS_NewObject(ctx);
    static const JSCFunctionListEntry defs[] = {
        JS_CGETSET_DEF("href", js_location_href_get, js_location_href_set),
        JS_CFUNC_DEF("reload", 0, js_location_reload),
        JS_CFUNC_DEF("toString", 0, js_location_tostring),
    };
    JS_SetPropertyFunctionList(ctx, o, defs, 3);
    return o;
}

static JSValue js_bridge_call(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
                              int magic)
{
    JsEngine *eng = js_engine(ctx);
    if (magic < 0 || magic >= (int)eng->page->bridges.size())
        return JS_UNDEFINED;
    const AromaWebPage::Bridge &b = eng->page->bridges[(size_t)magic];
    std::string arg = argc > 0 ? js_string(ctx, argv[0]) : "";
    char out[4096];
    out[0] = '\0';
    typedef void (*BridgeFn)(const char *, char *, size_t, void *);
    ((BridgeFn)b.fn)(arg.c_str(), out, sizeof(out), b.user);
    return JS_NewString(ctx, out);
}

void JsEngine::install_bridges()
{
    JSValue global = JS_GetGlobalObject(ctx);
    for (size_t i = 0; i < page->bridges.size(); i++) {
        JSValue fn = JS_NewCFunctionMagic(ctx, js_bridge_call, page->bridges[i].name.c_str(),
                                          1, JS_CFUNC_generic_magic, (int)i);
        JS_SetPropertyStr(ctx, global, page->bridges[i].name.c_str(), fn);
    }
    JS_FreeValue(ctx, global);
}

#endif

#endif

extern "C" {

AromaWebPage *aroma_webpage_create(int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    if (width > 4096)
        width = 4096;
    if (height > 4096)
        height = 4096;
    AromaWebPage *page = new (std::nothrow) AromaWebPage();
    if (!page)
        return nullptr;
    page->view_w = width;
    page->view_h = height;
    page->cont = new (std::nothrow) AromaContainer(width, height);
    if (!page->cont) {
        delete page;
        return nullptr;
    }
    page->cont->owner = page;
    if (!font_acquire()) {
        delete page->cont;
        delete page;
        return nullptr;
    }
    page->fonts_on = true;
    page->buf.assign((size_t)width * (size_t)height * 4u, 255);
    return page;
}

void aroma_webpage_destroy(AromaWebPage *page)
{
    if (!page)
        return;
    js_engine_free(page);
    page_clear_doc(page);
    delete page->cont;
    if (page->fonts_on)
        font_release();
    delete page;
}

void aroma_webpage_resize(AromaWebPage *page, int width, int height)
{
    if (!page || width <= 0 || height <= 0)
        return;
    if (width > 4096)
        width = 4096;
    if (height > 4096)
        height = 4096;
    if (width == page->view_w && height == page->view_h)
        return;
    page->view_w = width;
    page->view_h = height;
    page->cont->view_w = width;
    page->cont->view_h = height;
    page->buf.assign((size_t)width * (size_t)height * 4u, 255);
    if (page->doc) {
        page->doc->render(width);
        page->content_w = px(page->doc->width());
        page->content_h = px(page->doc->height());
    }
}

bool aroma_webpage_load_html(AromaWebPage *page, const char *html, const char *base_url)
{
    if (!page || !html)
        return false;
    std::string base = base_url ? base_url : "";
    if (!page_build(page, html, base))
        return false;
    page->url.clear();
    page_push_history(page, "", page->source_html, base);
    return true;
}

bool aroma_webpage_load_url(AromaWebPage *page, const char *url)
{
    if (!page || !url || !url[0])
        return false;
    std::string text = fetch_jar_text(&page->cookies, url);
    if (text.empty())
        return false;
    if (!page_build(page, text, url))
        return false;
    page->url = url;
    page_push_history(page, url, page->source_html, url);
    return true;
}

const unsigned char *aroma_webpage_render(AromaWebPage *page, int scroll_x, int scroll_y,
                                          int *out_w, int *out_h)
{
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (!page || !page->doc)
        return nullptr;
    if (scroll_x < 0)
        scroll_x = 0;
    if (scroll_y < 0)
        scroll_y = 0;
    size_t need = (size_t)page->view_w * (size_t)page->view_h * 4u;
    if (page->buf.size() != need)
        page->buf.assign(need, 255);
    else
        std::fill(page->buf.begin(), page->buf.end(), 255);
    page->cont->buf = &page->buf;
    page->cont->clip_stack.clear();
    litehtml::position clip(0, 0, page->view_w, page->view_h);
    try {
        page->doc->draw((litehtml::uint_ptr)page->cont, -scroll_x, -scroll_y, &clip);
    } catch (...) {
        page->cont->buf = nullptr;
        return nullptr;
    }
    page->cont->buf = nullptr;
    if (out_w)
        *out_w = page->view_w;
    if (out_h)
        *out_h = page->view_h;
    return page->buf.data();
}

const unsigned char *aroma_webpage_pixels(AromaWebPage *page, int *w, int *h)
{
    if (w)
        *w = 0;
    if (h)
        *h = 0;
    if (!page || page->buf.empty())
        return nullptr;
    if (w)
        *w = page->view_w;
    if (h)
        *h = page->view_h;
    return page->buf.data();
}

void aroma_webpage_content_size(AromaWebPage *page, int *w, int *h)
{
    if (w)
        *w = 0;
    if (h)
        *h = 0;
    if (!page)
        return;
    if (w)
        *w = page->content_w;
    if (h)
        *h = page->content_h;
}

bool aroma_webpage_click(AromaWebPage *page, int x, int y)
{
    if (!page || !page->doc)
        return false;
    auto noop = [](const litehtml::position &) {};
    page->in_doc_call = true;
    try {
        page->doc->on_lbutton_down(x, y, x, y, noop);
        page->doc->on_lbutton_up(x, y, x, y, noop);
    } catch (...) {
        page->in_doc_call = false;
        return false;
    }
    page->in_doc_call = false;
    if (page->layout_pending)
        page_relayout(page);
    if (page->cont->pending_nav.empty())
        return false;
    std::string target = page->cont->pending_nav;
    page->cont->pending_nav.clear();
    if (aroma_webpage_load_url(page, target.c_str())) {
        page->url = target;
        return true;
    }
    return false;
}

const char *aroma_webpage_url(AromaWebPage *page)
{
    if (!page)
        return nullptr;
    return page->url.c_str();
}

const char *aroma_webpage_title(AromaWebPage *page)
{
    if (!page)
        return nullptr;
    return page->cont->title.c_str();
}

bool aroma_webpage_can_go_back(AromaWebPage *page)
{
    return page && page->hindex > 0;
}

bool aroma_webpage_can_go_forward(AromaWebPage *page)
{
    return page && page->hindex + 1 < (int)page->hist.size();
}

static bool page_restore(AromaWebPage *page, int idx)
{
    if (!page || idx < 0 || idx >= (int)page->hist.size())
        return false;
    const AromaWebPage::Hist &h = page->hist[(size_t)idx];
    if (!page_build(page, h.html, h.base))
        return false;
    page->url = h.url;
    page->hindex = idx;
    return true;
}

bool aroma_webpage_go_back(AromaWebPage *page)
{
    if (!aroma_webpage_can_go_back(page))
        return false;
    return page_restore(page, page->hindex - 1);
}

bool aroma_webpage_go_forward(AromaWebPage *page)
{
    if (!aroma_webpage_can_go_forward(page))
        return false;
    return page_restore(page, page->hindex + 1);
}

void aroma_webpage_set_text_scale(AromaWebPage *page, float scale)
{
    if (!page)
        return;
    if (!(scale > 0.0f))
        scale = 1.0f;
    if (scale < 0.25f)
        scale = 0.25f;
    if (scale > 4.0f)
        scale = 4.0f;
    if (scale == page->cont->text_scale)
        return;
    page->cont->text_scale = scale;
    if (!page->source_html.empty())
        page_build(page, page->source_html, page->source_base);
}

float aroma_webpage_text_scale(AromaWebPage *page)
{
    if (!page)
        return 1.0f;
    return page->cont->text_scale;
}

static size_t utf8_prev(const std::string &s, size_t pos)
{
    if (pos == 0 || pos > s.size())
        return 0;
    size_t p = pos - 1;
    while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80)
        p--;
    return p;
}

static size_t utf8_next_pos(const std::string &s, size_t pos)
{
    if (pos >= s.size())
        return s.size();
    size_t p = pos + 1;
    while (p < s.size() && ((unsigned char)s[p] & 0xC0) == 0x80)
        p++;
    return p;
}

static size_t utf8_count(const std::string &s)
{
    size_t n = 0;
    size_t p = 0;
    while (p < s.size()) {
        p = utf8_next_pos(s, p);
        n++;
    }
    return n;
}

static bool page_key(AromaWebPage *page, unsigned key)
{
    if (!page)
        return false;
    AromaControl *ctl = page->cont->focused;
    if (!ctl || ctl->disabled)
        return false;
    bool changed = false;
    bool consumed = false;
    if (ctl->is_select) {
        if (key == 0xFF52 || key == 0xFF51 || key == 32) {
            if (!ctl->options.empty()) {
                ctl->sel_index = (ctl->sel_index - 1 + (int)ctl->options.size()) %
                                 (int)ctl->options.size();
                changed = true;
            }
            consumed = true;
        } else if (key == 0xFF54 || key == 0xFF53 || key == 13 || key == 10) {
            if (key == 13 || key == 10) {
                page->cont->focused = nullptr;
            } else if (!ctl->options.empty()) {
                ctl->sel_index = (ctl->sel_index + 1) % (int)ctl->options.size();
                changed = true;
            }
            consumed = true;
        }
        if (changed) {
            ctl->dirty = true;
            js_dispatch_event(page, ctl->shared_from_this(), "input");
            js_dispatch_event(page, ctl->shared_from_this(), "change");
            page->dom_dirty = true;
        }
        return consumed;
    }
    if (ctl->kind != AromaControl::Text && ctl->kind != AromaControl::Password &&
        !ctl->is_textarea)
        return false;
    if (ctl->readonly && key != 13 && key != 10 && key != 0xFF0D)
        return key == 13 || key == 10 || key == 0xFF0D;
    std::string &v = ctl->ctl_value;
    size_t cur = (size_t)(ctl->cursor < 0 ? 0 : ctl->cursor);
    if (cur > v.size())
        cur = v.size();
    if (key == 8 || key == 127 || key == 0xFF08) {
        if (cur > 0) {
            size_t p = utf8_prev(v, cur);
            v.erase(p, cur - p);
            ctl->cursor = (int)p;
            changed = true;
        }
        consumed = true;
    } else if (key == 0xFF51 || key == 0xFF52 || key == 0x0025) {
        ctl->cursor = (int)utf8_prev(v, cur);
        consumed = true;
    } else if (key == 0xFF53 || key == 0xFF54 || key == 0x0027) {
        ctl->cursor = (int)utf8_next_pos(v, cur);
        consumed = true;
    } else if (key == 0xFF50 || key == 0x0024) {
        ctl->cursor = 0;
        consumed = true;
    } else if (key == 0xFF57 || key == 0x0023) {
        ctl->cursor = (int)v.size();
        consumed = true;
    } else if (key == 13 || key == 10 || key == 0xFF0D) {
        if (ctl->is_textarea) {
            v.insert(cur, 1, '\n');
            ctl->cursor = (int)(cur + 1);
            changed = true;
        } else {
            page->cont->focused = nullptr;
            page_submit_form(page, ctl, nullptr);
            page->dom_dirty = true;
        }
        consumed = true;
    } else if (key >= 32 && key <= 126) {
        if (ctl->maxlen < 0 || (int)utf8_count(v) < ctl->maxlen) {
            v.insert(cur, 1, (char)key);
            ctl->cursor = (int)(cur + 1);
            changed = true;
        }
        consumed = true;
    }
    if (changed) {
        ctl->dirty = true;
        js_dispatch_event(page, ctl->shared_from_this(), "input");
        page->dom_dirty = true;
    }
    return consumed;
}

const char *aroma_webpage_eval(AromaWebPage *page, const char *code)
{
    if (!page)
        return "";
    page->last_js_result.clear();
#ifdef AROMA_WEBVIEW_JS
    if (!page->js || !page->js->ctx || !code)
        return "";
    std::string result;
    if (!page->js->eval(code, "<eval>", &result))
        return "";
    page->last_js_result = result;
    page_process_pending(page);
#endif
    return page->last_js_result.c_str();
}

bool aroma_webpage_add_bridge(AromaWebPage *page, const char *name,
                              AromaWebPageBridgeFn fn, void *user)
{
    if (!page || !name || !name[0] || !fn)
        return false;
#ifdef AROMA_WEBVIEW_JS
    for (auto &b : page->bridges) {
        if (b.name == name) {
            b.fn = (void *)fn;
            b.user = user;
            if (page->js && page->js->ctx)
                page->js->install_bridges();
            return true;
        }
    }
    if (page->bridges.size() >= 16)
        return false;
    AromaWebPage::Bridge b;
    b.name = name;
    b.fn = (void *)fn;
    b.user = user;
    page->bridges.push_back(std::move(b));
    if (page->js && page->js->ctx)
        page->js->install_bridges();
    return true;
#else
    (void)fn;
    (void)user;
    return false;
#endif
}

bool aroma_webpage_run_timers(AromaWebPage *page)
{
    if (!page)
        return false;
    return js_run_timers(page);
}

bool aroma_webpage_key(AromaWebPage *page, unsigned key_code)
{
    if (!page)
        return false;
    bool consumed = page_key(page, (unsigned)key_code);
    if (consumed)
        page_process_pending(page);
    return consumed;
}

bool aroma_webpage_dom_dirty(AromaWebPage *page)
{
    if (!page)
        return false;
    bool d = page->dom_dirty;
    page->dom_dirty = false;
    return d;
}

void aroma_webpage_set_view_scroll(AromaWebPage *page, int x, int y)
{
    if (!page)
        return;
    page->view_scroll_x = x;
    page->view_scroll_y = y;
}

unsigned aroma_webpage_nav_gen(AromaWebPage *page)
{
    return page ? page->nav_gen : 0;
}

}
