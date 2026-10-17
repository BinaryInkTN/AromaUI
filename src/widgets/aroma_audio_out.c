#include "aroma_audio_out.h"
#include "core/aroma_logger.h"

#include <stdlib.h>
#include <string.h>

#if !defined(ESP32) && !defined(__EMSCRIPTEN__)
#define AUDIO_THREADED 1
#include <pthread.h>
#include <time.h>
#else
#define AUDIO_THREADED 0
#endif

// Fixed PCM contract for the FFmpeg/pl_mpeg paths: 44.1kHz stereo s16.
// (The Android MediaCodec path opens the sink at the stream's native rate;
// see codec_audio_emit / android_drain_audio.)
#define OUT_RATE 44100
#define OUT_CH 2

// 2 seconds of stereo s16 AT_out: absorbs bursty decoder delivery so the
// sink thread always has realtime-paced data (no underrun gaps).
#define RING_FRAMES (44100u * 2u)

static bool g_muted = false;

#if AUDIO_THREADED

typedef struct {
    int16_t *buf;       // interleaved s16, capacity cap_frames * ch
    size_t cap_frames;
    size_t head;        // write index (frames)
    size_t tail;        // read index (frames)
    size_t used;        // frames currently queued
    int rate;
    int ch;
    bool open;
    bool paused;
    bool quit;
    bool thread_on;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} AudioCore;

static AudioCore g_core = { 0 };
static bool g_lock_init = false;
// Serializes sink access: the audio thread's writes vs flush() calls from
// the UI thread (pause/seek). Open/close don't need it (thread stopped).
static pthread_mutex_t g_sink_lock;
static bool g_sink_lock_init = false;

static void core_lock_init(void)
{
    if (!g_lock_init) {
        pthread_mutex_init(&g_core.lock, NULL);
        pthread_cond_init(&g_core.cond, NULL);
        g_lock_init = true;
    }
    if (!g_sink_lock_init) {
        pthread_mutex_init(&g_sink_lock, NULL);
        g_sink_lock_init = true;
    }
}

// ---- platform sink: runs ONLY on the audio thread ----

#if defined(__ANDROID__)

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

static SLObjectItf s_engine;
static SLObjectItf s_mix;
static SLObjectItf s_player;
static SLPlayItf s_play;
static SLVolumeItf s_vol;
static SLAndroidSimpleBufferQueueItf s_queue;
static int s_rate;
static int s_ch;

static void sink_teardown(void)
{
    if (s_player) {
        (*s_player)->Destroy(s_player);
        s_player = NULL;
        s_play = NULL;
        s_vol = NULL;
        s_queue = NULL;
    }
    if (s_mix) {
        (*s_mix)->Destroy(s_mix);
        s_mix = NULL;
    }
    if (s_engine) {
        (*s_engine)->Destroy(s_engine);
        s_engine = NULL;
    }
    s_rate = 0;
    s_ch = 0;
}

static bool sink_open(int rate, int ch)
{
    if (rate <= 0 || (ch != 1 && ch != 2))
        return false;
    if (s_player && s_rate == rate && s_ch == ch)
        return true;
    sink_teardown();
    SLresult r;
    if ((r = slCreateEngine(&s_engine, 0, NULL, 0, NULL, NULL)) != SL_RESULT_SUCCESS || !s_engine)
        goto fail;
    if ((r = (*s_engine)->Realize(s_engine, SL_BOOLEAN_FALSE)) != SL_RESULT_SUCCESS)
        goto fail;
    SLEngineItf engine = NULL;
    if ((r = (*s_engine)->GetInterface(s_engine, SL_IID_ENGINE, &engine)) != SL_RESULT_SUCCESS || !engine)
        goto fail;
    if ((r = (*engine)->CreateOutputMix(engine, &s_mix, 0, NULL, NULL)) != SL_RESULT_SUCCESS || !s_mix)
        goto fail;
    if ((r = (*s_mix)->Realize(s_mix, SL_BOOLEAN_FALSE)) != SL_RESULT_SUCCESS)
        goto fail;
    {
        SLDataLocator_AndroidSimpleBufferQueue loc_buf = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 8 };
        SLDataFormat_PCM fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.formatType = SL_DATAFORMAT_PCM;
        fmt.numChannels = (SLuint32)ch;
        fmt.samplesPerSec = (SLuint32)rate * 1000u;
        fmt.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
        fmt.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
        fmt.channelMask = (ch == 2) ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT)
                                    : SL_SPEAKER_FRONT_CENTER;
        fmt.endianness = SL_BYTEORDER_LITTLEENDIAN;
        SLDataSource src = { &loc_buf, &fmt };
        SLDataLocator_OutputMix loc_mix = { SL_DATALOCATOR_OUTPUTMIX, s_mix };
        SLDataSink sink = { &loc_mix, NULL };
        const SLInterfaceID ids[2] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
        if ((r = (*engine)->CreateAudioPlayer(engine, &s_player, &src, &sink, 2, ids, req)) != SL_RESULT_SUCCESS || !s_player)
            goto fail;
    }
    if ((r = (*s_player)->Realize(s_player, SL_BOOLEAN_FALSE)) != SL_RESULT_SUCCESS)
        goto fail;
    if ((r = (*s_player)->GetInterface(s_player, SL_IID_PLAY, &s_play)) != SL_RESULT_SUCCESS || !s_play)
        goto fail;
    (*s_player)->GetInterface(s_player, SL_IID_VOLUME, &s_vol);
    if ((r = (*s_player)->GetInterface(s_player, SL_IID_BUFFERQUEUE, &s_queue)) != SL_RESULT_SUCCESS || !s_queue)
        goto fail;
    if (s_vol)
        (*s_vol)->SetMute(s_vol, SL_BOOLEAN_FALSE);
    if ((r = (*s_play)->SetPlayState(s_play, SL_PLAYSTATE_PLAYING)) != SL_RESULT_SUCCESS)
        goto fail;
    s_rate = rate;
    s_ch = ch;
    return true;
fail:
    sink_teardown();
    return false;
}

static void sink_close(void) { sink_teardown(); }

static void sink_write_chunk(const int16_t *pcm, size_t frames, int ch)
{
    size_t done = 0;
    size_t stride = (size_t)ch * sizeof(int16_t);
    int tries = 0;
    while (done < frames && tries < 50) {
        size_t n = frames - done;
        if (n > 8192)
            n = 8192;
        SLresult r = (*s_queue)->Enqueue(s_queue, pcm + done * (size_t)ch,
                                        (SLuint32)(n * stride));
        if (r == SL_RESULT_SUCCESS) {
            done += n;
            tries = 0;
        } else {
            // Queue full (app ahead of realtime): wait a slice and retry
            // instead of dropping, which is what caused audible gaps.
            struct timespec ts = { 0, 2 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            tries++;
        }
    }
}

static void sink_flush(void)
{
    if (s_queue)
        (*s_queue)->Clear(s_queue);
}

#elif defined(__linux__) && defined(AROMA_HAS_ALSA)

#include <alsa/asoundlib.h>

static snd_pcm_t *s_pcm;
static int s_rate;
static int s_ch;

static bool sink_open(int rate, int ch)
{
    if (rate <= 0 || (ch != 1 && ch != 2))
        return false;
    if (s_pcm && s_rate == rate && s_ch == ch)
        return true;
    if (s_pcm) {
        snd_pcm_drop(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
    }
    snd_pcm_t *pcm = NULL;
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return false;
    snd_pcm_hw_params_t *hw = NULL;
    snd_pcm_hw_params_alloca(&hw);
    bool ok = snd_pcm_hw_params_any(pcm, hw) >= 0 &&
              snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0 &&
              snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE) >= 0 &&
              snd_pcm_hw_params_set_channels(pcm, hw, (unsigned)ch) >= 0;
    unsigned r = (unsigned)rate;
    ok = ok && snd_pcm_hw_params_set_rate_near(pcm, hw, &r, NULL) >= 0 &&
         snd_pcm_hw_params(pcm, hw) >= 0 && snd_pcm_prepare(pcm) >= 0;
    if (!ok) {
        snd_pcm_close(pcm);
        return false;
    }
    s_pcm = pcm;
    s_rate = rate;
    s_ch = ch;
    return true;
}

static void sink_close(void)
{
    if (s_pcm) {
        snd_pcm_drop(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
    }
    s_rate = 0;
    s_ch = 0;
}

static void sink_write_chunk(const int16_t *pcm, size_t frames, int ch)
{
    if (!s_pcm)
        return;
    size_t done = 0;
    while (done < frames) {
        snd_pcm_sframes_t rc = snd_pcm_writei(s_pcm, pcm + done * (size_t)ch, frames - done);
        if (rc == -EPIPE) {
            snd_pcm_prepare(s_pcm);
            continue;
        }
        if (rc < 0) {
            if (rc == -EAGAIN)
                continue;
            snd_pcm_prepare(s_pcm);
            return;
        }
        if (rc == 0)
            break;
        done += (size_t)rc;
    }
}

static void sink_flush(void)
{
    if (s_pcm) {
        snd_pcm_drop(s_pcm);
        snd_pcm_prepare(s_pcm);
    }
}

#else
#error "AUDIO_THREADED set on unsupported platform"
#endif

// ---- shared frontend: lock-free-ish SPSC ring + dedicated sink thread ----

static void *audio_thread_main(void *arg)
{
    (void)arg;
    static int16_t chunk[4096 * 2];
    for (;;) {
        pthread_mutex_lock(&g_core.lock);
        while (!g_core.quit &&
               (g_core.paused || g_muted || g_core.used == 0 || !g_core.open)) {
            // While muted/paused the producer drops or holds; anything
            // already queued is stale, so drop it for immediate effect.
            if ((g_core.paused || g_muted) && g_core.used > 0) {
                g_core.head = g_core.tail = g_core.used = 0;
            }
            pthread_cond_wait(&g_core.cond, &g_core.lock);
        }
        if (g_core.quit) {
            pthread_mutex_unlock(&g_core.lock);
            return NULL;
        }
        size_t n = g_core.used;
        if (n > 4096)
            n = 4096;
        size_t first = g_core.cap_frames - g_core.tail;
        if (first > n)
            first = n;
        memcpy(chunk, g_core.buf + g_core.tail * (size_t)g_core.ch, first * (size_t)g_core.ch * sizeof(int16_t));
        if (first < n) {
            memcpy(chunk + first * (size_t)g_core.ch, g_core.buf,
                   (n - first) * (size_t)g_core.ch * sizeof(int16_t));
        }
        int ch = g_core.ch;
        g_core.tail = (g_core.tail + n) % g_core.cap_frames;
        g_core.used -= n;
        pthread_mutex_unlock(&g_core.lock);
        pthread_mutex_lock(&g_sink_lock);
        sink_write_chunk(chunk, n, ch);
        pthread_mutex_unlock(&g_sink_lock);
    }
}

static void core_ensure_thread(void)
{
    if (g_core.thread_on)
        return;
    g_core.quit = false;
    if (pthread_create(&g_core.thread, NULL, audio_thread_main, NULL) == 0)
        g_core.thread_on = true;
}

// Stop the sink thread and wait for it. No lock held on entry; the sink
// itself is untouched (caller handles sink_close/open while no thread runs).
static void core_thread_stop(void)
{
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    bool was_on = g_core.thread_on;
    g_core.quit = true;
    g_core.open = false;
    g_core.head = g_core.tail = g_core.used = 0;
    pthread_cond_signal(&g_core.cond);
    pthread_mutex_unlock(&g_core.lock);
    if (was_on) {
        pthread_join(g_core.thread, NULL);
        pthread_mutex_lock(&g_core.lock);
        g_core.thread_on = false;
        g_core.quit = false;
        pthread_mutex_unlock(&g_core.lock);
    }
}

bool aroma_audio_out_open(int sample_rate_hz, int channels)
{
    if (sample_rate_hz <= 0 || (channels != 1 && channels != 2))
        return false;
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    bool same = g_core.open && g_core.rate == sample_rate_hz &&
                g_core.ch == channels && g_core.thread_on;
    pthread_mutex_unlock(&g_core.lock);
    if (same)
        return true;
    // New format (or first open): stop any running thread first so no one
    // is inside the old sink while we replace it, then open the sink,
    // then (re)start the thread.
    core_thread_stop();
    sink_close();
    // Sink opens on the caller thread: OpenSL/ALSA open is fast and
    // thread-safe, and a failed open must be reported synchronously.
    if (!sink_open(sample_rate_hz, channels))
        return false;
    pthread_mutex_lock(&g_core.lock);
    if (!g_core.buf) {
        g_core.buf = (int16_t *)malloc(RING_FRAMES * 2u * sizeof(int16_t));
        if (!g_core.buf) {
            pthread_mutex_unlock(&g_core.lock);
            sink_close();
            return false;
        }
        g_core.cap_frames = RING_FRAMES;
    }
    g_core.head = g_core.tail = g_core.used = 0;
    g_core.rate = sample_rate_hz;
    g_core.ch = channels;
    g_core.open = true;
    g_core.paused = false;
    core_ensure_thread();
    pthread_cond_signal(&g_core.cond);
    bool ok = g_core.thread_on;
    pthread_mutex_unlock(&g_core.lock);
    if (!ok)
        sink_close();
    return ok;
}

void aroma_audio_out_close(void)
{
    core_thread_stop();
    sink_close();
}

void aroma_audio_out_write(const int16_t *pcm, size_t frames)
{
    if (!pcm || frames == 0)
        return;
    // TEMP DEBUG: report PCM energy so an all-silent feed is visible on device.
    {
        static int s_dbg_writes = 0;
        if (s_dbg_writes < 3 || (s_dbg_writes % 200) == 0) {
            int peak = 0;
            size_t n = frames * (size_t)(g_core.ch > 0 ? g_core.ch : 2);
            for (size_t i = 0; i < n; i++) {
                int v = pcm[i];
                if (v < 0)
                    v = -v;
                if (v > peak)
                    peak = v;
            }
            LOG_INFO("aroma_video: pcm write#%d frames=%zu peak=%d", s_dbg_writes,
                     frames, peak);
        }
        s_dbg_writes++;
    }
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    if (!g_core.open || g_core.paused || g_muted || !g_core.thread_on) {
        pthread_mutex_unlock(&g_core.lock);
        return;
    }
    // Non-blocking push; on overflow drop the newest (decoder is
    // realtime-paced so this only happens on seeks - which flush first).
    size_t space = g_core.cap_frames - g_core.used;
    if (frames > space)
        frames = space;
    size_t first = g_core.cap_frames - g_core.head;
    if (first > frames)
        first = frames;
    memcpy(g_core.buf + g_core.head * (size_t)g_core.ch, pcm,
           first * (size_t)g_core.ch * sizeof(int16_t));
    if (first < frames) {
        memcpy(g_core.buf, pcm + first * (size_t)g_core.ch,
               (frames - first) * (size_t)g_core.ch * sizeof(int16_t));
    }
    g_core.head = (g_core.head + frames) % g_core.cap_frames;
    g_core.used += frames;
    pthread_cond_signal(&g_core.cond);
    pthread_mutex_unlock(&g_core.lock);
}

void aroma_audio_out_set_muted(bool muted)
{
    g_muted = muted;
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    if (muted) {
        g_core.head = g_core.tail = g_core.used = 0;
    }
    pthread_cond_signal(&g_core.cond);
    pthread_mutex_unlock(&g_core.lock);
}

bool aroma_audio_out_muted(void) { return g_muted; }

void aroma_audio_out_flush(void)
{
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    g_core.head = g_core.tail = g_core.used = 0;
    pthread_cond_signal(&g_core.cond);
    pthread_mutex_unlock(&g_core.lock);
    pthread_mutex_lock(&g_sink_lock);
    sink_flush();
    pthread_mutex_unlock(&g_sink_lock);
}

void aroma_audio_out_pause(bool pause)
{
    core_lock_init();
    pthread_mutex_lock(&g_core.lock);
    g_core.paused = pause;
    if (pause) {
        // Drop anything already queued so resume starts cleanly instead
        // of playing stale audio after a user-visible pause.
        g_core.head = g_core.tail = g_core.used = 0;
    }
    pthread_cond_signal(&g_core.cond);
    pthread_mutex_unlock(&g_core.lock);
    if (pause) {
        pthread_mutex_lock(&g_sink_lock);
        sink_flush();
        pthread_mutex_unlock(&g_sink_lock);
    }
}

#else // !AUDIO_THREADED — ESP32 / Emscripten: accept API, drop PCM.

// (Unchanged dummy implementation.)

static bool g_dummy_open = false;

bool aroma_audio_out_open(int sample_rate_hz, int channels)
{
    (void)sample_rate_hz;
    (void)channels;
    g_dummy_open = true;
    return true;
}
void aroma_audio_out_close(void) { g_dummy_open = false; }
void aroma_audio_out_write(const int16_t *pcm, size_t frames)
{
    (void)pcm;
    (void)frames;
}
void aroma_audio_out_set_muted(bool muted) { g_muted = muted; }
bool aroma_audio_out_muted(void) { return g_muted; }
void aroma_audio_out_flush(void) {}
void aroma_audio_out_pause(bool pause) { (void)pause; }

#endif // AUDIO_THREADED
