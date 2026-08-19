#include "glps_x11.h"
#include "glps_egl_context.h"
#include <X11/Xatom.h>
#include <EGL/egl.h>
#include "utils/logger/pico_logger.h"

#define MAX_EVENTS_PER_FRAME 10
#define TARGET_FPS 60
#define NS_PER_FRAME (1000000000 / TARGET_FPS)

#define XC_arrow 2
#define XC_hand1 58
#define XC_crosshair 34
#define XC_right_side 96
#define XC_top_side 138
#define XC_xterm 152
#define XC_X_cursor 0

static ssize_t __get_window_id_by_xid(glps_WindowManager *wm, Window xid)
{
    if (wm == NULL || wm->windows == NULL) return -1;

    for (size_t i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i] != NULL && wm->windows[i]->window == xid) return i;
    }

    return -1;
}
void __remove_window(glps_WindowManager *wm, Window xid)
{
    if (!wm || !wm->windows || wm->window_count == 0) return;

    ssize_t window_id = __get_window_id_by_xid(wm, xid);
    if (window_id < 0) return;

    // Unbind EGL surface if currently bound
    if (wm->egl_ctx != NULL && eglGetCurrentSurface(EGL_DRAW) == wm->windows[window_id]->egl_surface)
    {
        eglMakeCurrent(wm->egl_ctx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    // Destroy EGL surface if valid
    if (wm->windows[window_id]->egl_surface != EGL_NO_SURFACE && wm->egl_ctx != NULL)
    {
        eglDestroySurface(wm->egl_ctx->dpy, wm->windows[window_id]->egl_surface);
        wm->windows[window_id]->egl_surface = EGL_NO_SURFACE;
    }

    // Destroy X11 window if valid
    if (wm->x11_ctx && wm->x11_ctx->display && wm->windows[window_id]->window)
    {
        XDestroyWindow(wm->x11_ctx->display, wm->windows[window_id]->window);
        wm->windows[window_id]->window = 0;
    }

    // Free window structure
    free(wm->windows[window_id]);

    // Shift remaining windows in array
    for (size_t i = window_id; i < wm->window_count - 1; i++)
    {
        wm->windows[i] = wm->windows[i + 1];
    }
    wm->windows[wm->window_count - 1] = NULL;
    wm->window_count--;

    // Destroy EGL context if last window
    if (wm->window_count == 0 && wm->egl_ctx != NULL)
    {
        glps_egl_destroy(wm);
    }
}

void glps_x11_init(glps_WindowManager *wm)
{
    if (wm == NULL)
    {
        LOG_CRITICAL("Window Manager is NULL. Exiting..");
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx = (glps_X11Context *)calloc(1, sizeof(glps_X11Context));
    if (wm->x11_ctx == NULL)
    {
        LOG_CRITICAL("Failed to allocate X11 context");
        exit(EXIT_FAILURE);
    }

    wm->windows = (glps_X11Window **)calloc(MAX_WINDOWS, sizeof(glps_X11Window *));
    if (wm->windows == NULL)
    {
        LOG_CRITICAL("Failed to allocate windows array");
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }
     if (!XInitThreads()) {
        fprintf(stderr, "Failed to initialize X11 threads!\n");
        exit(EXIT_FAILURE);
    }
    wm->x11_ctx->display = XOpenDisplay(NULL);
    if (!wm->x11_ctx->display)
    {
        LOG_CRITICAL("Failed to open X display");
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->font = XLoadQueryFont(wm->x11_ctx->display, "fixed");
    if (!wm->x11_ctx->font)
    {
        LOG_CRITICAL("Failed to load system font");
        XCloseDisplay(wm->x11_ctx->display);
        free(wm->windows);
        free(wm->x11_ctx);
        exit(EXIT_FAILURE);
    }

    wm->x11_ctx->wm_delete_window = XInternAtom(wm->x11_ctx->display, "WM_DELETE_WINDOW", False);

    // Initialize default cursor (arrow)
    wm->x11_ctx->cursor = XCreateFontCursor(wm->x11_ctx->display, XC_arrow);
}

ssize_t glps_x11_window_create(glps_WindowManager *wm, const char *title,
                               int x, int y, int width, int height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL)
    {
        LOG_CRITICAL("Failed to create X11 window. Window manager and/or Display NULL.");
        exit(EXIT_FAILURE);
    }

    if (wm->window_count >= MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows reached");
        return -1;
    }

    int screen = DefaultScreen(wm->x11_ctx->display);
    wm->windows[wm->window_count] = (glps_X11Window *)calloc(1, sizeof(glps_X11Window));
    if (wm->windows[wm->window_count] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        return -1;
    }
    wm->windows[wm->window_count]->fps_start_time = (struct timespec){0};
    wm->windows[wm->window_count]->fps_is_init = false;

    wm->windows[wm->window_count]->window = XCreateSimpleWindow(
        wm->x11_ctx->display,
        RootWindow(wm->x11_ctx->display, screen),
        x, y, width, height, 1,
        BlackPixel(wm->x11_ctx->display, screen),
        WhitePixel(wm->x11_ctx->display, screen));

    if (wm->windows[wm->window_count]->window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    XSetWindowBackground(wm->x11_ctx->display, wm->windows[wm->window_count]->window, 0xFFFFFF);
    XSetWindowAttributes swa;
    swa.backing_store = WhenMapped;
    XChangeWindowAttributes(wm->x11_ctx->display, wm->windows[wm->window_count]->window, CWBackingStore, &swa);
    XStoreName(wm->x11_ctx->display, wm->windows[wm->window_count]->window, title);

    wm->x11_ctx->gc = XCreateGC(wm->x11_ctx->display, wm->windows[wm->window_count]->window, 0, NULL);
    if (wm->x11_ctx->gc == NULL)
    {
        LOG_ERROR("Failed to create graphics context");
        XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    XSetWMProtocols(wm->x11_ctx->display, wm->windows[wm->window_count]->window,
                    &wm->x11_ctx->wm_delete_window, 1);

    long event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                      KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    int result = XSelectInput(wm->x11_ctx->display, wm->windows[wm->window_count]->window, event_mask);
    if (result == BadWindow)
    {
        LOG_ERROR("Failed to select input events");
        XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
        free(wm->windows[wm->window_count]);
        wm->windows[wm->window_count] = NULL;
        return -1;
    }

    if (wm->egl_ctx != NULL)
    {
        wm->windows[wm->window_count]->egl_surface =
            eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                                   (NativeWindowType)wm->windows[wm->window_count]->window, NULL);
        if (wm->windows[wm->window_count]->egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
            free(wm->windows[wm->window_count]);
            wm->windows[wm->window_count] = NULL;
            return -1;
        }
    }

    XMapWindow(wm->x11_ctx->display, wm->windows[wm->window_count]->window);
    XFlush(wm->x11_ctx->display);

    if (wm->window_count == 0)
    {
        glps_egl_create_ctx(wm);
        glps_egl_make_ctx_current(wm, 0);
    }

    return wm->window_count++;
}

void glps_x11_toggle_window_decorations(glps_WindowManager *wm, bool state, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL) return;

    Atom motif_hints = XInternAtom(wm->x11_ctx->display, "_MOTIF_WM_HINTS", False);

    if (motif_hints != None)
    {
        typedef struct
        {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } MotifWmHints;

        MotifWmHints hints;
        hints.flags = 2;
        hints.decorations = state ? 1 : 0;
        hints.functions = 0;
        hints.input_mode = 0;
        hints.status = 0;

        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, motif_hints, motif_hints, 32,
                        PropModeReplace, (unsigned char *)&hints, 5);
    }

    Atom net_wm_window_type = XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE", False);
    Atom window_type = state ? XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_NORMAL", False) : XInternAtom(wm->x11_ctx->display, "_NET_WM_WINDOW_TYPE_DOCK", False);

    if (net_wm_window_type != None && window_type != None)
    {
        XChangeProperty(wm->x11_ctx->display, wm->windows[window_id]->window, net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&window_type, 1);
    }

    XFlush(wm->x11_ctx->display);
    XSync(wm->x11_ctx->display, False);
}

bool glps_x11_should_close(glps_WindowManager *wm)
{
    static struct timespec last_frame = {0};
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long elapsed = (now.tv_sec - last_frame.tv_sec) * 1000000000 + (now.tv_nsec - last_frame.tv_nsec);
    if (elapsed < NS_PER_FRAME && last_frame.tv_sec != 0)
    {
        return false;
    }
    last_frame = now;

    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL) return true;
    if (wm->window_count == 0) return true;

    Display *display = wm->x11_ctx->display;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(ConnectionNumber(display), &fds);

    struct timeval tv = {0, 0};

    int ready = select(ConnectionNumber(display) + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0) return false;

    XEvent event;
    int events_processed = 0;

    while (XPending(display) > 0 && events_processed < MAX_EVENTS_PER_FRAME)
    {
        XNextEvent(display, &event);
        events_processed++;

        ssize_t window_id = __get_window_id_by_xid(wm, event.xany.window);
        if (window_id < 0 || window_id >= (ssize_t)wm->window_count || wm->windows[window_id] == NULL) continue;

        switch (event.type)
        {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm->x11_ctx->wm_delete_window)
            {
                if (wm->callbacks.window_close_callback)
                {
                    wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
                }
                Window window_to_remove = event.xclient.window;
                __remove_window(wm, window_to_remove);
            }
            break;

        case DestroyNotify:
            if (wm->callbacks.window_close_callback)
            {
                wm->callbacks.window_close_callback((size_t)window_id, wm->callbacks.window_close_data);
            }
            Window window_to_remove = event.xdestroywindow.window;
            __remove_window(wm, window_to_remove);
            break;

        case ConfigureNotify:
            if (wm->callbacks.window_resize_callback)
            {
                wm->callbacks.window_resize_callback((size_t)window_id, event.xconfigure.width, event.xconfigure.height, wm->callbacks.window_resize_data);
            }
            break;

        case MotionNotify:
            if (wm->callbacks.mouse_move_callback)
            {
                wm->callbacks.mouse_move_callback((size_t)window_id, event.xmotion.x, event.xmotion.y, wm->callbacks.mouse_move_data);
            }
            if (window_id < wm->window_count && wm->windows[window_id] != NULL && wm->x11_ctx->cursor)
            {
                XDefineCursor(wm->x11_ctx->display, wm->windows[window_id]->window, wm->x11_ctx->cursor);
            }
            break;

        case ButtonPress:
            switch (event.xbutton.button)
            {
            case 4:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS, GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 5:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_V_AXIS, GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 6:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS, GLPS_SCROLL_SOURCE_WHEEL, -1.0f, -1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            case 7:
                if (wm->callbacks.mouse_scroll_callback)
                {
                    wm->callbacks.mouse_scroll_callback((size_t)window_id, GLPS_SCROLL_H_AXIS, GLPS_SCROLL_SOURCE_WHEEL, 1.0f, 1.0f, false, wm->callbacks.mouse_scroll_data);
                }
                break;
            default:
                if (wm->callbacks.mouse_click_callback)
                {
                    wm->callbacks.mouse_click_callback((size_t)window_id, true, wm->callbacks.mouse_click_data);
                }
                break;
            }
            break;

        case ButtonRelease:
            if (event.xbutton.button < 4 && wm->callbacks.mouse_click_callback)
            {
                wm->callbacks.mouse_click_callback((size_t)window_id, false, wm->callbacks.mouse_click_data);
            }
            break;

        case KeyPress:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                if (keycode != 0)
                {
                    wm->callbacks.keyboard_callback((size_t)window_id, true, buf, keycode, wm->callbacks.keyboard_data);
                }
            }
            break;

        case KeyRelease:
            if (wm->callbacks.keyboard_callback)
            {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                KeyCode keycode = XKeysymToKeycode(wm->x11_ctx->display, keysym);
                if (keycode != 0)
                {
                    wm->callbacks.keyboard_callback((size_t)window_id, false, buf, keycode, wm->callbacks.keyboard_data);
                }
            }
            break;

        case Expose:
            if (wm->callbacks.window_frame_update_callback)
            {
                wm->callbacks.window_frame_update_callback((size_t)window_id, wm->callbacks.window_frame_update_data);
            }
            break;
        }
    }

    return false;
}

void glps_x11_window_update(glps_WindowManager *wm, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL) return;

    static struct timespec last_time = {0};
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    if (last_time.tv_sec != 0 || last_time.tv_nsec != 0)
    {
        long elapsed_ns = (current_time.tv_sec - last_time.tv_sec) * 1000000000 + (current_time.tv_nsec - last_time.tv_nsec);
        if (elapsed_ns < NS_PER_FRAME)
        {
            struct timespec sleep_time = {0, NS_PER_FRAME - elapsed_ns};
            nanosleep(&sleep_time, NULL);
        }
    }
    last_time = current_time;

    if (wm->callbacks.window_frame_update_callback)
    {
        wm->callbacks.window_frame_update_callback(window_id, wm->callbacks.window_frame_update_data);
    }

    XFlush(wm->x11_ctx->display);
}
void glps_x11_destroy(glps_WindowManager *wm)
{
    if (!wm) return;

    // Remove all windows safely
    if (wm->windows)
    {
        while (wm->window_count > 0)
        {
            __remove_window(wm, wm->windows[0]->window);
        }
        free(wm->windows);
        wm->windows = NULL;
    }

    // Clean up X11 resources
    if (wm->x11_ctx)
    {
        if (wm->x11_ctx->font && wm->x11_ctx->display)
        {
            XFreeFont(wm->x11_ctx->display, wm->x11_ctx->font);
            wm->x11_ctx->font = NULL;
        }
        if (wm->x11_ctx->gc && wm->x11_ctx->display)
        {
            XFreeGC(wm->x11_ctx->display, wm->x11_ctx->gc);
            wm->x11_ctx->gc = 0;
        }
        if (wm->x11_ctx->cursor && wm->x11_ctx->display)
        {
            XFreeCursor(wm->x11_ctx->display, wm->x11_ctx->cursor);
            wm->x11_ctx->cursor = 0;
        }
        if (wm->x11_ctx->display)
        {
            XCloseDisplay(wm->x11_ctx->display);
            wm->x11_ctx->display = NULL;
        }

        wm->x11_ctx->wm_delete_window = None;
        free(wm->x11_ctx);
        wm->x11_ctx = NULL;
    }

    // Final safety
    wm->window_count = 0;
    wm->windows = NULL;
}

void glps_x11_get_window_dimensions(glps_WindowManager *wm, size_t window_id,
                                    int *width, int *height)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL ||
        width == NULL || height == NULL) return;

    Window root;
    int x, y;
    unsigned int border_width, depth;
    XGetGeometry(wm->x11_ctx->display, wm->windows[window_id]->window, &root,
                 &x, &y, (unsigned int *)width, (unsigned int *)height,
                 &border_width, &depth);
}

void glps_x11_window_is_resizable(glps_WindowManager *wm, bool state, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL ||
        window_id >= wm->window_count || wm->windows[window_id] == NULL) return;

    Display *display = wm->x11_ctx->display;
    Window win = wm->windows[window_id]->window;

    Window root;
    int x, y;
    unsigned int width, height, border_width, depth;
    if (!XGetGeometry(display, win, &root, &x, &y, &width, &height, &border_width, &depth)) return;

    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints == NULL) return;

    long supplied_return;
    XGetWMNormalHints(display, win, size_hints, &supplied_return);

    if (state)
    {
        size_hints->flags &= ~(PMinSize | PMaxSize);
        size_hints->min_width = 1;
        size_hints->min_height = 1;
        size_hints->max_width = INT_MAX;
        size_hints->max_height = INT_MAX;
        size_hints->flags |= PResizeInc;
        size_hints->width_inc = 1;
        size_hints->height_inc = 1;
    }
    else
    {
        size_hints->flags |= PMinSize | PMaxSize;
        size_hints->min_width = width;
        size_hints->min_height = height;
        size_hints->max_width = width;
        size_hints->max_height = height;
    }

    XSetWMNormalHints(display, win, size_hints);
    XFree(size_hints);
    XFlush(display);
}

void glps_x11_attach_to_clipboard(glps_WindowManager *wm, char *mime, char *data)
{
    (void)wm; (void)mime; (void)data;
}

void glps_x11_get_from_clipboard(glps_WindowManager *wm, char *data, size_t data_size)
{
    (void)wm; (void)data; (void)data_size;
}

void glps_x11_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor)
{
    if (!wm || !wm->x11_ctx)
    {
        LOG_ERROR("Window manager invalid. Couldn't change cursor.");
        return;
    }

    int selected_cursor;

    switch (user_cursor)
    {
    case GLPS_CURSOR_ARROW: selected_cursor = XC_arrow; break;
    case GLPS_CURSOR_IBEAM: selected_cursor = XC_xterm; break;
    case GLPS_CURSOR_CROSSHAIR: selected_cursor = XC_crosshair; break;
    case GLPS_CURSOR_HAND: selected_cursor = XC_hand1; break;
    case GLPS_CURSOR_HRESIZE: selected_cursor = XC_right_side; break;
    case GLPS_CURSOR_VRESIZE: selected_cursor = XC_top_side; break;
    case GLPS_CURSOR_NOT_ALLOWED: selected_cursor = XC_X_cursor; break;
    default: selected_cursor = -1;
    }

    if (selected_cursor < 0)
    {
        LOG_ERROR("Unknown cursor type.");
        return;
    }

    wm->x11_ctx->cursor = XCreateFontCursor(wm->x11_ctx->display, (unsigned int)selected_cursor);
}

void glps_x11_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count) return;

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_blur = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    if (atom_blur != None)
    {
        if (enable)
        {
            unsigned long value = 1;
            XChangeProperty(display, window, atom_blur, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
        }
        else
        {
            XDeleteProperty(display, window, atom_blur);
        }
    }

    Atom atom_mutter_blur = XInternAtom(display, "_MUFFIN_BLUR_REGION", False);
    if (atom_mutter_blur == None) atom_mutter_blur = XInternAtom(display, "_MUTTER_BLUR_REGION", False);

    if (atom_mutter_blur != None)
    {
        if (enable)
        {
            long blur_data[4] = {0, 0, 0, 0};
            int width, height;
            glps_x11_get_window_dimensions(wm, window_id, &width, &height);
            blur_data[2] = width;
            blur_data[3] = height;

            XChangeProperty(display, window, atom_mutter_blur, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)blur_data, 4);
        }
        else
        {
            XDeleteProperty(display, window, atom_mutter_blur);
        }
    }

    XFlush(display);
}

void glps_x11_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count) return;

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    Atom atom_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
    if (atom_opacity != None)
    {
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;

        unsigned long opacity_value = (unsigned long)(opacity * 0xFFFFFFFF);
        XChangeProperty(display, window, atom_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity_value, 1);
    }

    XFlush(display);
}

void glps_x11_set_window_background_transparent(glps_WindowManager *wm, size_t window_id)
{
    if (wm == NULL || wm->x11_ctx == NULL || window_id >= wm->window_count) return;

    Display *display = wm->x11_ctx->display;
    Window window = wm->windows[window_id]->window;

    XWindowAttributes window_attrs;
    if (!XGetWindowAttributes(display, window, &window_attrs)) return;

    if (window_attrs.depth == 32)
    {
        XSetWindowAttributes attrs;
        attrs.background_pixmap = None;
        Status status = XChangeWindowAttributes(display, window, CWBackPixmap, &attrs);
        if (status == 0)
        {
            LOG_ERROR("Failed to set window background to transparent");
        }
    }
    else
    {
        LOG_WARNING("Window depth %d doesn't support transparency. Need 32-bit depth.", window_attrs.depth);
    }

    XFlush(display);
}

bool glps_x11_create_window_with_visual(glps_WindowManager *wm, const char *title,
                                        int width, int height, bool transparent)
{
    if (wm == NULL || wm->x11_ctx == NULL || wm->x11_ctx->display == NULL) return false;
    if (wm->window_count >= MAX_WINDOWS)
    {
        LOG_ERROR("Maximum number of windows reached");
        return false;
    }

    Display *display = wm->x11_ctx->display;
    int screen = DefaultScreen(display);

    XVisualInfo visual_template;
    visual_template.depth = 32;
    visual_template.class = TrueColor;

    int num_visuals;
    XVisualInfo *visual_list = XGetVisualInfo(display, VisualDepthMask | VisualClassMask, &visual_template, &num_visuals);

    Visual *visual = NULL;
    int depth = 0;
    Colormap colormap = None;

    if (visual_list != NULL && num_visuals > 0 && transparent)
    {
        visual = visual_list[0].visual;
        depth = visual_list[0].depth;
        colormap = XCreateColormap(display, RootWindow(display, screen), visual, AllocNone);
        XFree(visual_list);
    }
    else
    {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        colormap = DefaultColormap(display, screen);
        if (transparent) LOG_WARNING("Transparent window requested but no 32-bit visual available");
    }

    XSetWindowAttributes attrs;
    attrs.colormap = colormap;
    attrs.background_pixmap = None;
    attrs.border_pixel = 0;
    attrs.event_mask = PointerMotionMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    unsigned long attrs_mask = CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask;

    if (!transparent)
    {
        attrs.background_pixel = WhitePixel(display, screen);
        attrs_mask |= CWBackPixel;
    }

    Window window = XCreateWindow(display, RootWindow(display, screen),
                                  10, 10, width, height, 1,
                                  depth, InputOutput, visual,
                                  attrs_mask, &attrs);

    if (window == 0)
    {
        LOG_ERROR("Failed to create X11 window");
        if (colormap != None && colormap != DefaultColormap(display, screen))
        {
            XFreeColormap(display, colormap);
        }
        return false;
    }

    size_t window_index = wm->window_count;
    wm->windows[window_index] = calloc(1, sizeof(glps_X11Window));
    if (wm->windows[window_index] == NULL)
    {
        LOG_ERROR("Failed to allocate window");
        XDestroyWindow(display, window);
        if (colormap != None && colormap != DefaultColormap(display, screen))
        {
            XFreeColormap(display, colormap);
        }
        return false;
    }

    wm->windows[window_index]->window = window;
    wm->windows[window_index]->fps_start_time = (struct timespec){0};
    wm->windows[window_index]->fps_is_init = false;

    XStoreName(display, window, title);
    XSetWMProtocols(display, window, &wm->x11_ctx->wm_delete_window, 1);

    if (wm->egl_ctx != NULL)
    {
        EGLSurface egl_surface = eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf, (NativeWindowType)window, NULL);
        if (egl_surface == EGL_NO_SURFACE)
        {
            LOG_ERROR("Failed to create EGL surface");
            XDestroyWindow(display, window);
            free(wm->windows[window_index]);
            wm->windows[window_index] = NULL;
            if (colormap != None && colormap != DefaultColormap(display, screen))
            {
                XFreeColormap(display, colormap);
            }
            return false;
        }
        wm->windows[window_index]->egl_surface = egl_surface;
    }

    if (window_index == 0 && wm->egl_ctx == NULL)
    {
        glps_egl_create_ctx(wm);
    }

    if (wm->egl_ctx != NULL)
    {
        glps_egl_make_ctx_current(wm, window_index);
    }

    XMapWindow(display, window);
    XFlush(display);

    wm->window_count++;
    return true;
}

Display *glps_x11_get_display(glps_WindowManager *wm)
{
    if (!wm) return NULL;
    return wm->x11_ctx->display;
}

#ifdef GLPS_USE_VULKAN
void glps_x11_vk_create_surface(glps_WindowManager *wm, size_t window_id, VkInstance *instance, VkSurfaceKHR *surface)
{
    Display *xdisplay = wm->x11_ctx->display;
    Window xwindow = wm->windows[window_id]->window;
    VkXlibSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = xdisplay,
        .window = xwindow};

    vkCreateXlibSurfaceKHR(*instance, &surface_info, NULL, surface);
}
#endif
