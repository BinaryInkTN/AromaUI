#include "widgets/aroma_video.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_native_utils.h"
#include "aroma_time.h"
#include "aroma_timer.h"
#include "aroma_font.h"
#include "widgets/aroma_video_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_VIDEO_PATH_MAX 1024
#define AROMA_VIDEO_TICK_MS 33

typedef struct AromaVideo {
    AromaRect rect;
    char path[AROMA_VIDEO_PATH_MAX];
    unsigned int texture_id;
    bool owns_texture;
    int video_w;
    int video_h;
    double duration;
    double framerate;
    double position;
    bool looping;
    bool muted;
    bool click_to_toggle;
    bool playing;
    bool finished_notified;
    bool show_controls;
    bool controls_visible;
    uint64_t controls_ms;
    uint32_t controls_timeout_ms;
    bool scrubbing;
    bool scrub_resume;
    bool mouse_down;
    bool press_in_button;
    AromaFont *font;
    AromaVideoState state;
    char error_msg[256];
    float corner_radius;
    AromaImageScaleMode scale_mode;
    AromaNode *node;
    AromaTimer *timer;
    uint64_t last_tick_ms;
    AromaVideoCodec *codec;
    bool has_frame;
    bool push_mode;
    AromaVideoFinishedCb on_finished;
    void *finished_ud;
    AromaVideoErrorCb on_error;
    void *error_ud;
    AromaVideoFrameCb on_frame;
    void *frame_ud;
    int active_pointer_id;
    bool events_registered;
} AromaVideo;

#ifdef __ANDROID__
static inline int video_dp(int dp) { return aroma_android_dp_to_px(dp); }
#else
static inline int video_dp(int dp) { return dp; }
#endif

static void video_set_error(AromaVideo *v, const char *msg) {
    if (!v)
        return;
    if (msg) {
        strncpy(v->error_msg, msg, sizeof(v->error_msg) - 1);
        v->error_msg[sizeof(v->error_msg) - 1] = '\0';
    } else {
        v->error_msg[0] = '\0';
    }
    v->state = AROMA_VIDEO_ERROR;
    v->playing = false;
    LOG_ERROR("aroma_video: %s", v->error_msg[0] ? v->error_msg : "unknown error");
    if (v->node && v->on_error && v->error_msg[0])
        v->on_error(v->node, v->error_msg, v->error_ud);
}

static void video_upload_rgba(AromaVideo *v, const unsigned char *rgba, int w, int h) {
    if (!v || !rgba || w <= 0 || h <= 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    if (v->texture_id == 0) {
        if (gfx->load_image_from_rgba) {
            v->texture_id = gfx->load_image_from_rgba((unsigned char *)rgba, w, h);
            v->owns_texture = (v->texture_id != 0);
        }
    } else {
        bool ok = false;
        if (gfx->update_image_rgba)
            ok = gfx->update_image_rgba(v->texture_id, (unsigned char *)rgba, w, h);
        if (!ok) {
            if (v->owns_texture && gfx->unload_image)
                gfx->unload_image(v->texture_id);
            v->texture_id = 0;
            v->owns_texture = false;
            if (gfx->load_image_from_rgba) {
                v->texture_id = gfx->load_image_from_rgba((unsigned char *)rgba, w, h);
                v->owns_texture = (v->texture_id != 0);
            }
        }
    }
    if (v->texture_id != 0) {
        v->video_w = w;
        v->video_h = h;
        v->has_frame = true;
    }
}

static void video_codec_frame_cb(const unsigned char *rgba, int w, int h,
                                  double pts, void *user) {
    AromaVideo *v = (AromaVideo *)user;
    if (!v || !rgba || w <= 0 || h <= 0)
        return;
    video_upload_rgba(v, rgba, w, h);
    v->position = pts;
    if (v->on_frame && v->node)
        v->on_frame(v->node, pts, v->frame_ud);
    if (v->node)
        aroma_node_invalidate(v->node);
}

static void video_on_finished(AromaVideo *v) {
    v->playing = false;
    v->state = AROMA_VIDEO_FINISHED;
    if (v->duration > 0.0)
        v->position = v->duration;
    if (v->node)
        aroma_node_invalidate(v->node);
    if (!v->finished_notified && v->on_finished && v->node) {
        v->finished_notified = true;
        v->on_finished(v->node, v->finished_ud);
    }
}

static void video_tick_cb(void *user_data) {
    AromaVideo *v = (AromaVideo *)user_data;
    if (!v)
        return;
    uint64_t now = aroma_time_now_ms();
    if (v->show_controls && v->controls_visible && v->playing && !v->scrubbing &&
        v->controls_timeout_ms > 0 && v->controls_ms > 0 &&
        now - v->controls_ms >= v->controls_timeout_ms) {
        v->controls_visible = false;
        if (v->node)
            aroma_node_invalidate(v->node);
    }
    if (!v->playing || !v->codec)
        return;
    if (v->state == AROMA_VIDEO_ERROR || v->state == AROMA_VIDEO_FINISHED)
        return;
    if (!v->node || aroma_node_is_hidden(v->node)) {
        v->last_tick_ms = now;
        return;
    }
    double dt = (double)(now - v->last_tick_ms) / 1000.0;
    v->last_tick_ms = now;
    if (dt < 0.0)
        dt = 0.0;
    if (dt > 0.5)
        dt = 0.5;
    if (dt <= 0.0)
        return;
    if (!aroma_video_codec_step(v->codec, dt)) {
        video_set_error(v, "video decode error");
        return;
    }
    double d = aroma_video_codec_duration(v->codec);
    if (d > 0.0 && d < 86400.0)
        v->duration = d;
    double fps = aroma_video_codec_framerate(v->codec);
    if (fps > 0.0 && fps < 1000.0)
        v->framerate = fps;
    if (aroma_video_codec_ended(v->codec))
        video_on_finished(v);
}

static bool video_open_file(AromaVideo *v, const char *path) {
    char resolved[AROMA_VIDEO_PATH_MAX];
    const char *load_path = aroma_resolve_asset_path(path, resolved, sizeof(resolved));
    if (!load_path || !load_path[0])
        load_path = path;
    char err[256] = { 0 };
    AromaVideoCodec *codec = aroma_video_codec_open_file(load_path, video_codec_frame_cb,
                                                         v, err, sizeof(err));
    if (!codec) {
        if (!aroma_video_codec_have_decoder()) {
            return true;
        }
        video_set_error(v, err[0] ? err : "unsupported video file");
        return false;
    }
    aroma_video_codec_set_loop(codec, v->looping);
    v->codec = codec;
    v->push_mode = false;
    double d = aroma_video_codec_duration(codec);
    if (d > 0.0 && d < 86400.0)
        v->duration = d;
    double fps = aroma_video_codec_framerate(codec);
    if (fps > 0.0 && fps < 1000.0)
        v->framerate = fps;
    LOG_INFO("aroma_video: decoding with %s backend", aroma_video_codec_backend(codec));
    return true;
}

static bool video_open_memory(AromaVideo *v, unsigned char *data, size_t len) {
    char err[256] = { 0 };
    AromaVideoCodec *codec = aroma_video_codec_open_memory(data, len, video_codec_frame_cb,
                                                           v, err, sizeof(err));
    if (!codec) {
        if (!aroma_video_codec_have_decoder()) {
            return true;
        }
        video_set_error(v, err[0] ? err : "unsupported video data");
        return false;
    }
    aroma_video_codec_set_loop(codec, v->looping);
    v->codec = codec;
    v->push_mode = false;
    double d = aroma_video_codec_duration(codec);
    if (d > 0.0 && d < 86400.0)
        v->duration = d;
    double fps = aroma_video_codec_framerate(codec);
    if (fps > 0.0 && fps < 1000.0)
        v->framerate = fps;
    LOG_INFO("aroma_video: decoding with %s backend", aroma_video_codec_backend(codec));
    return true;
}

static void video_close_decoder(AromaVideo *v) {
    if (!v)
        return;
    if (v->codec) {
        aroma_video_codec_close(v->codec);
        v->codec = NULL;
    }
}

static bool video_point_in_bounds(AromaVideo *v, int x, int y) {
    if (!v)
        return false;
    return (x >= v->rect.x && x <= (v->rect.x + v->rect.width) &&
            y >= v->rect.y && y <= (v->rect.y + v->rect.height));
}

static void video_touch_controls(AromaNode *node);
static void video_scrub_to(AromaNode *node, AromaVideo *v, int x);
static void video_scrub_begin(AromaNode *node, AromaVideo *v, int x);
static void video_scrub_end(AromaNode *node, AromaVideo *v);
static bool video_osd_point(AromaVideo *v, int x, int y, int *region);

static bool video_event_handler(AromaEvent *event, void *user_data) {
    AromaNode *node = (AromaNode *)user_data;
    if (!node || !node->node_widget_ptr || !event)
        return false;
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;

    int x = 0, y = 0;
    bool is_release = false;
    bool is_press = false;
    bool is_move = false;
    switch (event->event_type) {
    case EVENT_TYPE_MOUSE_CLICK:
        if (v->active_pointer_id != -1 || v->mouse_down)
            return false;
        x = event->data.mouse.x;
        y = event->data.mouse.y;
        if (!video_point_in_bounds(v, x, y))
            return false;
        v->mouse_down = true;
        is_press = true;
        break;
    case EVENT_TYPE_MOUSE_MOVE:
        if (!v->mouse_down || !v->scrubbing)
            return false;
        x = event->data.mouse.x;
        y = event->data.mouse.y;
        is_move = true;
        break;
    case EVENT_TYPE_MOUSE_RELEASE:
        if (v->active_pointer_id != -1)
            return false;
        x = event->data.mouse.x;
        y = event->data.mouse.y;
        v->mouse_down = false;
        is_release = true;
        break;
    case EVENT_TYPE_TOUCH_DOWN:
        if (v->active_pointer_id != -1)
            return false;
        x = event->data.touch.x;
        y = event->data.touch.y;
        if (!video_point_in_bounds(v, x, y))
            return false;
        v->active_pointer_id = event->data.touch.id;
        is_press = true;
        break;
    case EVENT_TYPE_TOUCH_UP:
        if (v->active_pointer_id != event->data.touch.id)
            return false;
        v->active_pointer_id = -1;
        x = event->data.touch.x;
        y = event->data.touch.y;
        is_release = true;
        break;
    case EVENT_TYPE_TOUCH_MOVE:
        if (v->active_pointer_id != event->data.touch.id)
            return false;
        if (!v->scrubbing)
            return true;
        x = event->data.touch.x;
        y = event->data.touch.y;
        is_move = true;
        break;
    default:
        return false;
    }

    if (is_move && v->scrubbing) {
        video_scrub_to(node, v, x);
        return true;
    }

    if (is_press) {
        int region = 0;
        if (video_osd_point(v, x, y, &region) && region == 2) {
            v->press_in_button = false;
            video_scrub_begin(node, v, x);
            return true;
        }
        v->press_in_button = (region == 1);
        video_touch_controls(node);
        return true;
    }

    if (is_release) {
        if (v->scrubbing) {
            video_scrub_end(node, v);
            return true;
        }
        int region = 0;
        bool on_controls = video_osd_point(v, x, y, &region);
        if (on_controls && region == 1 && v->press_in_button) {
            v->press_in_button = false;
            if (v->playing)
                aroma_video_pause(node);
            else
                aroma_video_play(node);
            video_touch_controls(node);
            return true;
        }
        v->press_in_button = false;
        if (on_controls) {
            video_touch_controls(node);
            return true;
        }
        if (video_point_in_bounds(v, x, y)) {
            if (v->click_to_toggle) {
                if (v->playing)
                    aroma_video_pause(node);
                else
                    aroma_video_play(node);
            }
            video_touch_controls(node);
            return true;
        }
        return false;
    }

    return video_point_in_bounds(v, x, y);
}

static void video_touch_controls(AromaNode *node) {
    if (!node || !node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;
    if (!v->show_controls)
        return;
    v->controls_visible = true;
    v->controls_ms = aroma_time_now_ms();
    aroma_node_invalidate(node);
}

typedef struct {
    int btn_cx, btn_cy, btn_r;
    int tx, ty, tw;
    int cur_x, dur_x, label_y;
    bool has_labels;
} VideoOsd;

static void video_fmt_time(char *buf, size_t n, double seconds) {
    if (!buf || n == 0)
        return;
    if (!(seconds >= 0.0))
        seconds = 0.0;
    long s = (long)seconds;
    if (s >= 3600)
        snprintf(buf, n, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    else
        snprintf(buf, n, "%ld:%02ld", s / 60, s % 60);
}

static bool video_osd_geom(AromaVideo *v, VideoOsd *o) {
    if (!v || !o)
        return false;
    int rx = v->rect.x, ry = v->rect.y, rw = v->rect.width, rh = v->rect.height;
    if (rw < 140 || rh < 100)
        return false;
    const int pad = 10, strip = 46;
    int bar_y = ry + rh - strip;
    int cy = bar_y + strip / 2;
    o->btn_r = 13;
    o->btn_cx = rx + pad + o->btn_r;
    o->btn_cy = cy;
    int x = o->btn_cx + o->btn_r + 8;
    o->has_labels = (v->font != NULL);
    o->label_y = cy - 7;
    if (o->has_labels) {
        char cur[16], dur[16];
        video_fmt_time(cur, sizeof(cur), v->position);
        video_fmt_time(dur, sizeof(dur), v->duration);
        int cur_w = aroma_font_get_line_width(v->font, cur);
        int dur_w = aroma_font_get_line_width(v->font, dur);
        if (cur_w < 0)
            cur_w = 0;
        if (dur_w < 0)
            dur_w = 0;
        o->cur_x = x;
        x += cur_w + 8;
        o->dur_x = rx + rw - pad - dur_w;
        o->tx = x;
    } else {
        o->tx = x;
        o->dur_x = rx + rw - pad;
    }
    int tx1 = o->has_labels ? o->dur_x - 8 : rx + rw - pad;
    o->tw = tx1 - o->tx;
    if (o->tw < 40)
        return false;
    o->ty = cy - 2;
    return true;
}

static double video_osd_frac(AromaVideo *v) {
    if (!v || v->duration <= 0.0)
        return 0.0;
    double f = v->position / v->duration;
    if (f < 0.0)
        return 0.0;
    if (f > 1.0)
        return 1.0;
    return f;
}

static bool video_osd_point(AromaVideo *v, int x, int y, int *region) {
    // region: 1 = play button, 2 = seek track. false when OSD hidden/tiny.
    if (!v || !region)
        return false;
    *region = 0;
    if (!v->show_controls || !v->controls_visible)
        return false;
    VideoOsd o;
    if (!video_osd_geom(v, &o))
        return false;
    int dx = x - o.btn_cx, dy = y - o.btn_cy;
    int r = o.btn_r + 5;
    if (dx * dx + dy * dy <= r * r) {
        *region = 1;
        return true;
    }
    if (x >= o.tx - 6 && x <= o.tx + o.tw + 6 && y >= o.ty - 11 && y <= o.ty + 16) {
        *region = 2;
        return true;
    }
    return false;
}

static double video_osd_seek_frac(AromaVideo *v, int x) {
    VideoOsd o;
    if (!video_osd_geom(v, &o) || o.tw <= 0)
        return -1.0;
    double f = (double)(x - o.tx) / (double)o.tw;
    if (f < 0.0)
        f = 0.0;
    if (f > 1.0)
        f = 1.0;
    return f;
}

static void video_draw_osd(AromaNode *node, size_t window_id) {
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;
    if (!v->show_controls || !v->controls_visible)
        return;
    VideoOsd o;
    if (!video_osd_geom(v, &o))
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle)
        return;
    int rx = v->rect.x, ry = v->rect.y, rw = v->rect.width, rh = v->rect.height;
    int bar_y = ry + rh - 46;

    if (gfx->graphics_set_clip)
        gfx->graphics_set_clip(rx, bar_y, rw, 46);
    gfx->fill_rectangle(window_id, rx, bar_y, rw, 23, 0x44000000, false, 0.0f);
    gfx->fill_rectangle(window_id, rx, bar_y + 23, rw, 23, 0x66000000, false, 0.0f);
    if (gfx->graphics_clear_clip)
        gfx->graphics_clear_clip();

    gfx->fill_rectangle(window_id, o.tx, o.ty, o.tw, 5, 0x66FFFFFF, true, 2.0f);
    double frac = video_osd_frac(v);
    int fill_w = (int)(o.tw * frac);
    if (fill_w > 0)
        gfx->fill_rectangle(window_id, o.tx, o.ty, fill_w, 5, 0xFFFFFFFF, true, 2.0f);
    int kd = v->scrubbing ? 16 : 12;
    int kx = o.tx + fill_w;
    gfx->fill_rectangle(window_id, kx - kd / 2, o.btn_cy - kd / 2, kd, kd,
                        0xFFFFFFFF, true, (float)kd / 2.0f);

    gfx->fill_rectangle(window_id, o.btn_cx - o.btn_r, o.btn_cy - o.btn_r,
                        o.btn_r * 2, o.btn_r * 2, 0xCC000000, true, (float)o.btn_r);

    if (gfx->draw_line) {
        uint32_t white = 0xFFFFFFFF;
        if (v->state == AROMA_VIDEO_PLAYING) {
            int bw = 4, bh = 13, gap = 5;
            int x0 = o.btn_cx - (bw * 2 + gap) / 2;
            int y0 = o.btn_cy - bh / 2;
            gfx->fill_rectangle(window_id, x0, y0, bw, bh, white, false, 0.0f);
            gfx->fill_rectangle(window_id, x0 + bw + gap, y0, bw, bh, white, false, 0.0f);
        } else {
            int x0 = o.btn_cx - 4, y0 = o.btn_cy - 7, y1 = o.btn_cy + 7, x1 = o.btn_cx + 7;
            gfx->draw_line(window_id, x0, y0, x0, y1, white, 3.0f, true);
            gfx->draw_line(window_id, x0, y1, x1, o.btn_cy, white, 3.0f, true);
            gfx->draw_line(window_id, x1, o.btn_cy, x0, y0, white, 3.0f, true);
        }
    }

    if (o.has_labels && gfx->render_text && v->font) {
        char cur[16], dur[16];
        video_fmt_time(cur, sizeof(cur), v->position);
        video_fmt_time(dur, sizeof(dur), v->duration);
        gfx->render_text(window_id, v->font, cur, o.cur_x, o.label_y, 0xFFFFFFFF, 1.0f);
        gfx->render_text(window_id, v->font, dur, o.dur_x, o.label_y, 0xFFFFFFFF, 1.0f);
    }
}

static void video_scrub_to(AromaNode *node, AromaVideo *v, int x) {
    double f = video_osd_seek_frac(v, x);
    if (f < 0.0)
        return;
    if (v->duration > 0.0)
        aroma_video_seek(node, f * v->duration);
    video_touch_controls(node);
}

static void video_scrub_begin(AromaNode *node, AromaVideo *v, int x) {
    v->scrubbing = true;
    v->scrub_resume = v->playing;
    if (v->playing)
        aroma_video_pause(node);
    video_scrub_to(node, v, x);
}

static void video_scrub_end(AromaNode *node, AromaVideo *v) {
    if (!v->scrubbing)
        return;
    v->scrubbing = false;
    if (v->scrub_resume) {
        v->scrub_resume = false;
        aroma_video_play(node);
    }
    video_touch_controls(node);
}

static void video_ensure_events(AromaNode *node, AromaVideo *v) {
    if (v->events_registered)
        return;
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, video_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, video_event_handler, node, 80);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, video_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, video_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, video_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, video_event_handler, node, 80);
    v->events_registered = true;
}

void aroma_video_draw(AromaNode *video_node, size_t window_id) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(video_node))
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (v->rect.width <= 0 || v->rect.height <= 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    if (v->texture_id == 0 || !v->has_frame) {
        if (gfx->fill_rectangle) {
            gfx->fill_rectangle(window_id, v->rect.x, v->rect.y,
                                v->rect.width, v->rect.height,
                                0xFF141419, false, 0.0f);
            if (gfx->draw_hollow_rectangle)
                gfx->draw_hollow_rectangle(window_id, v->rect.x, v->rect.y,
                                           v->rect.width, v->rect.height,
                                           0xFF3A3A44, 1, false, 0.0f);
        }
        return;
    }

    int dx = v->rect.x, dy = v->rect.y;
    int dw = v->rect.width, dh = v->rect.height;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (v->scale_mode != AROMA_IMAGE_SCALE_FILL && gfx->get_image_size) {
        int tw = 0, th = 0;
        if (gfx->get_image_size(v->texture_id, &tw, &th) && tw > 0 && th > 0) {
            if (v->scale_mode == AROMA_IMAGE_SCALE_FIT) {
                float s = (float)dw / (float)tw;
                float s2 = (float)dh / (float)th;
                if (s2 < s)
                    s = s2;
                int fw = (int)(tw * s), fh = (int)(th * s);
                if (fw < 1)
                    fw = 1;
                if (fh < 1)
                    fh = 1;
                dx += (dw - fw) / 2;
                dy += (dh - fh) / 2;
                dw = fw;
                dh = fh;
            } else {
                float s = (float)dw / (float)tw;
                float s2 = (float)dh / (float)th;
                if (s2 > s)
                    s = s2;
                float sw = (float)dw / s, sh = (float)dh / s;
                float sx = ((float)tw - sw) / 2.0f;
                float sy = ((float)th - sh) / 2.0f;
                u0 = sx / (float)tw;
                v0 = sy / (float)th;
                u1 = (sx + sw) / (float)tw;
                v1 = (sy + sh) / (float)th;
            }
        }
    }

    if (gfx->draw_image_uv && (u0 != 0.0f || v0 != 0.0f || u1 != 1.0f || v1 != 1.0f)) {
        gfx->draw_image_uv(window_id, dx, dy, dw, dh, v->texture_id,
                           v->corner_radius, u0, v0, u1, v1);
    } else if (gfx->draw_image) {
        gfx->draw_image(window_id, dx, dy, dw, dh, v->texture_id, v->corner_radius);
    }

    if (v->has_frame)
        video_draw_osd(video_node, window_id);
}

void aroma_video_destroy_node(AromaNode *node) {
    if (!node)
        return;
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;
    if (!v)
        return;
    if (v->timer) {
        aroma_timer_cancel(v->timer);
        v->timer = NULL;
    }
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (v->texture_id != 0 && v->owns_texture && gfx && gfx->unload_image)
        gfx->unload_image(v->texture_id);
    v->texture_id = 0;
    video_close_decoder(v);
    v->node = NULL;
    aroma_widget_free(v);
    node->node_widget_ptr = NULL;
}

static AromaNode *video_create_common(AromaNode *parent, int x, int y, int width, int height) {
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("aroma_video: invalid parent or size");
        return NULL;
    }
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    AromaVideo *v = (AromaVideo *)aroma_widget_alloc(sizeof(AromaVideo));
    if (!v) {
        LOG_ERROR("aroma_video: out of memory");
        return NULL;
    }
    memset(v, 0, sizeof(AromaVideo));
    v->rect.x = x;
    v->rect.y = y;
    v->rect.width = width;
    v->rect.height = height;
    v->looping = false;
    v->muted = true;
    v->click_to_toggle = true;
    v->playing = false;
    v->state = AROMA_VIDEO_STOPPED;
    v->scale_mode = AROMA_IMAGE_SCALE_FIT;
    v->active_pointer_id = -1;
    v->push_mode = true;
    v->show_controls = true;
    v->controls_visible = true;
    v->controls_ms = 0;
    v->controls_timeout_ms = 3000;
    v->font = NULL;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, v);
    if (!node) {
        aroma_widget_free(v);
        return NULL;
    }
    v->node = node;
    aroma_node_set_draw_cb(node, aroma_video_draw);
    node->destroy_cb = aroma_video_destroy_node;
    v->timer = aroma_timer_create(AROMA_VIDEO_TICK_MS, true, video_tick_cb, v);
    if (!v->timer)
        LOG_WARNING("aroma_video: timer unavailable, playback will not advance");
    video_ensure_events(node, v);
    return node;
}

AromaNode *aroma_video_create(AromaNode *parent, const char *video_path,
                              int x, int y, int width, int height) {
    AromaNode *node = video_create_common(parent, x, y, width, height);
    if (!node)
        return NULL;
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;
    if (video_path && video_path[0]) {
        strncpy(v->path, video_path, sizeof(v->path) - 1);
        v->path[sizeof(v->path) - 1] = '\0';
        if (!video_open_file(v, video_path))
            v->push_mode = true;
        if (v->state != AROMA_VIDEO_ERROR)
            v->state = AROMA_VIDEO_STOPPED;
    } else {
        v->push_mode = true;
    }
    LOG_INFO("aroma_video: created at (%d,%d) %dx%d src='%s'", x, y, width, height,
             video_path ? video_path : "(push)");
    return node;
}

AromaNode *aroma_video_create_from_memory(AromaNode *parent,
                                           unsigned char *data, size_t data_size,
                                           int x, int y, int width, int height) {
    if (!data || data_size == 0) {
        LOG_ERROR("aroma_video: invalid memory buffer");
        return NULL;
    }
    AromaNode *node = video_create_common(parent, x, y, width, height);
    if (!node)
        return NULL;
    AromaVideo *v = (AromaVideo *)node->node_widget_ptr;
    snprintf(v->path, sizeof(v->path), "<memory:%zu bytes>", data_size);
    if (!video_open_memory(v, data, data_size))
        v->push_mode = true;
    if (v->state != AROMA_VIDEO_ERROR)
        v->state = AROMA_VIDEO_STOPPED;
    return node;
}

void aroma_video_set_source(AromaNode *video_node, const char *video_path) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (v->texture_id != 0 && v->owns_texture && gfx && gfx->unload_image)
        gfx->unload_image(v->texture_id);
    v->texture_id = 0;
    v->owns_texture = false;
    v->has_frame = false;
    v->video_w = 0;
    v->video_h = 0;
    v->duration = 0.0;
    v->framerate = 0.0;
    v->position = 0.0;
    v->playing = false;
    v->finished_notified = false;
    v->error_msg[0] = '\0';
    video_close_decoder(v);
    if (video_path && video_path[0]) {
        strncpy(v->path, video_path, sizeof(v->path) - 1);
        v->path[sizeof(v->path) - 1] = '\0';
        if (!video_open_file(v, video_path))
            v->push_mode = true;
        if (v->state != AROMA_VIDEO_ERROR)
            v->state = AROMA_VIDEO_STOPPED;
    } else {
        v->path[0] = '\0';
        v->push_mode = true;
        v->state = AROMA_VIDEO_STOPPED;
    }
    aroma_node_invalidate(video_node);
}

bool aroma_video_push_frame(AromaNode *video_node, const unsigned char *rgba,
                             int frame_width, int frame_height) {
    if (!video_node || !video_node->node_widget_ptr || !rgba)
        return false;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (v->codec)
        return false;
    if (frame_width <= 0 || frame_height <= 0)
        return false;
    if (frame_width > 4096 || frame_height > 4096)
        return false;
    video_upload_rgba(v, rgba, frame_width, frame_height);
    if (v->texture_id == 0)
        return false;
    v->push_mode = true;
    if (v->state == AROMA_VIDEO_ERROR)
        v->state = AROMA_VIDEO_STOPPED;
    v->playing = true;
    if (v->state != AROMA_VIDEO_PLAYING)
        v->state = AROMA_VIDEO_PLAYING;
    aroma_node_invalidate(video_node);
    if (v->on_frame)
        v->on_frame(video_node, v->position, v->frame_ud);
    return true;
}

void aroma_video_play(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (v->state == AROMA_VIDEO_ERROR)
        return;
    if (!v->codec) {
        v->playing = true;
        if (v->state != AROMA_VIDEO_PLAYING)
            v->state = AROMA_VIDEO_PLAYING;
        aroma_node_invalidate(video_node);
        return;
    }
    if (v->state == AROMA_VIDEO_FINISHED) {
        aroma_video_codec_rewind(v->codec);
        v->position = 0.0;
        v->finished_notified = false;
    }
    v->playing = true;
    v->state = AROMA_VIDEO_PLAYING;
    v->last_tick_ms = aroma_time_now_ms();
    video_touch_controls(video_node);
}

void aroma_video_pause(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->playing = false;
    if (v->state == AROMA_VIDEO_PLAYING)
        v->state = AROMA_VIDEO_PAUSED;
    aroma_node_invalidate(video_node);
}

void aroma_video_stop(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->playing = false;
    v->finished_notified = false;
    if (v->codec) {
        aroma_video_codec_rewind(v->codec);
        v->position = 0.0;
    } else {
        v->position = 0.0;
    }
    if (v->state != AROMA_VIDEO_ERROR)
        v->state = AROMA_VIDEO_STOPPED;
    aroma_node_invalidate(video_node);
}

bool aroma_video_seek(AromaNode *video_node, double time_seconds) {
    if (!video_node || !video_node->node_widget_ptr)
        return false;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (time_seconds < 0.0)
        time_seconds = 0.0;
    if (!v->codec)
        return false;
    if (v->duration > 0.0 && time_seconds > v->duration)
        time_seconds = v->duration;
    if (!aroma_video_codec_seek(v->codec, time_seconds))
        return false;
    v->position = time_seconds;
    v->last_tick_ms = aroma_time_now_ms();
    if (v->state == AROMA_VIDEO_FINISHED || v->state == AROMA_VIDEO_STOPPED) {
        v->state = v->playing ? AROMA_VIDEO_PLAYING : AROMA_VIDEO_PAUSED;
        v->finished_notified = false;
    }
    aroma_node_invalidate(video_node);
    return true;
}

void aroma_video_set_looping(AromaNode *video_node, bool loop) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->looping = loop;
    if (v->codec)
        aroma_video_codec_set_loop(v->codec, loop);
}

bool aroma_video_get_looping(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return false;
    return ((AromaVideo *)video_node->node_widget_ptr)->looping;
}

void aroma_video_set_muted(AromaNode *video_node, bool muted) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    ((AromaVideo *)video_node->node_widget_ptr)->muted = muted;
}

bool aroma_video_get_muted(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return true;
    return ((AromaVideo *)video_node->node_widget_ptr)->muted;
}

void aroma_video_set_click_to_toggle(AromaNode *video_node, bool enabled) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    ((AromaVideo *)video_node->node_widget_ptr)->click_to_toggle = enabled;
}

void aroma_video_set_on_finished(AromaNode *video_node, AromaVideoFinishedCb cb, void *user_data) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->on_finished = cb;
    v->finished_ud = user_data;
}

void aroma_video_set_on_error(AromaNode *video_node, AromaVideoErrorCb cb, void *user_data) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->on_error = cb;
    v->error_ud = user_data;
    if (cb && v->state == AROMA_VIDEO_ERROR && v->error_msg[0])
        cb(video_node, v->error_msg, user_data);
}

void aroma_video_set_on_frame(AromaNode *video_node, AromaVideoFrameCb cb, void *user_data) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->on_frame = cb;
    v->frame_ud = user_data;
}

AromaVideoState aroma_video_get_state(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return AROMA_VIDEO_ERROR;
    return ((AromaVideo *)video_node->node_widget_ptr)->state;
}

bool aroma_video_is_playing(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return false;
    return ((AromaVideo *)video_node->node_widget_ptr)->playing;
}

double aroma_video_get_duration(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0.0;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (v->codec) {
        double d = aroma_video_codec_duration(v->codec);
        if (d > 0.0)
            return d;
    }
    return v->duration;
}

double aroma_video_get_position(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0.0;
    return ((AromaVideo *)video_node->node_widget_ptr)->position;
}

double aroma_video_get_framerate(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0.0;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (v->codec) {
        double fps = aroma_video_codec_framerate(v->codec);
        if (fps > 0.0)
            return fps;
    }
    return v->framerate;
}

int aroma_video_get_video_width(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0;
    return ((AromaVideo *)video_node->node_widget_ptr)->video_w;
}

int aroma_video_get_video_height(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0;
    return ((AromaVideo *)video_node->node_widget_ptr)->video_h;
}

unsigned int aroma_video_get_texture_id(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0;
    return ((AromaVideo *)video_node->node_widget_ptr)->texture_id;
}

const char *aroma_video_get_source(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return NULL;
    return ((AromaVideo *)video_node->node_widget_ptr)->path;
}

static float video_clamp_radius(AromaVideo *v, float radius) {
    if (!v)
        return 0.0f;
    if (radius < 0.0f)
        return 0.0f;
    int min_side = v->rect.width < v->rect.height ? v->rect.width : v->rect.height;
    float max_r = (float)min_side / 2.0f;
    if (max_r < 0.0f)
        max_r = 0.0f;
    return radius > max_r ? max_r : radius;
}

void aroma_video_set_corner_radius(AromaNode *video_node, float radius) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->corner_radius = video_clamp_radius(v, radius);
    aroma_node_invalidate(video_node);
}

float aroma_video_get_corner_radius(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return 0.0f;
    return ((AromaVideo *)video_node->node_widget_ptr)->corner_radius;
}

void aroma_video_set_scale_mode(AromaNode *video_node, AromaImageScaleMode mode) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    if (mode < AROMA_IMAGE_SCALE_FILL || mode > AROMA_IMAGE_SCALE_COVER)
        return;
    ((AromaVideo *)video_node->node_widget_ptr)->scale_mode = mode;
    aroma_node_invalidate(video_node);
}

AromaImageScaleMode aroma_video_get_scale_mode(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return AROMA_IMAGE_SCALE_FILL;
    return ((AromaVideo *)video_node->node_widget_ptr)->scale_mode;
}

void aroma_video_set_size(AromaNode *video_node, int width, int height) {
    if (!video_node || !video_node->node_widget_ptr || width <= 0 || height <= 0)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->rect.width = width;
    v->rect.height = height;
    v->corner_radius = video_clamp_radius(v, v->corner_radius);
    aroma_node_invalidate(video_node);
}

void aroma_video_set_position(AromaNode *video_node, int x, int y) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->rect.x = x;
    v->rect.y = y;
    aroma_node_invalidate(video_node);
}

void aroma_video_set_font(AromaNode *video_node, AromaFont *font) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    ((AromaVideo *)video_node->node_widget_ptr)->font = font;
    aroma_node_invalidate(video_node);
}

void aroma_video_set_show_controls(AromaNode *video_node, bool show) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    v->show_controls = show;
    if (!show) {
        v->controls_visible = false;
        v->scrubbing = false;
    } else {
        video_touch_controls(video_node);
        return;
    }
    aroma_node_invalidate(video_node);
}

bool aroma_video_get_show_controls(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return false;
    return ((AromaVideo *)video_node->node_widget_ptr)->show_controls;
}

void aroma_video_set_controls_visible(AromaNode *video_node, bool visible) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    if (!v->show_controls)
        visible = false;
    v->controls_visible = visible;
    if (visible)
        v->controls_ms = aroma_time_now_ms();
    aroma_node_invalidate(video_node);
}

bool aroma_video_get_controls_visible(AromaNode *video_node) {
    if (!video_node || !video_node->node_widget_ptr)
        return false;
    AromaVideo *v = (AromaVideo *)video_node->node_widget_ptr;
    return v->show_controls && v->controls_visible;
}

void aroma_video_set_controls_timeout(AromaNode *video_node, uint32_t timeout_ms) {
    if (!video_node || !video_node->node_widget_ptr)
        return;
    ((AromaVideo *)video_node->node_widget_ptr)->controls_timeout_ms = timeout_ms;
}
