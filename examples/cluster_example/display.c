
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <glib.h>
#include <glib-unix.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <inttypes.h>
#include <time.h>


#define WIDTH        1280
#define HEIGHT       720
#define BPP          4
#define FRAME_SIZE   (WIDTH * HEIGHT * BPP)
#define FPS          30
#define FRAME_NS     (1000000000ULL / FPS)

#define SHM_FRAME    "/aroma_frame_shm"
#define SHM_EVENTS   "/aroma_events_shm"
#define SEM_FRAME    "/aroma_frame_ready"


typedef struct __attribute__((packed)) {
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t click;
} SharedEvents;


typedef struct {
    GstElement      *pipeline;
    GstElement      *appsrc;
    GMainLoop       *loop;

    int              frame_fd;
    uint8_t         *frame_pixels;

    int              ev_fd;
    SharedEvents    *ev_shm;

    sem_t           *frame_sem;
    bool             use_sem;

    pthread_t        feed_thread;
    volatile bool    running;

    uint64_t         frames_pushed;
    uint64_t         frames_dropped;
} AppState;

static AppState g_app = {0};


static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void log_info(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "[aroma_display] %s\n", buf);
    fflush(stdout);
}

static void log_err(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[aroma_display] ERROR: %s\n", buf);
    fflush(stderr);
}


static int shm_open_retry(const char *name, int oflag, mode_t mode,
                          int retries, int sleep_ms)
{
    for (int i = 0; i < retries; i++) {
        int fd = shm_open(name, oflag, mode);
        if (fd >= 0) return fd;
        if (errno != ENOENT) { perror("shm_open"); return -1; }
        log_info("Waiting for SHM %s ... (%d/%d)", name, i + 1, retries);
        usleep((useconds_t)sleep_ms * 1000);
    }
    log_err("SHM %s not available after %d retries", name, retries);
    return -1;
}

static bool shm_init(AppState *app)
{
    app->frame_fd = shm_open_retry(SHM_FRAME, O_RDONLY, 0, 30, 200);
    if (app->frame_fd < 0) return false;

    app->frame_pixels = mmap(NULL, FRAME_SIZE, PROT_READ,
                             MAP_SHARED, app->frame_fd, 0);
    if (app->frame_pixels == MAP_FAILED) {
        perror("mmap frame"); return false;
    }

    madvise(app->frame_pixels, FRAME_SIZE, MADV_SEQUENTIAL);

    app->ev_fd = shm_open_retry(SHM_EVENTS, O_RDWR, 0, 30, 200);
    if (app->ev_fd < 0) return false;

    app->ev_shm = mmap(NULL, sizeof(SharedEvents), PROT_READ | PROT_WRITE,
                       MAP_SHARED, app->ev_fd, 0);
    if (app->ev_shm == MAP_FAILED) {
        perror("mmap events"); return false;
    }

    app->frame_sem = sem_open(SEM_FRAME, 0);
    if (app->frame_sem == SEM_FAILED) {
        log_info("No producer semaphore found (%s), using timer fallback", SEM_FRAME);
        app->use_sem = false;
    } else {
        log_info("Producer semaphore found — using sync'd frame delivery");
        app->use_sem = true;
    }

    log_info("SHM attached — frame=%s events=%s", SHM_FRAME, SHM_EVENTS);
    return true;
}

static void shm_cleanup(AppState *app)
{
    if (app->frame_pixels && app->frame_pixels != MAP_FAILED) {
        munmap(app->frame_pixels, FRAME_SIZE);
        app->frame_pixels = NULL;
    }
    if (app->frame_fd >= 0)  { close(app->frame_fd);  app->frame_fd  = -1; }

    if (app->ev_shm && app->ev_shm != MAP_FAILED) {
        munmap(app->ev_shm, sizeof(SharedEvents));
        app->ev_shm = NULL;
    }
    if (app->ev_fd >= 0) { close(app->ev_fd); app->ev_fd = -1; }

    if (app->use_sem && app->frame_sem != SEM_FAILED) {
        sem_close(app->frame_sem);
        app->frame_sem = SEM_FAILED;
    }
}


static void write_event(AppState *app, int x, int y, int click)
{
    SharedEvents ev = { .mouse_x = x, .mouse_y = y, .click = click };
    __atomic_store_n(&app->ev_shm->mouse_x, ev.mouse_x, __ATOMIC_RELAXED);
    __atomic_store_n(&app->ev_shm->mouse_y, ev.mouse_y, __ATOMIC_RELAXED);
    __atomic_store_n(&app->ev_shm->click,   ev.click,   __ATOMIC_RELEASE);

    log_info("Event -> x=%d y=%d click=%d", x, y, click);
}


static GstPadProbeReturn nav_probe_cb(GstPad *pad, GstPadProbeInfo *info,
                                      gpointer user_data)
{
    (void)pad;
    AppState *app = (AppState *)user_data;

    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) != GST_EVENT_NAVIGATION)
        return GST_PAD_PROBE_OK;

    const GstStructure *s = gst_event_get_structure(event);
    if (!s) return GST_PAD_PROBE_OK;

    const gchar *ev_type = gst_structure_get_string(s, "event");
    if (!ev_type) return GST_PAD_PROBE_OK;

    gdouble x = 0.0, y = 0.0;
    gboolean got_x = gst_structure_get_double(s, "pointer_x", &x);
    gboolean got_y = gst_structure_get_double(s, "pointer_y", &y);

    double flipped_y = HEIGHT - y;

    if (g_strcmp0(ev_type, "mouse-move") == 0) {
        if (got_x && got_y)
            write_event(app, (int)x, (int)flipped_y, 0);

    } else if (g_strcmp0(ev_type, "mouse-button-press") == 0) {
        gint button = 1;
        if (!gst_structure_get_int(s, "button", &button))
            button = 1;
        log_info("Button press: %d", button);
        if (got_x && got_y && button == 1)
            write_event(app, (int)x, (int)flipped_y, 1);

    } else if (g_strcmp0(ev_type, "mouse-button-release") == 0) {
        gint button = 1;
        if (!gst_structure_get_int(s, "button", &button))
            button = 1;
        log_info("Button release: %d", button);
        if (got_x && got_y && button == 1)
            write_event(app, (int)x, (int)flipped_y, 2);
    }

    return GST_PAD_PROBE_OK;
}


static bool pipeline_init(AppState *app)
{
    GError *err = NULL;

    const gchar *pipeline_desc =
        "appsrc name=src "
        "  format=time "
        "  is-live=true "
        "  do-timestamp=true "
        "  caps=\"video/x-raw,format=RGBA,"
              "width=" G_STRINGIFY(WIDTH) ","
              "height=" G_STRINGIFY(HEIGHT) ","
              "framerate=" G_STRINGIFY(FPS) "/1\" "
        "! queue max-size-buffers=2 leaky=downstream "
        "! videoconvert "
        "! videoflip method=vertical-flip "
        "! autovideosink name=sink sync=false";

    app->pipeline = gst_parse_launch(pipeline_desc, &err);
    if (!app->pipeline) {
        log_err("gst_parse_launch: %s", err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    app->appsrc = gst_bin_get_by_name(GST_BIN(app->pipeline), "src");
    if (!app->appsrc) {
        log_err("Could not get appsrc element");
        return false;
    }

    GstPad *srcpad = gst_element_get_static_pad(app->appsrc, "src");
    gst_pad_add_probe(srcpad,
                      GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
                      nav_probe_cb, app, NULL);
    gst_object_unref(srcpad);

    g_object_set(app->appsrc,
                 "stream-type", 0,
                 "format",      GST_FORMAT_TIME,
                 "is-live",     TRUE,
                 "max-bytes",   (guint64)(FRAME_SIZE * 2),
                 NULL);

    log_info("Pipeline built OK");
    return true;
}

static void pipeline_cleanup(AppState *app)
{
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        if (app->appsrc) {
            gst_object_unref(app->appsrc);
            app->appsrc = NULL;
        }
        gst_object_unref(app->pipeline);
        app->pipeline = NULL;
    }
}


static void *feed_thread(void *arg)
{
    AppState *app = (AppState *)arg;
    GstClockTime pts = 0;
    uint64_t next_wake = monotonic_ns();

    log_info("Feed thread started (sem=%s)", app->use_sem ? "yes" : "no");

    while (app->running) {

        if (app->use_sem) {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 100000000LL;
            if (timeout.tv_nsec >= 1000000000LL) {
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000LL;
            }
            if (sem_timedwait(app->frame_sem, &timeout) != 0) {
                app->frames_dropped++;
                goto push_frame;
            }
        } else {
            next_wake += FRAME_NS;
            uint64_t now = monotonic_ns();
            if (next_wake > now) {
                struct timespec ts = {
                    .tv_sec  = (next_wake - now) / 1000000000ULL,
                    .tv_nsec = (next_wake - now) % 1000000000ULL
                };
                nanosleep(&ts, NULL);
            } else {
                next_wake = monotonic_ns();
                app->frames_dropped++;
            }
        }

push_frame:;
        GstBuffer *buf = gst_buffer_new_allocate(NULL, FRAME_SIZE, NULL);
        if (!buf) {
            log_err("gst_buffer_new_allocate failed");
            continue;
        }

        GstMapInfo map;
        if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
            log_err("gst_buffer_map failed");
            gst_buffer_unref(buf);
            continue;
        }

        memcpy(map.data, app->frame_pixels, FRAME_SIZE);
        gst_buffer_unmap(buf, &map);

        GST_BUFFER_PTS(buf)      = pts;
        GST_BUFFER_DURATION(buf) = FRAME_NS;
        pts += FRAME_NS;

        GstFlowReturn ret;
        g_signal_emit_by_name(app->appsrc, "push-buffer", buf, &ret);
        gst_buffer_unref(buf);

        if (ret != GST_FLOW_OK) {
            log_err("push-buffer returned %d — pipeline may be shutting down", ret);
            break;
        }

        app->frames_pushed++;

        if ((app->frames_pushed % (FPS * 10)) == 0) {
            log_info("Stats: pushed=%" PRIu64 " dropped=%" PRIu64,
                     app->frames_pushed, app->frames_dropped);
        }
    }

    log_info("Feed thread exiting");
    return NULL;
}


static gboolean bus_message_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
    (void)bus;
    AppState *app = (AppState *)data;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        log_info("EOS received");
        g_main_loop_quit(app->loop);
        break;

    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar  *dbg = NULL;
        gst_message_parse_error(msg, &err, &dbg);
        log_err("Pipeline error: %s (%s)", err->message, dbg ? dbg : "");
        g_clear_error(&err);
        g_free(dbg);
        g_main_loop_quit(app->loop);
        break;
    }

    case GST_MESSAGE_WARNING: {
        GError *err = NULL;
        gchar  *dbg = NULL;
        gst_message_parse_warning(msg, &err, &dbg);
        log_info("Pipeline warning: %s (%s)", err->message, dbg ? dbg : "");
        g_clear_error(&err);
        g_free(dbg);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(app->pipeline)) {
            GstState old_s, new_s, pending;
            gst_message_parse_state_changed(msg, &old_s, &new_s, &pending);
            log_info("Pipeline: %s -> %s",
                     gst_element_state_get_name(old_s),
                     gst_element_state_get_name(new_s));
        }
        break;

    default:
        break;
    }
    return TRUE;
}


static gboolean sigint_cb(gpointer data)
{
    AppState *app = (AppState *)data;
    log_info("SIGINT/SIGTERM received — shutting down");
    app->running = false;
    g_main_loop_quit(app->loop);
    return G_SOURCE_REMOVE;
}


int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    AppState *app = &g_app;
    app->frame_fd = -1;
    app->ev_fd    = -1;
    app->running  = true;

    log_info("aroma_display starting %dx%d @ %dfps", WIDTH, HEIGHT, FPS);

    if (!shm_init(app)) {
        log_err("Failed to attach SHM — is the render VM running?");
        return EXIT_FAILURE;
    }

    if (!pipeline_init(app)) {
        shm_cleanup(app);
        return EXIT_FAILURE;
    }

    app->loop = g_main_loop_new(NULL, FALSE);
    GstBus *bus = gst_element_get_bus(app->pipeline);
    gst_bus_add_watch(bus, bus_message_cb, app);
    gst_object_unref(bus);

    g_unix_signal_add(SIGINT,  sigint_cb, app);
    g_unix_signal_add(SIGTERM, sigint_cb, app);

    GstStateChangeReturn sc =
        gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (sc == GST_STATE_CHANGE_FAILURE) {
        log_err("Failed to set pipeline to PLAYING");
        pipeline_cleanup(app);
        shm_cleanup(app);
        g_main_loop_unref(app->loop);
        return EXIT_FAILURE;
    }

    if (pthread_create(&app->feed_thread, NULL, feed_thread, app) != 0) {
        perror("pthread_create");
        pipeline_cleanup(app);
        shm_cleanup(app);
        g_main_loop_unref(app->loop);
        return EXIT_FAILURE;
    }

    log_info("Running. Press Ctrl+C to stop.");
    g_main_loop_run(app->loop);

    app->running = false;

    if (app->use_sem && app->frame_sem != SEM_FAILED)
        sem_post(app->frame_sem);

    pthread_join(app->feed_thread, NULL);
    log_info("Final stats: pushed=%" PRIu64 " dropped=%" PRIu64,
             app->frames_pushed, app->frames_dropped);

    gst_app_src_end_of_stream(GST_APP_SRC(app->appsrc));
    pipeline_cleanup(app);
    shm_cleanup(app);
    g_main_loop_unref(app->loop);

    log_info("Clean exit.");
    return EXIT_SUCCESS;
}