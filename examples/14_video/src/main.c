#include <aroma.h>
#include <aroma_incense_loader.h>
#include <aroma_time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define FRAME_W 160
#define FRAME_H 90
#define FRAME_INTERVAL_MS 33

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;
static AromaNode *g_video;
static AromaNode *g_status;
static AromaNode *g_source;
static unsigned char g_pixels[FRAME_W * FRAME_H * 4];
static uint64_t g_last_push_ms;
static uint64_t g_last_status_ms;
static double g_position_s;
static bool g_playing = true;
static bool g_file_mode = false;

static void paint_frame(double t)
{
    for (int y = 0; y < FRAME_H; y++)
    {
        for (int x = 0; x < FRAME_W; x++)
        {
            int bar = (x + (int)(t * 60.0)) % FRAME_W;
            unsigned char r = (unsigned char)((bar * 255) / FRAME_W);
            unsigned char g = (unsigned char)((y * 255) / FRAME_H);
            unsigned char b = (unsigned char)(128 + 127 * ((x + y) % 2));
            unsigned char *p = g_pixels + (y * FRAME_W + x) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

static void show_status(void)
{
    if (!g_status || !g_video)
        return;
    char buf[96];
    const char *state = "stopped";
    switch (aroma_video_get_state(g_video))
    {
    case AROMA_VIDEO_PLAYING:
        state = "playing";
        break;
    case AROMA_VIDEO_PAUSED:
        state = "paused";
        break;
    case AROMA_VIDEO_FINISHED:
        state = "finished";
        break;
    case AROMA_VIDEO_ERROR:
        state = "error";
        break;
    default:
        break;
    }
    if (g_file_mode && g_video)
        g_position_s = aroma_video_get_position(g_video);
    if (aroma_video_get_state(g_video) == AROMA_VIDEO_ERROR)
    {
        const char *err = aroma_video_get_error(g_video);
        snprintf(buf, sizeof(buf), "error: %s",
                 err && err[0] ? err : "unknown");
        aroma_label_set_text(g_status, buf);
        return;
    }
    snprintf(buf, sizeof(buf), "%s  %.1fs  %dx%d", state, g_position_s,
             aroma_video_get_video_width(g_video),
             aroma_video_get_video_height(g_video));
    aroma_label_set_text(g_status, buf);
}

static void play_cb(void *userdata)
{
    (void)userdata;
    g_playing = true;
    if (g_video)
        aroma_video_play(g_video);
    show_status();
}

static void pause_cb(void *userdata)
{
    (void)userdata;
    g_playing = false;
    if (g_video)
        aroma_video_pause(g_video);
    show_status();
}

static void stop_cb(void *userdata)
{
    (void)userdata;
    g_playing = false;
    g_position_s = 0.0;
    if (g_video)
        aroma_video_stop(g_video);
    show_status();
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("play", INCENSE_CALLBACK_VOID_PTR, play_cb, NULL);
    IncenseRegisterCallback("pause", INCENSE_CALLBACK_VOID_PTR, pause_cb, NULL);
    IncenseRegisterCallback("stop", INCENSE_CALLBACK_VOID_PTR, stop_cb, NULL);
    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    if (!window)
    {
        int count = 0;
        const IncenseError *errors = IncenseGetErrors(&count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
        aroma_font_destroy(g_font);
        aroma_font_destroy(g_icon_font);
        aroma_ui_shutdown();
        return 1;
    }
    g_video = IncenseFindWidget(g_registry, "player");
    g_status = IncenseFindWidget(g_registry, "status");
    g_source = IncenseFindWidget(g_registry, "source");
    if (g_video)
        aroma_video_set_font(g_video, g_font);
    if (g_video)
    {
        
        aroma_video_set_source(g_video, "assets/sample_sintel.mp4");
        if (aroma_video_get_state(g_video) != AROMA_VIDEO_ERROR)
        {
            g_file_mode = true;
            aroma_video_set_looping(g_video, true);
            aroma_video_set_muted(g_video, false);
            aroma_video_play(g_video);
            if (g_source)
            {
                char sub[128];
                snprintf(sub, sizeof(sub), "Sintel MP4/H.264, %.0fs, CC-BY Blender",
                         aroma_video_get_duration(g_video));
                aroma_label_set_text(g_source, sub);
            }
        }
        else if (g_source)
        {
            aroma_label_set_text(g_source, "Synthetic stream (no file)");
        }
    }
    g_last_push_ms = aroma_time_now_ms();
    g_last_status_ms = g_last_push_ms;
    show_status();

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        uint64_t now = aroma_time_now_ms();
        if (!g_file_mode && g_playing && g_video && now - g_last_push_ms >= FRAME_INTERVAL_MS)
        {
            g_last_push_ms = now;
            g_position_s += (double)FRAME_INTERVAL_MS / 1000.0;
            paint_frame(g_position_s);
            aroma_video_push_frame(g_video, g_pixels, FRAME_W, FRAME_H);
        }
        if (now - g_last_status_ms >= 250)
        {
            g_last_status_ms = now;
            show_status();
        }
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
