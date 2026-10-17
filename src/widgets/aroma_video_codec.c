#include "widgets/aroma_video_codec.h"
#include "widgets/aroma_audio_out.h"
#include "core/aroma_logger.h"

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
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#endif

#if CODEC_HAS_PLM
#define PL_MPEG_IMPLEMENTATION
#include "backends/graphics/utils/pl_mpeg.h"
#endif

#ifdef __ANDROID__
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Open an extractor for a file path. Path-based setDataSource is tried
// first; several OEM extractors reject plain paths that fd-based access
// handles fine, so fall back to open()+setDataSourceFd (fd stays open for
// the extractor's lifetime; store it and close on destroy).
static AMediaExtractor *android_extractor_for(const char *open_path, int *fd_out)
{
    if (fd_out)
        *fd_out = -1;
    AMediaExtractor *ex = AMediaExtractor_new();
    if (!ex)
        return NULL;
    if (AMediaExtractor_setDataSource(ex, open_path) == AMEDIA_OK)
        return ex;
    int fd = open(open_path, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
            AMediaExtractor_setDataSourceFd(ex, fd, 0, (off64_t)st.st_size) == AMEDIA_OK) {
            LOG_INFO("aroma_video: mediacodec using fd source for '%s'", open_path);
            if (fd_out)
                *fd_out = fd;
            else
                close(fd);
            return ex;
        }
        close(fd);
    }
    LOG_ERROR("aroma_video: mediacodec setDataSource failed for '%s'", open_path);
    AMediaExtractor_delete(ex);
    return NULL;
}
#endif

struct AromaVideoCodec {
    AromaVideoCodecFrameCb cb;
    void *user;
    bool loop;
    bool muted;
    bool ended;
    const char *backend;
    double duration;
    double fps;
    // Set once the PCM sink has been opened for this instance.
    bool audio_opened;
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
    // Audio (optional): decoded + resampled to s16 stereo.
    AVCodecContext *adec;
    int astream;
    struct SwrContext *swr;
    AVFrame *aframe;
    int a_out_rate;
    int a_out_ch;
#endif
#if CODEC_HAS_PLM
    plm_t *plm;
    unsigned char *tmp;
    size_t tmp_size;
#endif
#ifdef __ANDROID__
    bool use_android;
    AMediaExtractor *aex_v;
    AMediaExtractor *aex_a;
    AMediaCodec *vdec;
    AMediaCodec *adec_mc;
    int a_fd_v;
    int a_fd_a;
    int a_width;
    int a_height;
    double a_clock;
    double a_base;
    bool a_v_eos;
    bool a_a_eos;
    uint8_t *a_rgba;
    size_t a_rgba_size;
    int a_rw;
    int a_rh;
    int a_arate;
    int a_ach;
#endif
};

static void set_err(char *err, size_t len, const char *msg)
{
    if (err && len > 0) {
        strncpy(err, msg, len - 1);
        err[len - 1] = '\0';
    }
}

#ifdef __ANDROID__
const char *aroma_video_android_cache_path(const char *name);
#endif

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

void aroma_video_codec_set_muted(AromaVideoCodec *c, bool muted)
{
    if (!c)
        return;
    c->muted = muted;
    aroma_audio_out_set_muted(muted);
    if (muted)
        aroma_audio_out_flush();
}

// Fixed PCM sink format for the FFmpeg/pl_mpeg paths: 44.1kHz stereo s16.
// (The Android MediaCodec path opens the sink at the stream's native
// rate instead; see android_drain_audio.) Resampling in the codec keeps
// the audio backend trivial.
#define CODEC_A_OUT_RATE 44100
#define CODEC_A_OUT_CH 2

static void codec_audio_emit(AromaVideoCodec *c, const int16_t *pcm, size_t frames)
{
    if (c->muted || !pcm || frames == 0)
        return;
    if (!c->audio_opened) {
        if (!aroma_audio_out_open(CODEC_A_OUT_RATE, CODEC_A_OUT_CH))
            return;
        c->audio_opened = true;
    }
    aroma_audio_out_write(pcm, frames);
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
    if (c->swr) {
        swr_free(&c->swr);
        c->swr = NULL;
    }
    if (c->aframe)
        av_frame_free(&c->aframe);
    if (c->adec)
        avcodec_free_context(&c->adec);
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
    c->astream = -1;
    c->audio_opened = false;
}

// Fixed PCM sink format is defined above (CODEC_A_OUT_RATE/CH).

static bool codec_audio_setup(AromaVideoCodec *c)
{
    c->astream = -1;
    c->audio_opened = false;
    int idx = av_find_best_stream(c->fmt, AVMEDIA_TYPE_AUDIO, -1, c->vstream, NULL, 0);
    if (idx < 0)
        return true; // video-only file: not an error
    AVStream *st = c->fmt->streams[idx];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec)
        return true;
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    if (!ctx)
        return true;
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return true;
    }
    if (avcodec_open2(ctx, dec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return true;
    }
    struct SwrContext *swr = NULL;
    {
        AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout in_ch = ctx->ch_layout;
        if (in_ch.nb_channels != 1 && in_ch.nb_channels != 2) {
            // Fall back to stereo mask when the source layout is exotic.
            av_channel_layout_uninit(&in_ch);
            av_channel_layout_from_mask(&in_ch, AV_CH_LAYOUT_STEREO);
        }
        if (swr_alloc_set_opts2(&swr,
                &out_ch, AV_SAMPLE_FMT_S16, CODEC_A_OUT_RATE,
                &in_ch, ctx->sample_fmt,
                ctx->sample_rate ? ctx->sample_rate : CODEC_A_OUT_RATE,
                0, NULL) < 0)
            swr = NULL;
    }
    // Fallback for very old channel layouts without mask.
    if (!swr) {
        avcodec_free_context(&ctx);
        return true;
    }
    if (swr_init(swr) < 0) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        return true;
    }
    AVFrame *af = av_frame_alloc();
    if (!af) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        return true;
    }
    LOG_INFO("aroma_video: audio %s %dHz ch%d -> 44100Hz stereo",
             dec->name ? dec->name : "?",
             ctx->sample_rate, ctx->ch_layout.nb_channels);
    c->adec = ctx;
    c->astream = idx;
    c->swr = swr;
    c->aframe = af;
    c->a_out_rate = CODEC_A_OUT_RATE;
    c->a_out_ch = CODEC_A_OUT_CH;
    return true;
}

static void codec_audio_decode_packet(AromaVideoCodec *c, AVPacket *pkt)
{
    if (!c->adec || !c->swr || !c->aframe)
        return;
    if (avcodec_send_packet(c->adec, pkt) < 0)
        return;
    for (;;) {
        int rc = avcodec_receive_frame(c->adec, c->aframe);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0)
            break;
        uint8_t *out = NULL;
        int out_samples = av_rescale_rnd(
            swr_get_delay(c->swr, c->adec->sample_rate) + c->aframe->nb_samples,
            CODEC_A_OUT_RATE, c->adec->sample_rate, AV_ROUND_UP);
        if (out_samples <= 0)
            continue;
        if (av_samples_alloc(&out, NULL, CODEC_A_OUT_CH, out_samples,
                             AV_SAMPLE_FMT_S16, 0) < 0)
            continue;
        int converted = swr_convert(c->swr, &out, out_samples,
            (const uint8_t **)c->aframe->data, c->aframe->nb_samples);
        if (converted > 0)
            codec_audio_emit(c, (const int16_t *)out, (size_t)converted);
        av_freep(&out);
    }
}

static void codec_audio_drain(AromaVideoCodec *c)
{
    if (!c->adec)
        return;
    avcodec_send_packet(c->adec, NULL);
    for (;;) {
        int rc = avcodec_receive_frame(c->adec, c->aframe);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0)
            break;
        uint8_t *out = NULL;
        int out_samples = av_rescale_rnd(
            swr_get_delay(c->swr, c->adec->sample_rate) + c->aframe->nb_samples,
            CODEC_A_OUT_RATE, c->adec->sample_rate, AV_ROUND_UP);
        if (out_samples <= 0)
            continue;
        if (av_samples_alloc(&out, NULL, CODEC_A_OUT_CH, out_samples,
                             AV_SAMPLE_FMT_S16, 0) < 0)
            continue;
        int converted = swr_convert(c->swr, &out, out_samples,
            (const uint8_t **)c->aframe->data, c->aframe->nb_samples);
        if (converted > 0)
            codec_audio_emit(c, (const int16_t *)out, (size_t)converted);
        av_freep(&out);
    }
}

static void codec_audio_flush(AromaVideoCodec *c)
{
    if (c->adec)
        avcodec_flush_buffers(c->adec);
    aroma_audio_out_flush();
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
    // Audio is best-effort: video still plays if there is no audio stream
    // or the audio decoder fails to open.
    codec_audio_setup(c);
    aroma_audio_out_set_muted(c->muted);
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
    c->astream = -1;
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
    const char *open_path = path;
#ifdef __ANDROID__
    {
        const char *cached = aroma_video_android_cache_path(path);
        if (cached && cached[0])
            open_path = cached;
    }
#endif
    if (avformat_open_input(&c->fmt, open_path, NULL, NULL) < 0) {
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
        // Interleaved audio packets are decoded now so A/V stay in sync;
        // output is dropped when muted or when no sink is open.
        if (c->pkt->stream_index == c->astream && c->adec) {
            AVPacket *apkt = av_packet_clone(c->pkt);
            av_packet_unref(c->pkt);
            if (apkt) {
                codec_audio_decode_packet(c, apkt);
                av_packet_free(&apkt);
            }
            continue;
        }
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
    codec_audio_flush(c);
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
            codec_audio_drain(c);
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
    codec_audio_flush(c);
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

#endif // AROMA_HAS_FFMPEG

#ifdef __ANDROID__
// Resolve an APK asset (or plain file path) to a real filesystem path via
// the platform cache dir, trying full path, basename, and assets/-stripped
// variants to match the image/incense loaders.
#include "../backends/platforms/aroma_platform_interface.h"
const char *aroma_video_android_cache_path(const char *name)
{
    static char out[1024];
    if (!name || !name[0])
        return NULL;
    FILE *probe = fopen(name, "rb");
    if (probe) {
        fclose(probe);
        return name;
    }
    AromaPlatformInterface *plat = aroma_get_platform_interface();
    if (!plat || !plat->android_asset_cache_path)
        return NULL;
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    char stripped[1024];
    const char *extra = NULL;
    if (strncmp(name, "assets/", 7) == 0) {
        strncpy(stripped, name + 7, sizeof(stripped) - 1);
        stripped[sizeof(stripped) - 1] = '\0';
        extra = stripped;
    }
    const char *cands[3] = { name, base, extra };
    for (int i = 0; i < 3; i++) {
        const char *nm = cands[i];
        if (!nm || !nm[0])
            continue;
        const char *cached = plat->android_asset_cache_path(nm);
        if (cached && cached[0]) {
            strncpy(out, cached, sizeof(out) - 1);
            out[sizeof(out) - 1] = '\0';
            return out;
        }
    }
    return NULL;
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
    // pl_mpeg's PLM_PUT_PIXEL only writes the RGB bytes; the alpha byte
    // is left untouched, so a freshly realloc'd buffer leaves whatever
    // garbage (or zeros, on web) was in memory and the upload comes out
    // fully transparent. Force opaque alpha like every other backend.
    for (size_t i = 3; i < need; i += 4)
        c->tmp[i] = 0xFF;
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

static void codec_plm_audio(plm_t *plm, plm_samples_t *samples, void *user)
{
    (void)plm;
    AromaVideoCodec *c = (AromaVideoCodec *)user;
    if (!samples || !c || c->muted)
        return;
    // pl_mpeg emits float interleaved stereo at the stream rate (often
    // 44100 but also 48000/32000/...). Playing 48kHz content through a
    // 44.1kHz sink sounds wrong (slow/pitch-dropped), so open the sink at
    // the real rate instead of assuming.
    int rate = plm ? plm_get_samplerate(plm) : 0;
    if (rate != 44100 && rate != 48000 && rate != 32000 &&
        rate != 22050 && rate != 24000 && rate != 16000)
        rate = 44100;
    int count = (int)samples->count;
    if (count <= 0)
        return;
    if (!c->audio_opened) {
        LOG_INFO("aroma_video: pl_mpeg audio %dHz stereo", rate);
        if (!aroma_audio_out_open(rate, 2))
            return;
        c->audio_opened = true;
    }
    static int16_t s16[PLM_AUDIO_SAMPLES_PER_FRAME * 2];
    int n = count;
    if (n > PLM_AUDIO_SAMPLES_PER_FRAME)
        n = PLM_AUDIO_SAMPLES_PER_FRAME;
    for (int i = 0; i < n * 2; i++) {
        float f = samples->interleaved[i];
        if (f > 1.0f)
            f = 1.0f;
        else if (f < -1.0f)
            f = -1.0f;
        s16[i] = (int16_t)(f * 32767.0f);
    }
    aroma_audio_out_write(s16, (size_t)n);
    (void)rate;
}

static AromaVideoCodec *codec_open_plm_file(const char *path,
                                            AromaVideoCodecFrameCb cb,
                                            void *user,
                                            char *err, size_t errlen)
{
    // Android APK assets are invisible to fopen: resolve via the cache dir
    // (tries full path, basename, and assets/-stripped variants).
    const char *open_path = path;
#ifdef __ANDROID__
    static char s_cache_buf[1024];
    {
        const char *cached = NULL;
        // Local forward declaration to avoid pulling platform headers here.
        extern const char *aroma_video_android_cache_path(const char *name);
        cached = aroma_video_android_cache_path(path);
        if (cached && cached[0]) {
            strncpy(s_cache_buf, cached, sizeof(s_cache_buf) - 1);
            s_cache_buf[sizeof(s_cache_buf) - 1] = '\0';
            open_path = s_cache_buf;
        }
    }
#endif
    plm_t *plm = plm_create_with_filename(open_path);
    if (!plm) {
        set_err(err, errlen, "cannot open file");
        return NULL;
    }
    plm_set_audio_enabled(plm, 1);
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
    plm_set_audio_decode_callback(plm, codec_plm_audio, c);
    aroma_audio_out_set_muted(c->muted);
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
    plm_set_audio_enabled(plm, 1);
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
    plm_set_audio_decode_callback(plm, codec_plm_audio, c);
    aroma_audio_out_set_muted(c->muted);
    codec_plm_refresh(c);
    return c;
}

#endif // CODEC_HAS_PLM

#ifdef __ANDROID__
// ---- Android MediaCodec backend (MP4/H.264/H.265/VP8/VP9, no FFmpeg) ----
static void android_yuv420_to_rgba(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                                   int stride, int slice_h,
                                   uint8_t *rgba, int width, int height,
                                   bool semi_planar, bool nv21)
{
    for (int j = 0; j < height; j++) {
        int y_row = (j < slice_h) ? j * stride : (slice_h - 1) * stride;
        int uv_row = (j / 2) * stride;
        for (int i = 0; i < width; i++) {
            int yy = y[y_row + i];
            int uu, vv;
            if (semi_planar) {
                const uint8_t *uv = u; // interleaved plane
                int o = uv_row + (i / 2) * 2;
                if (nv21) { vv = uv[o]; uu = uv[o + 1]; }
                else { uu = uv[o]; vv = uv[o + 1]; }
            } else {
                int o = (j / 2) * (stride / 2) + (i / 2);
                uu = u[o];
                vv = v[o];
            }
            int c = yy - 16;
            int d = uu - 128;
            int e = vv - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            uint8_t *p = rgba + ((size_t)j * (size_t)width + (size_t)i) * 4u;
            p[0] = (uint8_t)r;
            p[1] = (uint8_t)g;
            p[2] = (uint8_t)b;
            p[3] = 255;
        }
    }
}

static bool android_find_track(AMediaExtractor *ex, const char *prefix, int *out_idx,
                               char *mime_out, size_t mime_len)
{
    size_t n = AMediaExtractor_getTrackCount(ex);
    for (size_t i = 0; i < n; i++) {
        AMediaFormat *f = AMediaExtractor_getTrackFormat(ex, i);
        if (!f)
            continue;
        const char *mime = NULL;
        bool has = AMediaFormat_getString(f, "mime", &mime);
        bool match = has && mime && strncmp(mime, prefix, strlen(prefix)) == 0;
        if (match) {
            if (out_idx)
                *out_idx = (int)i;
            if (mime_out && mime_len > 0) {
                strncpy(mime_out, mime, mime_len - 1);
                mime_out[mime_len - 1] = '\0';
            }
            AMediaFormat_delete(f);
            return true;
        }
        AMediaFormat_delete(f);
    }
    return false;
}

void aroma_video_android_close(AromaVideoCodec *c);

static AromaVideoCodec *codec_open_android_file(const char *path,
                                               AromaVideoCodecFrameCb cb,
                                               void *user,
                                               char *err, size_t errlen)
{
    const char *open_path = path;
    {
        const char *cached = aroma_video_android_cache_path(path);
        if (cached && cached[0])
            open_path = cached;
    }
    int vfd = -1;
    AMediaExtractor *vex = android_extractor_for(open_path, &vfd);
    if (!vex) {
        set_err(err, errlen, "cannot open file (unsupported container?)");
        return NULL;
    }
    int vtrack = -1;
    char vmime[64] = { 0 };
    if (!android_find_track(vex, "video/", &vtrack, vmime, sizeof(vmime))) {
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        LOG_ERROR("aroma_video: mediacodec found no video track in '%s'", open_path);
        set_err(err, errlen, "no video track found");
        return NULL;
    }
    LOG_INFO("aroma_video: mediacodec video track %d mime=%s", vtrack, vmime);
    AMediaFormat *vfmt = AMediaExtractor_getTrackFormat(vex, (size_t)vtrack);
    if (!vfmt) {
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        set_err(err, errlen, "cannot read video format");
        return NULL;
    }
    int32_t w = 0, h = 0, fps = 0;
    int64_t dur_us = 0;
    AMediaFormat_getInt32(vfmt, "width", &w);
    AMediaFormat_getInt32(vfmt, "height", &h);
    AMediaFormat_getInt32(vfmt, "frame-rate", &fps);
    AMediaFormat_getInt64(vfmt, "durationUs", &dur_us);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        AMediaFormat_delete(vfmt);
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        set_err(err, errlen, "unsupported video dimensions");
        return NULL;
    }
    AMediaCodec *vdec = AMediaCodec_createDecoderByType(vmime);
    if (!vdec) {
        AMediaFormat_delete(vfmt);
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        set_err(err, errlen, "no system decoder for this video");
        return NULL;
    }
    if (AMediaCodec_configure(vdec, vfmt, NULL, NULL, 0) != AMEDIA_OK ||
        AMediaCodec_start(vdec) != AMEDIA_OK) {
        AMediaCodec_delete(vdec);
        AMediaFormat_delete(vfmt);
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        LOG_ERROR("aroma_video: mediacodec configure/start failed mime=%s %dx%d", vmime, (int)w, (int)h);
        set_err(err, errlen, "cannot start video decoder");
        return NULL;
    }
    LOG_INFO("aroma_video: mediacodec decoder started %dx%d dur=%.2fs",
             (int)w, (int)h,
             dur_us > 0 ? (double)dur_us / 1000000.0 : 0.0);
    AMediaFormat_delete(vfmt);
    AMediaExtractor_selectTrack(vex, (size_t)vtrack);

    // Optional audio track on a second extractor (keeps video seeking simple).
    AMediaExtractor *aex = NULL;
    AMediaCodec *adec = NULL;
    int afd = -1;
    int atrack = -1;
    int a_rate = 0, a_ch = 0;
    {
        AMediaExtractor *ex = android_extractor_for(open_path, &afd);
        if (ex) {
            char amime[64] = { 0 };
            int idx = -1;
            if (android_find_track(ex, "audio/", &idx, amime, sizeof(amime))) {
                AMediaFormat *af = AMediaExtractor_getTrackFormat(ex, (size_t)idx);
                if (af) {
                    AMediaCodec *ac = AMediaCodec_createDecoderByType(amime);
                    if (ac && AMediaCodec_configure(ac, af, NULL, NULL, 0) == AMEDIA_OK &&
                        AMediaCodec_start(ac) == AMEDIA_OK) {
                        AMediaExtractor_selectTrack(ex, (size_t)idx);
                        int32_t sr = 0, ch = 0;
                        AMediaFormat_getInt32(af, "sample-rate", &sr);
                        AMediaFormat_getInt32(af, "channel-count", &ch);
                        aex = ex;
                        adec = ac;
                        atrack = idx;
                        a_rate = (int)sr;
                        a_ch = (int)ch;
                        ex = NULL; // owned by codec now (fd too)
                    } else {
                        if (ac)
                            AMediaCodec_delete(ac);
                    }
                    AMediaFormat_delete(af);
                }
            }
            if (ex) {
                AMediaExtractor_delete(ex);
                ex = NULL;
            }
            if (!adec && afd >= 0) {
                close(afd);
                afd = -1;
            }
        }
    }

    AromaVideoCodec *c = (AromaVideoCodec *)calloc(1, sizeof(AromaVideoCodec));
    if (!c) {
        AMediaCodec_stop(vdec);
        AMediaCodec_delete(vdec);
        AMediaExtractor_delete(vex);
        if (vfd >= 0)
            close(vfd);
        if (adec) {
            AMediaCodec_stop(adec);
            AMediaCodec_delete(adec);
        }
        if (aex)
            AMediaExtractor_delete(aex);
        if (afd >= 0)
            close(afd);
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    c->cb = cb;
    c->user = user;
    c->use_android = true;
    c->aex_v = vex;
    c->aex_a = aex;
    c->vdec = vdec;
    c->adec_mc = adec;
    c->a_fd_v = vfd;
    c->a_fd_a = afd;
    c->a_width = (int)w;
    c->a_height = (int)h;
    c->a_rw = (int)w;
    c->a_rh = (int)h;
    c->a_arate = a_rate;
    c->a_ach = a_ch;
    c->a_clock = 0.0;
    c->a_base = 0.0;
    c->duration = dur_us > 0 ? (double)dur_us / 1000000.0 : 0.0;
    c->fps = (fps > 0 && fps < 240) ? (double)fps : 0.0;
    c->backend = "mediacodec";
    (void)atrack;
    aroma_audio_out_set_muted(c->muted);
    return c;
}

static bool android_drain_video(AromaVideoCodec *c, double target, bool *advanced)
{
    for (int i = 0; i < 8; i++) {
        AMediaCodecBufferInfo info;
        ssize_t out = AMediaCodec_dequeueOutputBuffer(c->vdec, &info, 0);
        if (out == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
            return true;
        if (out == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *f = AMediaCodec_getOutputFormat(c->vdec);
            if (f) {
                int32_t w = 0, h = 0;
                AMediaFormat_getInt32(f, "width", &w);
                AMediaFormat_getInt32(f, "height", &h);
                if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                    c->a_width = (int)w;
                    c->a_height = (int)h;
                    c->a_rw = (int)w;
                    c->a_rh = (int)h;
                }
                AMediaFormat_delete(f);
            }
            continue;
        }
        if (out == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
            continue;
        if (out < 0)
            return true;
        size_t sz = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(c->vdec, (size_t)out, &sz);
        bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        if (buf && info.size > 0 && c->a_width > 0 && c->a_height > 0) {
            int32_t stride = c->a_width, slice = c->a_height, cfmt = 21;
            AMediaFormat *f = AMediaCodec_getOutputFormat(c->vdec);
            if (f) {
                AMediaFormat_getInt32(f, "stride", &stride);
                AMediaFormat_getInt32(f, "slice-height", &slice);
                AMediaFormat_getInt32(f, "color-format", &cfmt);
                AMediaFormat_delete(f);
            }
            if (stride < c->a_width)
                stride = c->a_width;
            if (slice < c->a_height)
                slice = c->a_height;
            size_t need = (size_t)c->a_width * (size_t)c->a_height * 4u;
            if (c->a_rgba_size < need) {
                uint8_t *nb = (uint8_t *)realloc(c->a_rgba, need);
                if (nb) {
                    c->a_rgba = nb;
                    c->a_rgba_size = need;
                }
            }
            if (c->a_rgba && c->a_rgba_size >= need) {
                // Flexible YUV420: planar (Y+U+V) or semi-planar (Y+UV).
                // NV21 vs NV12 is decided by color-format; default to NV12
                // with an NV21 fallback impossible to detect cheaply, so
                // handle the common 21 (semi-planar) as NV12.
                bool semi = (cfmt == 21 || cfmt == 39 || cfmt == 2130708361);
                const uint8_t *y = buf;
                const uint8_t *u = NULL, *v = NULL;
                size_t y_size = (size_t)stride * (size_t)slice;
                if (semi) {
                    u = buf + y_size;
                } else {
                    size_t uv_stride = (size_t)(stride / 2);
                    size_t uv_h = (size_t)(slice / 2);
                    u = buf + y_size;
                    v = u + uv_stride * uv_h;
                }
                android_yuv420_to_rgba(y, u, v, stride, slice, c->a_rgba,
                                       c->a_width, c->a_height, semi, false);
                double pts = (double)info.presentationTimeUs / 1000000.0;
                c->a_clock = pts;
                if (c->cb)
                    c->cb(c->a_rgba, c->a_width, c->a_height, pts, c->user);
                if (advanced)
                    *advanced = true;
                if (pts >= target - 1e-6) {
                    AMediaCodec_releaseOutputBuffer(c->vdec, (size_t)out, false);
                    return true;
                }
            }
        }
        AMediaCodec_releaseOutputBuffer(c->vdec, (size_t)out, false);
        if (eos) {
            c->a_v_eos = true;
            return true;
        }
    }
    return true;
}

static void android_drain_audio(AromaVideoCodec *c, double target)
{
    if (!c->adec_mc || !c->aex_a)
        return;
    for (int i = 0; i < 8; i++) {
        AMediaCodecBufferInfo info;
        ssize_t out = AMediaCodec_dequeueOutputBuffer(c->adec_mc, &info, 0);
        if (out == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
            return;
        if (out == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *f = AMediaCodec_getOutputFormat(c->adec_mc);
            if (f) {
                int32_t sr = 0, ch = 0;
                AMediaFormat_getInt32(f, "sample-rate", &sr);
                AMediaFormat_getInt32(f, "channel-count", &ch);
                if (sr > 0)
                    c->a_arate = (int)sr;
                if (ch == 1 || ch == 2)
                    c->a_ach = (int)ch;
                AMediaFormat_delete(f);
            }
            continue;
        }
        if (out == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
            continue;
        if (out < 0)
            return;
        size_t sz = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(c->adec_mc, (size_t)out, &sz);
        bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        if (buf && info.size > 0) {
            int rate = c->a_arate > 0 ? c->a_arate : 44100;
            int ch = (c->a_ach == 1 || c->a_ach == 2) ? c->a_ach : 2;
            if (!c->audio_opened) {
                LOG_INFO("aroma_video: android audio %dHz ch%d", rate, ch);
                if (aroma_audio_out_open(rate, ch))
                    c->audio_opened = true;
            }
            size_t frames = (size_t)info.size / ((size_t)ch * sizeof(int16_t));
            if (frames > 0)
                codec_audio_emit(c, (const int16_t *)(buf + info.offset), frames);
            (void)target;
        }
        AMediaCodec_releaseOutputBuffer(c->adec_mc, (size_t)out, false);
        if (eos) {
            c->a_a_eos = true;
            return;
        }
    }
}

static bool android_feed_video(AromaVideoCodec *c)
{
    if (c->a_v_eos)
        return true;
    ssize_t idx = AMediaCodec_dequeueInputBuffer(c->vdec, 0);
    if (idx < 0)
        return true;
    size_t cap = 0;
    uint8_t *dst = AMediaCodec_getInputBuffer(c->vdec, (size_t)idx, &cap);
    if (!dst)
        return true;
    ssize_t n = AMediaExtractor_readSampleData(c->aex_v, dst, cap);
    if (n < 0) {
        AMediaCodec_queueInputBuffer(c->vdec, (size_t)idx, 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        c->a_v_eos = true;
        return true;
    }
    int64_t t = AMediaExtractor_getSampleTime(c->aex_v);
    uint32_t flags = 0;
    AMediaCodec_queueInputBuffer(c->vdec, (size_t)idx, 0, (size_t)n,
                                 (uint64_t)t, flags);
    AMediaExtractor_advance(c->aex_v);
    return true;
}

static void android_feed_audio(AromaVideoCodec *c)
{
    if (!c->adec_mc || !c->aex_a || c->a_a_eos)
        return;
    for (int i = 0; i < 4; i++) {
        ssize_t idx = AMediaCodec_dequeueInputBuffer(c->adec_mc, 0);
        if (idx < 0)
            return;
        size_t cap = 0;
        uint8_t *dst = AMediaCodec_getInputBuffer(c->adec_mc, (size_t)idx, &cap);
        if (!dst)
            return;
        ssize_t n = AMediaExtractor_readSampleData(c->aex_a, dst, cap);
        if (n < 0) {
            AMediaCodec_queueInputBuffer(c->adec_mc, (size_t)idx, 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            c->a_a_eos = true;
            return;
        }
        int64_t t = AMediaExtractor_getSampleTime(c->aex_a);
        AMediaCodec_queueInputBuffer(c->adec_mc, (size_t)idx, 0, (size_t)n,
                                     (uint64_t)t, 0);
        AMediaExtractor_advance(c->aex_a);
    }
}

static bool codec_step_android(AromaVideoCodec *c, double dt)
{
    if (dt < 0.0)
        dt = 0.0;
    if (dt > 0.5)
        dt = 0.5;
    double target = c->a_clock + dt;
    // When looping, EOS was already handled by seeking to 0 on the
    // previous step that observed it.
    for (int iter = 0; iter < 32; iter++) {
        bool advanced = false;
        android_feed_video(c);
        android_feed_audio(c);
        android_drain_video(c, target, &advanced);
        android_drain_audio(c, target);
        if (c->a_clock >= target - 1e-6) {
            c->a_clock = target;
            return true;
        }
        if (c->a_v_eos) {
            if (c->loop) {
                AMediaExtractor_seekTo(c->aex_v, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                AMediaCodec_flush(c->vdec);
                if (c->aex_a) {
                    AMediaExtractor_seekTo(c->aex_a, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                    AMediaCodec_flush(c->adec_mc);
                }
                aroma_audio_out_flush();
                c->a_v_eos = false;
                c->a_a_eos = false;
                c->a_clock = 0.0;
                target = dt;
                continue;
            }
            c->ended = true;
            c->a_clock = target;
            return true;
        }
        if (advanced)
            continue;
    }
    c->a_clock = target;
    return true;
}

static bool codec_seek_android(AromaVideoCodec *c, double t)
{
    if (t < 0.0)
        t = 0.0;
    if (c->duration > 0.0 && t > c->duration)
        t = c->duration;
    int64_t us = (int64_t)(t * 1000000.0);
    AMediaExtractor_seekTo(c->aex_v, us, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
    AMediaCodec_flush(c->vdec);
    if (c->aex_a && c->adec_mc) {
        AMediaExtractor_seekTo(c->aex_a, us, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
        AMediaCodec_flush(c->adec_mc);
    }
    aroma_audio_out_flush();
    c->a_v_eos = false;
    c->a_a_eos = false;
    c->ended = false;
    c->a_clock = t;
    // Prime one frame so the widget shows the seek target immediately.
    for (int i = 0; i < 16; i++) {
        bool advanced = false;
        android_feed_video(c);
        android_drain_video(c, t, &advanced);
        if (advanced)
            break;
        if (c->a_v_eos)
            break;
    }
    return true;
}

static void codec_rewind_android(AromaVideoCodec *c)
{
    AMediaExtractor_seekTo(c->aex_v, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
    AMediaCodec_flush(c->vdec);
    if (c->aex_a && c->adec_mc) {
        AMediaExtractor_seekTo(c->aex_a, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
        AMediaCodec_flush(c->adec_mc);
    }
    aroma_audio_out_flush();
    c->a_v_eos = false;
    c->a_a_eos = false;
    c->ended = false;
    c->a_clock = 0.0;
}

void aroma_video_android_close(AromaVideoCodec *c)
{
    if (!c || !c->use_android)
        return;
    if (c->vdec) {
        AMediaCodec_stop(c->vdec);
        AMediaCodec_delete(c->vdec);
        c->vdec = NULL;
    }
    if (c->adec_mc) {
        AMediaCodec_stop(c->adec_mc);
        AMediaCodec_delete(c->adec_mc);
        c->adec_mc = NULL;
    }
    if (c->aex_v) {
        AMediaExtractor_delete(c->aex_v);
        c->aex_v = NULL;
    }
    if (c->aex_a) {
        AMediaExtractor_delete(c->aex_a);
        c->aex_a = NULL;
    }
    if (c->a_fd_v >= 0) {
        close(c->a_fd_v);
        c->a_fd_v = -1;
    }
    if (c->a_fd_a >= 0) {
        close(c->a_fd_a);
        c->a_fd_a = -1;
    }
    free(c->a_rgba);
    c->a_rgba = NULL;
    c->a_rgba_size = 0;
    c->use_android = false;
}

#endif // __ANDROID__

AromaVideoCodec *aroma_video_codec_open_file(const char *path,
                                             AromaVideoCodecFrameCb cb,
                                             void *user,
                                             char *err, size_t errlen)
{
    if (!path || !path[0]) {
        set_err(err, errlen, "empty video path");
        return NULL;
    }
#ifdef __ANDROID__
    // Prefer the system decoder on Android: handles MP4/H.264/H.265/VP9.
    // Falls back to pl_mpeg (MPEG-1 .mpg) when the container/codec is
    // unsupported, preserving the old behavior for .mpg assets.
    char android_err[256] = { 0 };
    {
        AromaVideoCodec *c = codec_open_android_file(path, cb, user,
                                                     android_err, sizeof(android_err));
        if (c)
            return c;
    }
#endif
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
    {
        char plm_err[256] = { 0 };
        AromaVideoCodec *c = codec_open_plm_file(path, cb, user, plm_err, sizeof(plm_err));
        if (c)
            return c;
#ifdef __ANDROID__
        // Report the MediaCodec error for non-MPEG files: pl_mpeg's
        // "no MPEG-1 video stream" just hides the real failure for MP4.
        {
            size_t nlen = strlen(path);
            bool is_mpg = nlen > 4 &&
                (strcasecmp(path + nlen - 4, ".mpg") == 0 ||
                 strcasecmp(path + nlen - 5, ".mpeg") == 0);
            if (!is_mpg && android_err[0]) {
                strncpy(err, android_err, errlen - 1);
                err[errlen - 1] = '\0';
                return NULL;
            }
        }
#endif
        if (err && errlen > 0) {
            strncpy(err, plm_err, errlen - 1);
            err[errlen - 1] = '\0';
        }
        return NULL;
    }
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
#ifdef __ANDROID__
    aroma_video_android_close(c);
#endif
    aroma_audio_out_flush();
    free(c);
}

bool aroma_video_codec_step(AromaVideoCodec *c, double dt)
{
    if (!c)
        return false;
    if (c->ended)
        return true;
#ifdef __ANDROID__
    if (c->use_android)
        return codec_step_android(c, dt);
#endif
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
#ifdef __ANDROID__
    if (c->use_android)
        return codec_seek_android(c, t);
#endif
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg)
        return codec_seek_ffmpeg(c, t);
#endif
#if CODEC_HAS_PLM
    if (c->plm) {
        if (c->duration > 0.0 && t > c->duration)
            t = c->duration;
        aroma_audio_out_flush();
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
#ifdef __ANDROID__
    if (c->use_android) {
        codec_rewind_android(c);
        return;
    }
#endif
#ifdef AROMA_HAS_FFMPEG
    if (c->use_ffmpeg) {
        codec_do_rewind(c);
        return;
    }
#endif
#if CODEC_HAS_PLM
    if (c->plm) {
        plm_rewind(c->plm);
        aroma_audio_out_flush();
    }
#endif
}
