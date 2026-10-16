#include "widgets/aroma_video_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(ESP32) || defined(AROMA_NO_VIDEO_DECODER)
#define CODEC_HAS_PLM 0
#else
#define CODEC_HAS_PLM 1
#endif

#ifdef AROMA_HAS_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/cpu.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#endif

#if CODEC_HAS_PLM
#define PL_MPEG_IMPLEMENTATION
#include "backends/graphics/utils/pl_mpeg.h"
#endif

struct AromaVideoCodec {
    AromaVideoCodecFrameCb cb;
    void *user;
    bool loop;
    bool ended;
    const char *backend;
    double duration;
    double fps;
#ifdef AROMA_HAS_FFMPEG
    bool use_ffmpeg;
    AVFormatContext *fmt;
    AVCodecContext *dec;
    int vstream;
    AVRational tb;
    double tb_sec;
    double base;
    double clock;
    double last_pts;
    AVPacket *pkt;
    AVFrame *frame;
    struct SwsContext *sws;
    int sws_w;
    int sws_h;
    enum AVPixelFormat sws_fmt;
    uint8_t *rgba;
    size_t rgba_size;
    int rw;
    int rh;
    bool flushed;
    uint8_t *membuf;
    size_t memlen;
    size_t mempos;
    AVIOContext *avio;
#endif
#if CODEC_HAS_PLM
    plm_t *plm;
    unsigned char *tmp;
    size_t tmp_size;
#endif
};

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len > 0) {
        strncpy(err, msg, len - 1);
        err[len - 1] = '\0';
    }
}

#ifdef AROMA_HAS_FFMPEG
static void codec_log_filter(void *ptr, int level, const char *fmt, va_list vl)
{
    char line[512];
    va_list copy;
    va_copy(copy, vl);
    vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (strstr(line, "No accelerated colorspace conversion") ||
        strstr(line, "Invalid frame dimensions"))
        return;
    av_log_default_callback(ptr, level, fmt, vl);
}
#endif

void aroma_video_codec_close(AromaVideoCodec *c);

bool aroma_video_codec_have_decoder(void)
{
#if defined(AROMA_HAS_FFMPEG) || CODEC_HAS_PLM
    return true;
#else
    return false;
#endif
}

const char *aroma_video_codec_backend(const AromaVideoCodec *c)
{
    return (c && c->backend) ? c->backend : "none";
}

double aroma_video_codec_duration(const AromaVideoCodec *c)
{
    return c ? c->duration : 0.0;
}

double aroma_video_codec_framerate(const AromaVideoCodec *c)
{
    return c ? c->fps : 0.0;
}

bool aroma_video_codec_ended(const AromaVideoCodec *c)
{
    return !c || c->ended;
}

void aroma_video_codec_set_loop(AromaVideoCodec *c, bool loop)
{
    if (!c)
        return;
    c->loop = loop;
#if CODEC_HAS_PLM
    if (c->plm)
        plm_set_loop(c->plm, loop ? 1 : 0);
#endif
}

#ifdef AROMA_HAS_FFMPEG

#define CODEC_AVIO_BUF 65536
#define CODEC_MAX_ITERS 32
#define CODEC_SEEK_DISCARD_MAX 240

static int codec_mem_read(void *opaque, uint8_t *buf, int size)
{
    AromaVideoCodec *c = (AromaVideoCodec *)opaque;
    if (c->mempos >= c->memlen)
        return AVERROR_EOF;
    size_t avail = c->memlen - c->mempos;
    if ((size_t)size > avail)
        size = (int)avail;
    memcpy(buf, c->membuf + c->mempos, (size_t)size);
    c->mempos += (size_t)size;
    return size;
}

static int64_t codec_mem_seek(void *opaque, int64_t offset, int whence)
{
    AromaVideoCodec *c = (AromaVideoCodec *)opaque;
    if (whence == AVSEEK_SIZE)
        return (int64_t)c->memlen;
    int64_t pos = 0;
    if (whence == SEEK_SET)
        pos = offset;
    else if (whence == SEEK_CUR)
        pos = (int64_t)c->mempos + offset;
    else if (whence == SEEK_END)
        pos = (int64_t)c->memlen + offset;
    else
        return -1;
    if (pos < 0 || pos > (int64_t)c->memlen)
        return -1;
    c->mempos = (size_t)pos;
    return pos;
}

static double codec_calc_fps(AVStream *st, AVCodecContext *dec)
{
    AVRational r = st->avg_frame_rate;
    if (r.num <= 0 || r.den <= 0)
        r = st->r_frame_rate;
    if (r.num <= 0 || r.den <= 0)
        r = dec->framerate;
    if (r.num <= 0 || r.den <= 0)
        return 0.0;
    double fps = av_q2d(r);
    return (fps > 0.0 && fps < 1000.0) ? fps : 0.0;
}

static double codec_calc_duration(AVFormatContext *fmt, AVStream *st)
{
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        return (double)fmt->duration / (double)AV_TIME_BASE;
    if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        return (double)st->duration * av_q2d(st->time_base);
    return 0.0;
}

static void codec_close_ffmpeg(AromaVideoCodec *c)
{
    if (c->sws) {
        sws_freeContext(c->sws);
        c->sws = NULL;
    }
    if (c->pkt)
        av_packet_free(&c->pkt);
    if (c->frame)
        av_frame_free(&c->frame);
    if (c->dec)
        avcodec_free_context(&c->dec);
    if (c->fmt)
        avformat_close_input(&c->fmt);
    if (c->avio) {
        av_freep(&c->avio->buffer);
        avio_context_free(&c->avio);
    }
    if (c->membuf) {
        av_free(c->membuf);
        c->membuf = NULL;
    }
    if (c->rgba) {
        av_free(c->rgba);
        c->rgba = NULL;
        c->rgba_size = 0;
    }
    c->use_ffmpeg = false;
}

static bool codec_finish_open(AromaVideoCodec *c, char *err, size_t errlen)
{
    if (avformat_find_stream_info(c->fmt, NULL) < 0) {
        set_err(err, errlen, "cannot parse media headers");
        return false;
    }
    int idx = av_find_best_stream(c->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (idx < 0) {
        set_err(err, errlen, "no video stream found");
        return false;
    }
    AVStream *st = c->fmt->streams[idx];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        set_err(err, errlen, "video codec not supported by this build");
        return false;
    }
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    if (!ctx) {
        set_err(err, errlen, "out of memory");
        return false;
    }
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        avcodec_free_context(&ctx);
        set_err(err, errlen, "cannot configure video decoder");
        return false;
    }
    int threads = av_cpu_count();
    if (threads < 1)
        threads = 1;
    if (threads > 8)
        threads = 8;
    if (dec->id == AV_CODEC_ID_MPEG1VIDEO || dec->id == AV_CODEC_ID_MPEG2VIDEO)
        threads = 1;
    char thread_str[8];
    snprintf(thread_str, sizeof(thread_str), "%d", threads);
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "threads", thread_str, 0);
    int rc = avcodec_open2(ctx, dec, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        avcodec_free_context(&ctx);
        set_err(err, errlen, "cannot open video decoder");
        return false;
    }
    c->dec = ctx;
    c->vstream = idx;
    c->tb = st->time_base;
    c->tb_sec = av_q2d(st->time_base);
    if (!(c->tb_sec > 0.0))
        c->tb_sec = 1.0 / 90000.0;
    c->base = 0.0;
    if (st->start_time != AV_NOPTS_VALUE)
        c->base = (double)st->start_time * c->tb_sec;
    c->pkt = av_packet_alloc();
    c->frame = av_frame_alloc();
    if (!c->pkt || !c->frame) {
        set_err(err, errlen, "out of memory");
        return false;
    }
    c->duration = codec_calc_duration(c->fmt, st);
    c->fps = codec_calc_fps(st, ctx);
    c->clock = 0.0;
    c->last_pts = 0.0;
    c->use_ffmpeg = true;
    c->backend = "ffmpeg";
    return true;
}

static AromaVideoCodec *codec_alloc(AromaVideoCodecFrameCb cb, void *user)
{
    AromaVideoCodec *c = (AromaVideoCodec *)calloc(1, sizeof(AromaVideoCodec));
    if (!c)
        return NULL;
    c->cb = cb;
    c->user = user;
    c->vstream = -1;
    c->sws_fmt = AV_PIX_FMT_NONE;
#ifdef AROMA_HAS_FFMPEG
    {
        // Vendored build is portable C without SIMD, and post-seek packets
        // are skipped by design: both print benign notices on stderr.
        // Filter exactly those; everything else still goes through.
        static bool log_tamed = false;
        if (!log_tamed) {
            log_tamed = true;
            av_log_set_callback(codec_log_filter);
        }
    }
#endif
    return c;
}

static AromaVideoCodec *codec_open_ffmpeg_file(const char *path,
                                               AromaVideoCodecFrameCb cb,
                                               void *user,
                                               char *err, size_t errlen)
{
    AromaVideoCodec *c = codec_alloc(cb, user);
    if (!c) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    if (avformat_open_input(&c->fmt, path, NULL, NULL) < 0) {
        set_err(err, errlen, "cannot open file or unsupported container");
        aroma_video_codec_close(c);
        return NULL;
    }
    if (!codec_finish_open(c, err, errlen)) {
        aroma_video_codec_close(c);
        return NULL;
    }
    return c;
}

static AromaVideoCodec *codec_open_ffmpeg_memory(const unsigned char *data,
                                                 size_t len,
                                                 AromaVideoCodecFrameCb cb,
                                                 void *user,
                                                 char *err, size_t errlen)
{
    if (!data || len == 0) {
        set_err(err, errlen, "empty media buffer");
        return NULL;
    }
    AromaVideoCodec *c = codec_alloc(cb, user);
    if (!c) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    c->membuf = (uint8_t *)av_malloc(len);
    if (!c->membuf) {
        set_err(err, errlen, "out of memory");
        aroma_video_codec_close(c);
        return NULL;
    }
    memcpy(c->membuf, data, len);
    c->memlen = len;
    c->mempos = 0;
    uint8_t *buf = (uint8_t *)av_malloc(CODEC_AVIO_BUF);
    if (!buf) {
        set_err(err, errlen, "out of memory");
        aroma_video_codec_close(c);
        return NULL;
    }
    c->avio = avio_alloc_context(buf, CODEC_AVIO_BUF, 0, c,
                                 codec_mem_read, NULL, codec_mem_seek);
    if (!c->avio) {
        av_free(buf);
        set_err(err, errlen, "out of memory");
        aroma_video_codec_close(c);
        return NULL;
    }
    c->fmt = avformat_alloc_context();
    if (!c->fmt) {
        set_err(err, errlen, "out of memory");
        aroma_video_codec_close(c);
        return NULL;
    }
    c->fmt->pb = c->avio;
    if (avformat_open_input(&c->fmt, NULL, NULL, NULL) < 0) {
        set_err(err, errlen, "cannot parse media data or unsupported container");
        aroma_video_codec_close(c);
        return NULL;
    }
    if (!codec_finish_open(c, err, errlen)) {
        aroma_video_codec_close(c);
        return NULL;
    }
    return c;
}

static double codec_frame_pts(AromaVideoCodec *c)
{
    int64_t ts = c->frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE)
        ts = c->frame->pts;
    if (ts == AV_NOPTS_VALUE)
        return c->clock;
    double pts = (double)ts * c->tb_sec - c->base;
    if (pts < 0.0 && pts > -1.0)
        pts = 0.0;
    return pts;
}

static bool codec_ensure_sws(AromaVideoCodec *c)
{
    int w = c->frame->width;
    int h = c->frame->height;
    enum AVPixelFormat fmt = (enum AVPixelFormat)c->frame->format;
    if (w <= 0 || h <= 0 || fmt == AV_PIX_FMT_NONE)
        return false;
    if (c->sws && w == c->sws_w && h == c->sws_h && fmt == c->sws_fmt)
        return true;
    if (c->sws) {
        sws_freeContext(c->sws);
        c->sws = NULL;
    }
    struct SwsContext *sws = sws_alloc_context();
    if (!sws)
        return false;
    av_opt_set_int(sws, "srcw", w, 0);
    av_opt_set_int(sws, "srch", h, 0);
    av_opt_set_int(sws, "src_format", fmt, 0);
    av_opt_set_int(sws, "dstw", w, 0);
    av_opt_set_int(sws, "dsth", h, 0);
    av_opt_set_int(sws, "dst_format", AV_PIX_FMT_RGBA, 0);
    av_opt_set_int(sws, "sws_flags", SWS_BILINEAR, 0);
    if (sws_init_context(sws, NULL, NULL) < 0) {
        sws_freeContext(sws);
        return false;
    }
    size_t need = (size_t)w * (size_t)h * 4u;
    if (c->rgba_size < need) {
        av_free(c->rgba);
        c->rgba = (uint8_t *)av_malloc(need);
        if (!c->rgba) {
            sws_freeContext(sws);
            c->rgba_size = 0;
            return false;
        }
        c->rgba_size = need;
    }
    c->sws = sws;
    c->sws_w = w;
    c->sws_h = h;
    c->sws_fmt = fmt;
    c->rw = w;
    c->rh = h;
    return true;
}

static bool codec_deliver(AromaVideoCodec *c)
{
    if (!codec_ensure_sws(c))
        return false;
    uint8_t *dst[4] = { c->rgba, NULL, NULL, NULL };
    int stride[4] = { c->rw * 4, 0, 0, 0 };
    sws_scale(c->sws, (const uint8_t *const *)c->frame->data,
              c->frame->linesize, 0, c->rh, dst, stride);
    double pts = codec_frame_pts(c);
    c->last_pts = pts;
    if (c->cb)
        c->cb(c->rgba, c->rw, c->rh, pts, c->user);
    return true;
}

static int codec_read_video_packet(AromaVideoCodec *c)
{
    for (;;) {
        int rc = av_read_frame(c->fmt, c->pkt);
        if (rc == AVERROR_EOF)
            return 1;
        if (rc < 0)
            return -1;
        if (c->pkt->stream_index == c->vstream)
            return 0;
        av_packet_unref(c->pkt);
    }
}

static void codec_do_rewind(AromaVideoCodec *c)
{
    AVStream *st = c->fmt->streams[c->vstream];
    int64_t start = (st->start_time != AV_NOPTS_VALUE) ? st->start_time : 0;
    if (av_seek_frame(c->fmt, c->vstream, start, AVSEEK_FLAG_BACKWARD) < 0)
        avformat_seek_file(c->fmt, -1, INT64_MIN, start, start, 0);
    avcodec_flush_buffers(c->dec);
    c->flushed = false;
    c->ended = false;
    c->clock = 0.0;
}

static bool codec_step_ffmpeg(AromaVideoCodec *c, double dt)
{
    if (dt < 0.0)
        dt = 0.0;
    if (dt > 0.5)
        dt = 0.5;
    double target = c->clock + dt;
    int iters = 0;
    int bad_streak = 0;
    while (c->clock < target && iters++ < CODEC_MAX_ITERS) {

        int drained = 0;
        for (;;) {
            if (drained++ >= 16)
                break;
            int rc = avcodec_receive_frame(c->dec, c->frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {

                if (++bad_streak > 100)
                    return false;
                break;
            }
            double pts = codec_frame_pts(c);
            if (pts < c->clock - 1.0) {
                continue;
            }
            if (!codec_deliver(c)) {
                continue;
            }
            bad_streak = 0;
            if (pts >= target - 1e-6) {
                c->clock = target;
                return true;
            }
        }
        if (c->clock >= target) {
            c->clock = target;
            return true;
        }
        if (c->flushed) {
            if (c->loop) {
                codec_do_rewind(c);
                target = c->clock + dt;
                continue;
            }
            c->ended = true;
            c->clock = target;
            return true;
        }
        int got = codec_read_video_packet(c);
        if (got != 0) {
            avcodec_send_packet(c->dec, NULL);
            c->flushed = true;
            continue;
        }
        int rc = avcodec_send_packet(c->dec, c->pkt);
        av_packet_unref(c->pkt);
        if (rc == AVERROR(EAGAIN))
            continue;
        if (rc < 0) {

            if (++bad_streak > 100)
                return false;
            continue;
        }
        bad_streak = 0;
    }
    c->clock = target;
    return true;
}

static bool codec_seek_ffmpeg(AromaVideoCodec *c, double t)
{
    if (t < 0.0)
        t = 0.0;
    if (c->duration > 0.0 && t > c->duration)
        t = c->duration;
    double abs_t = t + c->base;
    int64_t ts = (int64_t)(abs_t / c->tb_sec);
    if (ts < 0)
        ts = 0;
    if (av_seek_frame(c->fmt, c->vstream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(c->dec);
    c->flushed = false;
    c->ended = false;

    int left = CODEC_SEEK_DISCARD_MAX;
    bool delivered = false;
    while (left-- > 0) {
        int rc = avcodec_receive_frame(c->dec, c->frame);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            if (c->flushed)
                break;
            int got = codec_read_video_packet(c);
            if (got != 0) {
                avcodec_send_packet(c->dec, NULL);
                c->flushed = true;
                continue;
            }
            rc = avcodec_send_packet(c->dec, c->pkt);
            av_packet_unref(c->pkt);
            if (rc == AVERROR(EAGAIN) || rc == 0)
                continue;
            continue;
        }
        if (rc < 0)
            continue;
        double pts = codec_frame_pts(c);
        if (pts < t) {
            continue;
        }
        if (!codec_deliver(c)) {
            continue;
        }
        delivered = true;
        break;
    }
    c->clock = t;
    if (!delivered && c->flushed)
        c->ended = true;
    return true;
}

#endif

#if CODEC_HAS_PLM

static void codec_plm_frame(plm_t *plm, plm_frame_t *frame, void *user)
{
    (void)plm;
    AromaVideoCodec *c = (AromaVideoCodec *)user;
    if (!frame || frame->width == 0 || frame->height == 0)
        return;
    int w = (int)frame->width;
    int h = (int)frame->height;
    size_t need = (size_t)w * (size_t)h * 4u;
    if (c->tmp_size < need) {
        unsigned char *nb = (unsigned char *)realloc(c->tmp, need);
        if (!nb)
            return;
        c->tmp = nb;
        c->tmp_size = need;
    }
    plm_frame_to_rgba(frame, c->tmp, w * 4);
    if (c->cb)
        c->cb(c->tmp, w, h, frame->time, c->user);
}

static void codec_plm_refresh(AromaVideoCodec *c)
{
    if (c->duration <= 0.0) {
        double d = plm_get_duration(c->plm);
        if (d > 0.0 && d < 86400.0)
            c->duration = d;
    }
    if (c->fps <= 0.0) {
        double fps = plm_get_framerate(c->plm);
        if (fps > 0.0 && fps < 240.0)
            c->fps = fps;
    }
}

static AromaVideoCodec *codec_open_plm_file(const char *path,
                                            AromaVideoCodecFrameCb cb,
                                            void *user,
                                            char *err, size_t errlen)
{
    plm_t *plm = plm_create_with_filename(path);
    if (!plm) {
        set_err(err, errlen, "cannot open file");
        return NULL;
    }
    plm_set_audio_enabled(plm, 0);
    plm_set_video_enabled(plm, 1);
    plm_probe(plm, 256 * 1024);
    if (plm_get_num_video_streams(plm) <= 0) {
        plm_destroy(plm);
        set_err(err, errlen, "no MPEG-1 video stream (use .mpg, or enable FFmpeg for MP4/WebM/MKV)");
        return NULL;
    }
    AromaVideoCodec *c = (AromaVideoCodec *)calloc(1, sizeof(AromaVideoCodec));
    if (!c) {
        plm_destroy(plm);
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    c->cb = cb;
    c->user = user;
    c->plm = plm;
    c->backend = "pl_mpeg";
    plm_set_loop(plm, c->loop ? 1 : 0);
    plm_set_video_decode_callback(plm, codec_plm_frame, c);
    codec_plm_refresh(c);
    return c;
}

static AromaVideoCodec *codec_open_plm_memory(const unsigned char *data,
                                              size_t len,
                                              AromaVideoCodecFrameCb cb,
                                              void *user,
                                              char *err, size_t errlen)
{
    if (!data || len == 0) {
        set_err(err, errlen, "empty media buffer");
        return NULL;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    memcpy(copy, data, len);
    plm_t *plm = plm_create_with_memory(copy, len, 1);
    if (!plm) {
        free(copy);
        set_err(err, errlen, "cannot parse media data");
        return NULL;
    }
    plm_set_audio_enabled(plm, 0);
    plm_set_video_enabled(plm, 1);
    plm_probe(plm, 256 * 1024);
    if (plm_get_num_video_streams(plm) <= 0) {
        plm_destroy(plm);
        set_err(err, errlen, "no MPEG-1 video stream (use .mpg, or enable FFmpeg for MP4/WebM/MKV)");
        return NULL;
    }
    AromaVideoCodec *c = (AromaVideoCodec *)calloc(1, sizeof(AromaVideoCodec));
    if (!c) {
        plm_destroy(plm);
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    c->cb = cb;
    c->user = user;
    c->plm = plm;
    c->backend = "pl_mpeg";
    plm_set_loop(plm, c->loop ? 1 : 0);
    plm_set_video_decode_callback(plm, codec_plm_frame, c);
    codec_plm_refresh(c);
    return c;
}

#endif

AromaVideoCodec *aroma_video_codec_open_file(const char *path,
                                             AromaVideoCodecFrameCb cb,
                                             void *user,
                                             char *err, size_t errlen)
{
    if (!path || !path[0]) {
        set_err(err, errlen, "empty video path");
        return NULL;
    }
#ifdef AROMA_HAS_FFMPEG
    {
        char ffmpeg_err[256] = { 0 };
        AromaVideoCodec *c = codec_open_ffmpeg_file(path, cb, user,
                                                    ffmpeg_err, sizeof(ffmpeg_err));
        if (c)
            return c;
#if CODEC_HAS_PLM

        (void)ffmpeg_err;
#else
        if (err && errlen > 0) {
            strncpy(err, ffmpeg_err, errlen - 1);
            err[errlen - 1] = '\0';
        }
        return NULL;
#endif
    }
#endif
#if CODEC_HAS_PLM
    return codec_open_plm_file(path, cb, user, err, errlen);
#else
    set_err(err, errlen, "no video decoder available on this platform");
    return NULL;
#endif
}

AromaVideoCodec *aroma_video_codec_open_memory(const unsigned char *data,
                                               size_t len,
                                               AromaVideoCodecFrameCb cb,
                                               void *user,
                                               char *err, size_t errlen)
{
#ifdef AROMA_HAS_FFMPEG
    {
        char ffmpeg_err[256] = { 0 };
        AromaVideoCodec *c = codec_open_ffmpeg_memory(data, len, cb, user,
                                                      ffmpeg_err, sizeof(ffmpeg_err));
        if (c)
            return c;
#if CODEC_HAS_PLM
        (void)ffmpeg_err;
#else
        if (err && errlen > 0) {
            strncpy(err, ffmpeg_err, errlen - 1);
            err[errlen - 1] = '\0';
        }
        return NULL;
#endif
    }
#endif
#if CODEC_HAS_PLM
    return codec_open_plm_memory(data, len, cb, user, err, errlen);
#else
    (void)data;
    (void)len;
    set_err(err, errlen, "no video decoder available on this platform");
    return NULL;
#endif
}

void aroma_video_codec_close(AromaVideoCodec *c)
{
    if (!c)
        return;
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg)
        codec_close_ffmpeg(c);
#endif
#if CODEC_HAS_PLM
    if (c->plm)
        plm_destroy(c->plm);
    free(c->tmp);
#endif
    free(c);
}

bool aroma_video_codec_step(AromaVideoCodec *c, double dt)
{
    if (!c)
        return false;
    if (c->ended)
        return true;
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg)
        return codec_step_ffmpeg(c, dt);
#endif
#if CODEC_HAS_PLM
    if (c->plm) {
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 0.5)
            dt = 0.5;
        plm_decode(c->plm, dt);
        codec_plm_refresh(c);
        if (plm_has_ended(c->plm))
            c->ended = true;
        return true;
    }
#endif
    return false;
}

bool aroma_video_codec_seek(AromaVideoCodec *c, double t)
{
    if (!c)
        return false;
    if (t < 0.0)
        t = 0.0;
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg)
        return codec_seek_ffmpeg(c, t);
#endif
#if CODEC_HAS_PLM
    if (c->plm) {
        if (c->duration > 0.0 && t > c->duration)
            t = c->duration;
        if (plm_seek(c->plm, t, 1))
            return true;
        return plm_seek(c->plm, t, 0) != 0;
    }
#endif
    return false;
}

void aroma_video_codec_rewind(AromaVideoCodec *c)
{
    if (!c)
        return;
    c->ended = false;
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg) {
        codec_do_rewind(c);
        return;
    }
#endif
#if CODEC_HAS_PLM
    if (c->plm)
        plm_rewind(c->plm);
#endif
}
