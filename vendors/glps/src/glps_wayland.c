#include <glps_egl_context.h>
#include <glps_wayland.h>
#include "utils/logger/pico_logger.h"

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                      uint32_t serial)
{
  xdg_wm_base_pong(xdg_wm_base, serial);
}

struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

glps_WaylandContext *__get_wl_context(glps_WindowManager *wm)
{
  if (wm == NULL || wm->wayland_ctx == NULL)
  {
    LOG_WARNING("Window manager and/or Wayland context is NULL.");
    return NULL;
  }
  return wm->wayland_ctx;
}


static bool __is_valid_window_id(glps_WindowManager *wm, size_t window_id)
{
  if (wm == NULL || wm->windows == NULL)
    return false;
  if (window_id >= (size_t)MAX_WINDOWS)
    return false;
  if (window_id >= wm->window_count)
    return false;
  if (wm->windows[window_id] == NULL)
    return false;
  return true;
}

ssize_t __get_window_id_from_surface(glps_WindowManager *wm,
                                     struct wl_surface *surface)
{
  if (wm == NULL || surface == NULL)
  {
    LOG_ERROR("Couldn't get window id from surface, Window manager and/or "
              "Surface is NULL.");
    return -1;
  }

  for (size_t i = 0; i < wm->window_count; ++i)
  {
    if (wm->windows[i] == NULL)
      continue;
    if (surface == wm->windows[i]->wl_surface)
      return i;
  }

  return -1;
}

ssize_t __get_window_id_from_xdg_surface(glps_WindowManager *wm,
                                         struct xdg_surface *surface)
{
  if (wm == NULL || surface == NULL)
  {
    LOG_ERROR("Couldn't get window id from surface, Window manager and/or "
              "Surface is NULL.");
    return -1;
  }

  for (size_t i = 0; i < wm->window_count; ++i)
  {
    if (wm->windows[i] == NULL)
      continue;
    if (surface == wm->windows[i]->xdg_surface)
      return i;
  }

  return -1;
}


void __window_destroy(glps_WindowManager *wm, size_t window_id)
{
  if (!__is_valid_window_id(wm, window_id))
  {
    LOG_ERROR("__window_destroy: invalid window id %zu", window_id);
    return;
  }

  glps_WaylandWindow *window = wm->windows[window_id];

  if (window->frame_args != NULL)
  {
    free(window->frame_args);
    window->frame_args = NULL;
  }

  if (window->frame_callback != NULL)
  {
    wl_callback_destroy(window->frame_callback);
    window->frame_callback = NULL;
  }

  if (window->egl_surface != EGL_NO_SURFACE)
  {
    if (wm->egl_ctx != NULL)
      eglDestroySurface(wm->egl_ctx->dpy, window->egl_surface);
    window->egl_surface = EGL_NO_SURFACE;
  }

  if (window->egl_window != NULL)
  {
    wl_egl_window_destroy(window->egl_window);
    window->egl_window = NULL;
  }

  if (window->xdg_toplevel != NULL)
  {
    xdg_toplevel_destroy(window->xdg_toplevel);
    window->xdg_toplevel = NULL;
  }

  if (window->xdg_surface != NULL)
  {
    xdg_surface_destroy(window->xdg_surface);
    window->xdg_surface = NULL;
  }

  if (window->wl_surface != NULL)
  {
    wl_surface_destroy(window->wl_surface);
    window->wl_surface = NULL;
  }

  free(window);
  wm->windows[window_id] = NULL;

 
  while (wm->window_count > 0 && wm->windows[wm->window_count - 1] == NULL)
  {
    wm->window_count--;
  }

  if (wm->window_count == 0)
  {
    LOG_INFO("All windows destroyed. Exiting program.");
    wm->should_close = true;
  }
}

void wl_update(glps_WindowManager *wm, size_t window_id)
{
  if (!__is_valid_window_id(wm, window_id))
  {
    LOG_ERROR("wl_update: invalid window id %zu", window_id);
    return;
  }

  int width  = wm->windows[window_id]->properties.width;
  int height = wm->windows[window_id]->properties.height;
  wl_surface_damage(wm->windows[window_id]->wl_surface, 0, 0, width, height);
  wl_surface_commit(wm->windows[window_id]->wl_surface);
}

ssize_t __get_window_id_from_xdg_toplevel(glps_WindowManager *wm,
                                          struct xdg_toplevel *toplevel)
{
  if (wm == NULL || toplevel == NULL)
  {
    LOG_ERROR("Couldn't get window id from toplevel, Window manager and/or "
              "toplevel is NULL.");
    return -1;
  }

  for (size_t i = 0; i < wm->window_count; ++i)
  {
    if (wm->windows[i] == NULL)
      continue;
    if (toplevel == wm->windows[i]->xdg_toplevel)
      return i;
  }

  return -1;
}

struct wl_callback_listener frame_callback_listener;

void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                      uint32_t serial, struct wl_surface *surface,
                      wl_fixed_t surface_x, wl_fixed_t surface_y)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  ssize_t window_id = __get_window_id_from_surface(context, surface);
  if (window_id < 0)
  {
    LOG_ERROR("Origin window id is invalid.");
    return;
  }

  context->pointer_event.event_mask |= POINTER_EVENT_ENTER;
  context->pointer_event.serial    = serial;
  context->pointer_event.surface_x = surface_x;
  context->pointer_event.surface_y = surface_y;

  glps_WaylandContext *wayland_context = __get_wl_context(context);
  if (wayland_context == NULL)
  {
    LOG_ERROR("Couldn't fetch wayland context.");
    return;
  }
  wayland_context->mouse_window_id = (size_t)window_id;
}

void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                      uint32_t serial, struct wl_surface *surface)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  context->pointer_event.serial     = serial;
  context->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                       uint32_t time, wl_fixed_t surface_x,
                       wl_fixed_t surface_y)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  context->pointer_event.event_mask |= POINTER_EVENT_MOTION;
  context->pointer_event.time       = time;
  context->pointer_event.surface_x  = surface_x;
  context->pointer_event.surface_y  = surface_y;
}

void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                       uint32_t serial, uint32_t time, uint32_t button,
                       uint32_t state)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  context->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
  context->pointer_event.time   = time;
  context->pointer_event.serial = serial;
  context->pointer_event.button = button;
  context->pointer_event.state  = state;
}


#define GLPS_POINTER_AXES_COUNT 2

void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                     uint32_t axis, wl_fixed_t value)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  if (axis >= GLPS_POINTER_AXES_COUNT)
  {
    LOG_WARNING("wl_pointer_axis: axis %u out of range, ignoring", axis);
    return;
  }

  context->pointer_event.event_mask        |= POINTER_EVENT_AXIS;
  context->pointer_event.time               = time;
  context->pointer_event.axes[axis].valid   = true;
  context->pointer_event.axes[axis].value   = value;
}

void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                            uint32_t axis_source)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  context->pointer_event.event_mask  |= POINTER_EVENT_AXIS_SOURCE;
  context->pointer_event.axis_source  = axis_source;
}

void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                          uint32_t time, uint32_t axis)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  if (axis >= GLPS_POINTER_AXES_COUNT)
  {
    LOG_WARNING("wl_pointer_axis_stop: axis %u out of range, ignoring", axis);
    return;
  }

  context->pointer_event.time              = time;
  context->pointer_event.event_mask       |= POINTER_EVENT_AXIS_STOP;
  context->pointer_event.axes[axis].valid  = true;
}

void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                              uint32_t axis, int32_t discrete)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  if (axis >= GLPS_POINTER_AXES_COUNT)
  {
    LOG_WARNING("wl_pointer_axis_discrete: axis %u out of range, ignoring", axis);
    return;
  }

  context->pointer_event.event_mask           |= POINTER_EVENT_AXIS_DISCRETE;
  context->pointer_event.axes[axis].valid      = true;
  context->pointer_event.axes[axis].discrete   = discrete;
}

void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer,
                               uint32_t axis, int32_t value120)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  if (axis >= GLPS_POINTER_AXES_COUNT)
  {
    LOG_WARNING("wl_pointer_axis_value120: axis %u out of range, ignoring", axis);
    return;
  }

  context->pointer_event.event_mask           |= POINTER_EVENT_AXIS_DISCRETE;
  context->pointer_event.axes[axis].valid      = true;
  context->pointer_event.axes[axis].discrete   = value120 / 120;
}

void wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer,
                                         uint32_t axis, uint32_t direction)
{
  (void)data;
  (void)wl_pointer;
  (void)axis;
  (void)direction;
}

void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
  glps_WindowManager *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  struct pointer_event *event           = &context->pointer_event;
  glps_WaylandContext  *wayland_context = __get_wl_context(context);

  if (wayland_context == NULL)
  {
    LOG_ERROR("Couldn't fetch wayland context.");
    return;
  }

  if (event->event_mask & POINTER_EVENT_ENTER)
  {
    if (context->callbacks.mouse_enter_callback)
    {
      context->callbacks.mouse_enter_callback(
          wayland_context->mouse_window_id,
          wl_fixed_to_double(event->surface_x),
          wl_fixed_to_double(event->surface_y),
          context->callbacks.mouse_enter_data);
    }
  }

  if (event->event_mask & POINTER_EVENT_LEAVE)
  {
    if (context->callbacks.mouse_leave_callback)
    {
      context->callbacks.mouse_leave_callback(
          wayland_context->mouse_window_id,
          context->callbacks.mouse_leave_data);
    }
  }

  if (event->event_mask & POINTER_EVENT_MOTION)
  {
    if (context->callbacks.mouse_move_callback)
    {
      context->callbacks.mouse_move_callback(
          wayland_context->mouse_window_id,
          wl_fixed_to_double(event->surface_x),
          wl_fixed_to_double(event->surface_y),
          context->callbacks.mouse_move_data);
    }
  }

  if (event->event_mask & POINTER_EVENT_BUTTON)
  {
    if (context->callbacks.mouse_click_callback)
    {
      context->callbacks.mouse_click_callback(
          wayland_context->mouse_window_id,
          event->state == WL_POINTER_BUTTON_STATE_RELEASED ? false : true,
          context->callbacks.mouse_click_data);
    }
  }

  uint32_t axis_events = POINTER_EVENT_AXIS | POINTER_EVENT_AXIS_SOURCE |
                         POINTER_EVENT_AXIS_STOP | POINTER_EVENT_AXIS_DISCRETE;

  if (event->event_mask & axis_events)
  {
   
    static const GLPS_SCROLL_AXES axis_name[GLPS_POINTER_AXES_COUNT] = {
        [WL_POINTER_AXIS_VERTICAL_SCROLL]   = GLPS_SCROLL_V_AXIS,
        [WL_POINTER_AXIS_HORIZONTAL_SCROLL] = GLPS_SCROLL_H_AXIS,
    };
    static const GLPS_SCROLL_SOURCE axis_source[4] = {
        [WL_POINTER_AXIS_SOURCE_WHEEL]       = GLPS_SCROLL_SOURCE_WHEEL,
        [WL_POINTER_AXIS_SOURCE_FINGER]      = GLPS_SCROLL_SOURCE_FINGER,
        [WL_POINTER_AXIS_SOURCE_CONTINUOUS]  = GLPS_SCROLL_SOURCE_CONTINUOUS,
        [WL_POINTER_AXIS_SOURCE_WHEEL_TILT]  = GLPS_SCROLL_SOURCE_WHEEL_TILT,
    };
    const size_t axis_source_count = sizeof(axis_source) / sizeof(axis_source[0]);

    for (size_t i = 0; i < GLPS_POINTER_AXES_COUNT; ++i)
    {
      if (!event->axes[i].valid)
        continue;

      if (context->callbacks.mouse_scroll_callback)
      {
        GLPS_SCROLL_AXES axe = axis_name[i];

        GLPS_SCROLL_SOURCE source = GLPS_SCROLL_SOURCE_OTHER;
        if ((event->event_mask & POINTER_EVENT_AXIS_SOURCE) &&
            event->axis_source < axis_source_count)
        {
          source = axis_source[event->axis_source];
        }

        double value    = event->event_mask & POINTER_EVENT_AXIS
                              ? wl_fixed_to_double(event->axes[i].value)
                              : 0.0;
        int    discrete = event->event_mask & POINTER_EVENT_AXIS_DISCRETE
                              ? event->axes[i].discrete
                              : -1;
        bool   is_stopped = event->event_mask & POINTER_EVENT_AXIS_STOP;

        context->callbacks.mouse_scroll_callback(
            wayland_context->mouse_window_id,
            axe, source, value, discrete, is_stopped,
            context->callbacks.mouse_scroll_data);
      }
    }
  }

  memset(event, 0, sizeof(*event));
}

struct wl_pointer_listener wl_pointer_listener = {
    .enter                   = wl_pointer_enter,
    .leave                   = wl_pointer_leave,
    .motion                  = wl_pointer_motion,
    .button                  = wl_pointer_button,
    .axis                    = wl_pointer_axis,
    .frame                   = wl_pointer_frame,
    .axis_source             = wl_pointer_axis_source,
    .axis_stop               = wl_pointer_axis_stop,
    .axis_discrete           = wl_pointer_axis_discrete,
    .axis_value120           = wl_pointer_axis_value120,
    .axis_relative_direction = wl_pointer_axis_relative_direction,
};

void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                        uint32_t format, int32_t fd, uint32_t size)
{
  glps_WaylandContext *context = __get_wl_context(data);
  if (context == NULL)
  {
    close(fd);
    return;
  }

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
  {
    LOG_ERROR("wl_keyboard_keymap: unsupported keymap format %u", format);
    close(fd);
    return;
  }

  char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (map_shm == MAP_FAILED)
  {
    LOG_ERROR("wl_keyboard_keymap: mmap failed (errno=%d)", errno);
    close(fd);
    return;
  }

  struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
      context->xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);

  munmap(map_shm, size);
  close(fd);

  if (xkb_keymap == NULL)
  {
    LOG_ERROR("wl_keyboard_keymap: failed to compile keymap");
    return;
  }

  struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
  if (xkb_state == NULL)
  {
    LOG_ERROR("wl_keyboard_keymap: failed to create xkb state");
    xkb_keymap_unref(xkb_keymap);
    return;
  }

  if (context->xkb_keymap != NULL)
    xkb_keymap_unref(context->xkb_keymap);
  if (context->xkb_state != NULL)
    xkb_state_unref(context->xkb_state);

  context->xkb_keymap = xkb_keymap;
  context->xkb_state  = xkb_state;
}

void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                       uint32_t serial, struct wl_surface *surface,
                       struct wl_array *keys)
{
  glps_WaylandContext *context = __get_wl_context(data);
  if (context == NULL)
    return;

  glps_WindowManager *wm = (glps_WindowManager *)data;

  ssize_t window_id = __get_window_id_from_surface(wm, surface);
  if (window_id < 0)
  {
    LOG_ERROR("Origin window id is invalid.");
    return;
  }

  context->keyboard_serial    = serial;
  context->keyboard_window_id = (size_t)window_id;

  if (wm->callbacks.keyboard_enter_callback != NULL)
  {
    wm->callbacks.keyboard_enter_callback(context->keyboard_window_id,
                                          wm->callbacks.keyboard_enter_data);
  }
}

void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                     uint32_t serial, uint32_t time, uint32_t key,
                     uint32_t state)
{
  glps_WaylandContext *context = __get_wl_context(data);
  if (context == NULL)
    return;

  if (context->xkb_state == NULL)
  {
    LOG_WARNING("wl_keyboard_key: no xkb state yet, dropping key event");
    return;
  }

  char     utf8[128] = "";
  char     name[128] = "";
  uint32_t keycode   = key + 8;

  xkb_keysym_t sym = xkb_state_key_get_one_sym(context->xkb_state, keycode);
  if (sym == XKB_KEY_NoSymbol)
    return;

  if (xkb_keysym_get_name(sym, name, sizeof(name)) <= 0)
    name[0] = '\0';

  ssize_t utf8_len =
      xkb_state_key_get_utf8(context->xkb_state, keycode, utf8, sizeof(utf8));
  if (utf8_len <= 0 || utf8[0] == '\0')
    utf8[0] = '\0';

  glps_WindowManager *wm = (glps_WindowManager *)data;
  if (wm->callbacks.keyboard_callback != NULL)
  {
    wm->callbacks.keyboard_callback(
        context->keyboard_window_id,
        state == WL_KEYBOARD_KEY_STATE_PRESSED ? true : false,
        (utf8[0] != '\0' ? utf8 : name),
        keycode,
        wm->callbacks.keyboard_data);
  }
}

void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                       uint32_t serial, struct wl_surface *surface)
{
  glps_WindowManager *wm = (glps_WindowManager *)data;
  if (wm == NULL || wm->wayland_ctx == NULL)
    return;

  if (wm->callbacks.keyboard_leave_callback != NULL)
  {
    wm->callbacks.keyboard_leave_callback(wm->wayland_ctx->keyboard_window_id,
                                          wm->callbacks.keyboard_leave_data);
  }
}

void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                           uint32_t serial, uint32_t mods_depressed,
                           uint32_t mods_latched, uint32_t mods_locked,
                           uint32_t group)
{
  glps_WaylandContext *context = __get_wl_context(data);
  if (context == NULL || context->xkb_state == NULL)
    return;

  xkb_state_update_mask(context->xkb_state, mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                             int32_t rate, int32_t delay)
{
  (void)data;
  (void)wl_keyboard;
  (void)rate;
  (void)delay;
}

struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap      = wl_keyboard_keymap,
    .enter       = wl_keyboard_enter,
    .leave       = wl_keyboard_leave,
    .key         = wl_keyboard_key,
    .modifiers   = wl_keyboard_modifiers,
    .repeat_info = wl_keyboard_repeat_info,
};

struct touch_point *get_touch_point(void *data, int32_t id)
{
  if (data == NULL)
    return NULL;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_event *touch  = &wm->touch_event;
  const size_t        nmemb  = sizeof(touch->points) / sizeof(struct touch_point);
  ssize_t             invalid = -1;

  for (size_t i = 0; i < nmemb; ++i)
  {
    if (touch->points[i].valid && touch->points[i].id == id)
      return &touch->points[i];
    if (invalid == -1 && !touch->points[i].valid)
      invalid = (ssize_t)i;
  }

  if (invalid == -1)
  {
    LOG_WARNING("get_touch_point: no free touch point slots (max %zu), "
                "dropping touch id %d", nmemb, id);
    return NULL;
  }

  touch->points[invalid].valid       = true;
  touch->points[invalid].id          = id;
  touch->points[invalid].event_mask  = 0;
  touch->points[invalid].surface_x   = 0;
  touch->points[invalid].surface_y   = 0;
  touch->points[invalid].major       = 0;
  touch->points[invalid].minor       = 0;
  touch->points[invalid].orientation = 0;
  return &touch->points[invalid];
}

void wl_touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
                   uint32_t time, struct wl_surface *surface, int32_t id,
                   wl_fixed_t x, wl_fixed_t y)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm = (glps_WindowManager *)data;
  ssize_t window_id = __get_window_id_from_surface(wm, surface);
  if (window_id < 0)
  {
    LOG_ERROR("Window id is invalid.");
    return;
  }

  struct touch_point *point = get_touch_point(wm, id);
  if (point == NULL)
    return;

  point->event_mask  |= TOUCH_EVENT_DOWN;
  point->surface_x    = x;
  point->surface_y    = y;
  wm->touch_event.time   = time;
  wm->touch_event.serial = serial;

  glps_WaylandContext *context = __get_wl_context(wm);
  if (context == NULL)
  {
    LOG_ERROR("Couldn't fetch wayland context.");
    return;
  }
  context->touch_window_id = (size_t)window_id;
}

void wl_touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
                 uint32_t time, int32_t id)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_point *point  = get_touch_point(wm, id);
  if (point == NULL)
    return;

  point->event_mask |= TOUCH_EVENT_UP;
}

void wl_touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time,
                     int32_t id, wl_fixed_t x, wl_fixed_t y)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_point *point  = get_touch_point(wm, id);
  if (point == NULL)
    return;

  point->event_mask  |= TOUCH_EVENT_MOTION;
  point->surface_x    = x;
  point->surface_y    = y;
  wm->touch_event.time = time;
}

void wl_touch_cancel(void *data, struct wl_touch *wl_touch)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_event *touch  = &wm->touch_event;
  const size_t        nmemb  = sizeof(touch->points) / sizeof(struct touch_point);

  touch->event_mask |= TOUCH_EVENT_CANCEL;

  for (size_t i = 0; i < nmemb; ++i)
  {
    if (touch->points[i].valid)
      touch->points[i].event_mask |= TOUCH_EVENT_UP;
  }
}

void wl_touch_shape(void *data, struct wl_touch *wl_touch, int32_t id,
                    wl_fixed_t major, wl_fixed_t minor)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_point *point  = get_touch_point(wm, id);
  if (point == NULL)
    return;

  point->event_mask |= TOUCH_EVENT_SHAPE;
  point->major       = major;
  point->minor       = minor;
}

void wl_touch_orientation(void *data, struct wl_touch *wl_touch, int32_t id,
                          wl_fixed_t orientation)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_point *point  = get_touch_point(wm, id);
  if (point == NULL)
    return;

  point->event_mask  |= TOUCH_EVENT_ORIENTATION;
  point->orientation  = orientation;
}

void wl_touch_frame(void *data, struct wl_touch *wl_touch)
{
  if (data == NULL)
    return;

  glps_WindowManager *wm    = (glps_WindowManager *)data;
  struct touch_event *touch  = &wm->touch_event;
  const size_t        nmemb  = sizeof(touch->points) / sizeof(struct touch_point);

  glps_WaylandContext *context = __get_wl_context(wm);
  if (context == NULL)
  {
    LOG_ERROR("Couldn't fetch wayland context.");
    return;
  }

  for (size_t i = 0; i < nmemb; ++i)
  {
    struct touch_point *point = &touch->points[i];
    if (!point->valid)
      continue;
    if (point->event_mask == 0)
      continue;

    double major = (point->event_mask & TOUCH_EVENT_SHAPE)
                       ? wl_fixed_to_double(point->major)
                       : 0.0;
    double minor = (point->event_mask & TOUCH_EVENT_SHAPE)
                       ? wl_fixed_to_double(point->minor)
                       : 0.0;
    double orientation = (point->event_mask & TOUCH_EVENT_ORIENTATION)
                              ? wl_fixed_to_double(point->orientation)
                              : 0.0;
    double px = wl_fixed_to_double(point->surface_x);
    double py = wl_fixed_to_double(point->surface_y);

    bool had_down = point->event_mask & TOUCH_EVENT_DOWN;
    bool had_up   = point->event_mask & TOUCH_EVENT_UP;

    if (had_down && had_up)
    {
      if (wm->callbacks.touch_callback)
      {
        wm->callbacks.touch_callback(
            context->touch_window_id, point->id, px, py,
            true, major, minor, orientation,
            wm->callbacks.touch_data);
        wm->callbacks.touch_callback(
            context->touch_window_id, point->id, px, py,
            false, major, minor, orientation,
            wm->callbacks.touch_data);
      }
      point->valid = false;
    }
    else
    {
      bool is_down = had_up ? false : true;
      if (wm->callbacks.touch_callback)
      {
        wm->callbacks.touch_callback(
            context->touch_window_id, point->id, px, py,
            is_down, major, minor, orientation,
            wm->callbacks.touch_data);
      }
      if (had_up)
        point->valid = false;
    }

    point->event_mask = 0;
  }
}

struct wl_touch_listener wl_touch_listener = {
    .down        = wl_touch_down,
    .up          = wl_touch_up,
    .motion      = wl_touch_motion,
    .frame       = wl_touch_frame,
    .cancel      = wl_touch_cancel,
    .shape       = wl_touch_shape,
    .orientation = wl_touch_orientation,
};

void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
                           uint32_t capabilities)
{
  glps_WindowManager  *context = (glps_WindowManager *)data;
  if (context == NULL)
    return;

  glps_WaylandContext *ctx = __get_wl_context(context);
  if (ctx == NULL)
    return;

  bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (have_pointer && ctx->wl_pointer == NULL)
  {
    ctx->wl_pointer = wl_seat_get_pointer(wl_seat);
    if (ctx->wl_pointer != NULL)
      wl_pointer_add_listener(ctx->wl_pointer, &wl_pointer_listener, data);
    else
      LOG_ERROR("wl_seat_capabilities: failed to get wl_pointer");
  }
  else if (!have_pointer && ctx->wl_pointer != NULL)
  {
    wl_pointer_release(ctx->wl_pointer);
    ctx->wl_pointer = NULL;
  }

  bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
  if (have_keyboard && ctx->wl_keyboard == NULL)
  {
    ctx->wl_keyboard = wl_seat_get_keyboard(wl_seat);
    if (ctx->wl_keyboard != NULL)
      wl_keyboard_add_listener(ctx->wl_keyboard, &wl_keyboard_listener, data);
    else
      LOG_ERROR("wl_seat_capabilities: failed to get wl_keyboard");
  }
  else if (!have_keyboard && ctx->wl_keyboard != NULL)
  {
    wl_keyboard_release(ctx->wl_keyboard);
    ctx->wl_keyboard = NULL;
  }

  bool have_touch = capabilities & WL_SEAT_CAPABILITY_TOUCH;
  if (have_touch && ctx->wl_touch == NULL)
  {
    ctx->wl_touch = wl_seat_get_touch(wl_seat);
    if (ctx->wl_touch != NULL)
      wl_touch_add_listener(ctx->wl_touch, &wl_touch_listener, data);
    else
      LOG_ERROR("wl_seat_capabilities: failed to get wl_touch");
  }
  else if (!have_touch && ctx->wl_touch != NULL)
  {
    wl_touch_release(ctx->wl_touch);
    ctx->wl_touch = NULL;
  }
}

void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
  (void)data;
  (void)wl_seat;
  (void)name;
}

struct wl_seat_listener wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name         = wl_seat_name,
};

void handle_global(void *data, struct wl_registry *registry, uint32_t id,
                   const char *interface, uint32_t version)
{
  glps_WindowManager  *context = (glps_WindowManager *)data;
  if (context == NULL || context->wayland_ctx == NULL || interface == NULL)
    return;

  glps_WaylandContext *s = context->wayland_ctx;

  LOG_INFO("Attempting to bind interface: %s (id=%u, version=%u)",
           interface, id, version);

  if (strcmp(interface, "wl_compositor") == 0)
  {
    s->wl_compositor = wl_registry_bind(registry, id,
                                        &wl_compositor_interface, 4);
    if (!s->wl_compositor)
      LOG_ERROR("Failed to bind wl_compositor.");
    else
      LOG_INFO("Successfully bound wl_compositor.");
  }
  else if (strcmp(interface, "xdg_wm_base") == 0)
  {
    s->xdg_wm_base = wl_registry_bind(registry, id,
                                       &xdg_wm_base_interface, 1);
    if (!s->xdg_wm_base)
      LOG_ERROR("Failed to bind xdg_wm_base.");
    else
      LOG_INFO("Successfully bound xdg_wm_base.");
  }
  else if (strcmp(interface, "wl_shm") == 0)
  {
    s->wl_shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    if (!s->wl_shm)
      LOG_ERROR("Failed to bind wl_shm.");
    else
      LOG_INFO("Successfully bound wl_shm.");
  }
  else if (strcmp(interface, wl_seat_interface.name) == 0)
  {
    s->wl_seat = wl_registry_bind(registry, id, &wl_seat_interface, 5);
    if (s->wl_seat)
    {
      wl_seat_add_listener(s->wl_seat, &wl_seat_listener, data);
      LOG_INFO("Successfully bound wl_seat and added listener.");
    }
    else
    {
      LOG_ERROR("Failed to bind wl_seat.");
    }
  }
  else
  {
    LOG_WARNING("Unhandled interface: %s", interface);
  }
}

void handle_global_remove(void *data, struct wl_registry *registry,
                          uint32_t name)
{
  (void)data;
  (void)registry;
  (void)name;
}

struct wl_registry_listener registry_listener = {
    .global        = handle_global,
    .global_remove = handle_global_remove,
};

static void request_frame(glps_WaylandWindow *window, frame_callback_args *args)
{
  if (window == NULL || args == NULL)
    return;

  if (window->frame_callback)
    return;

  window->frame_callback = wl_surface_frame(window->wl_surface);
  if (window->frame_callback == NULL)
  {
    LOG_ERROR("request_frame: wl_surface_frame failed for window id: %zu",
              args->window_id);
    return;
  }

  wl_callback_add_listener(
      window->frame_callback,
      &frame_callback_listener,
      args
  );

  LOG_INFO("Frame callback set for window id: %zu", args->window_id);
}

void frame_callback_done(void *data, struct wl_callback *callback,
                         uint32_t time)
{
  (void)time;

  frame_callback_args *args = (frame_callback_args *)data;
  if (args == NULL || args->wm == NULL)
  {
    if (callback)
      wl_callback_destroy(callback);
    return;
  }

  if (!__is_valid_window_id(args->wm, args->window_id))
  {
    if (callback)
      wl_callback_destroy(callback);
    return;
  }

  glps_WaylandWindow *window = args->wm->windows[args->window_id];

  if (callback)
    wl_callback_destroy(callback);

 
  if (window->frame_callback == callback)
    window->frame_callback = NULL;

  if (args->wm->callbacks.window_frame_update_callback)
  {
    args->wm->callbacks.window_frame_update_callback(
        args->window_id, args->wm->callbacks.window_frame_update_data);
  }
}

struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_done,
};

void handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
  (void)states;

  glps_WindowManager *wm = (glps_WindowManager *)data;
  if (wm == NULL)
    return;

  ssize_t window_id = __get_window_id_from_xdg_toplevel(wm, toplevel);
  if (window_id < 0)
    return;

  if (!__is_valid_window_id(wm, (size_t)window_id))
    return;

  glps_WaylandWindow *window = wm->windows[window_id];

  if (width != 0 && height != 0)
  {
    window->properties.height = height;
    window->properties.width  = width;
    if (window->egl_window != NULL)
      wl_egl_window_resize(window->egl_window, width, height, 0, 0);
  }

  if (wm->callbacks.window_resize_callback)
  {
    wm->callbacks.window_resize_callback(window_id,
                                         window->properties.width,
                                         window->properties.height,
                                         wm->callbacks.window_resize_data);
  }
  if (window->egl_window != NULL)
    wl_update(wm, (size_t)window_id);
}

void handle_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
  glps_WindowManager *wm = (glps_WindowManager *)data;
  if (wm == NULL)
  {
    LOG_ERROR("Window Manager is NULL. Can't close window.");
    return;
  }

  ssize_t window_id = __get_window_id_from_xdg_toplevel(wm, toplevel);
  if (window_id < 0)
  {
    LOG_ERROR("Invalid Window ID. Can't close window.");
    return;
  }

  if (wm->callbacks.window_close_callback)
  {
    wm->callbacks.window_close_callback((size_t)window_id,
                                        wm->callbacks.window_close_data);
  }
}

void handle_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height)
{
  (void)data;
  (void)toplevel;
  (void)width;
  (void)height;
}

void handle_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                     struct wl_array *capabilities)
{
  (void)data;
  (void)toplevel;
  (void)capabilities;
}

struct xdg_toplevel_listener toplevel_listener = {
    .configure        = handle_toplevel_configure,
    .close            = handle_toplevel_close,
    .configure_bounds = handle_toplevel_configure_bounds,
    .wm_capabilities  = handle_toplevel_wm_capabilities,
};

void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                           uint32_t serial)
{
  glps_WindowManager *wm = (glps_WindowManager *)data;
  if (wm == NULL)
  {
    LOG_ERROR("Couldn't configure XDG Surface. Window Manager is NULL.");
    return;
  }

  glps_WaylandContext *ctx = __get_wl_context(wm);
  if (ctx == NULL)
  {
    LOG_ERROR("Couldn't configure XDG Surface. Wayland context is NULL.");
    return;
  }

  xdg_surface_ack_configure(xdg_surface, serial);

  ssize_t window_id = __get_window_id_from_xdg_surface(wm, xdg_surface);
  if (window_id < 0)
    return;

  if (!__is_valid_window_id(wm, (size_t)window_id))
    return;

  wm->windows[(size_t)window_id]->serial = serial;
}

struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void _cleanup_wl(glps_WindowManager *wm)
{
  if (wm == NULL)
    return;

  if (wm->windows != NULL)
  {
    for (size_t i = 0; i < wm->window_count; ++i)
    {
      if (wm->windows[i])
        __window_destroy(wm, i);
    }
    free(wm->windows);
    wm->windows = NULL;
  }
  wm->window_count = 0;

  if (wm->wayland_ctx != NULL)
  {
    if (wm->wayland_ctx->wl_seat != NULL)
    {
      wl_seat_destroy(wm->wayland_ctx->wl_seat);
      wm->wayland_ctx->wl_seat = NULL;
    }
    if (wm->wayland_ctx->xdg_wm_base != NULL)
    {
      xdg_wm_base_destroy(wm->wayland_ctx->xdg_wm_base);
      wm->wayland_ctx->xdg_wm_base = NULL;
    }
    if (wm->wayland_ctx->wl_compositor != NULL)
    {
      wl_compositor_destroy(wm->wayland_ctx->wl_compositor);
      wm->wayland_ctx->wl_compositor = NULL;
    }
    if (wm->wayland_ctx->wl_registry != NULL)
    {
      wl_registry_destroy(wm->wayland_ctx->wl_registry);
      wm->wayland_ctx->wl_registry = NULL;
    }
    if (wm->wayland_ctx->wl_keyboard != NULL)
    {
      wl_keyboard_destroy(wm->wayland_ctx->wl_keyboard);
      wm->wayland_ctx->wl_keyboard = NULL;
    }
    if (wm->wayland_ctx->wl_touch != NULL)
    {
      wl_touch_destroy(wm->wayland_ctx->wl_touch);
      wm->wayland_ctx->wl_touch = NULL;
    }
    if (wm->wayland_ctx->wl_pointer != NULL)
    {
      wl_pointer_destroy(wm->wayland_ctx->wl_pointer);
      wm->wayland_ctx->wl_pointer = NULL;
    }
    if (wm->wayland_ctx->xkb_keymap != NULL)
    {
      xkb_keymap_unref(wm->wayland_ctx->xkb_keymap);
      wm->wayland_ctx->xkb_keymap = NULL;
    }
    if (wm->wayland_ctx->xkb_state != NULL)
    {
      xkb_state_unref(wm->wayland_ctx->xkb_state);
      wm->wayland_ctx->xkb_state = NULL;
    }
    if (wm->wayland_ctx->xkb_context != NULL)
    {
      xkb_context_unref(wm->wayland_ctx->xkb_context);
      wm->wayland_ctx->xkb_context = NULL;
    }
    if (wm->wayland_ctx->wl_shm != NULL)
    {
      wl_shm_destroy(wm->wayland_ctx->wl_shm);
      wm->wayland_ctx->wl_shm = NULL;
    }
    if (wm->wayland_ctx->wl_display != NULL)
    {
      wl_display_disconnect(wm->wayland_ctx->wl_display);
      wm->wayland_ctx->wl_display = NULL;
    }
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
  }
}

ssize_t glps_wl_window_create(glps_WindowManager *wm, const char *title,
                              int x, int y, int width, int height)
{
  (void)x;
  (void)y;

  if (wm == NULL || wm->windows == NULL || wm->wayland_ctx == NULL || title == NULL)
  {
    LOG_ERROR("glps_wl_window_create: invalid arguments or uninitialized "
              "window manager");
    return -1;
  }

  if (wm->window_count >= (size_t)MAX_WINDOWS)
  {
    LOG_ERROR("glps_wl_window_create: maximum number of windows (%d) reached",
              MAX_WINDOWS);
    return -1;
  }

  glps_WaylandWindow *window = malloc(sizeof(glps_WaylandWindow));
  if (window == NULL)
  {
    LOG_ERROR("Wayland window allocation failed.");
    return -1;
  }
  memset(window, 0, sizeof(glps_WaylandWindow));
  window->egl_surface = EGL_NO_SURFACE;

  window->wl_surface =
      wl_compositor_create_surface(wm->wayland_ctx->wl_compositor);
  if (!window->wl_surface)
  {
    LOG_ERROR("Failed to create wayland surface");
    free(window);
    return -1;
  }

  window->properties.width  = width;
  window->properties.height = height;
  strncpy(window->properties.title, title,
          sizeof(window->properties.title) - 1);
  window->properties.title[sizeof(window->properties.title) - 1] = '\0';

  window->xdg_surface = xdg_wm_base_get_xdg_surface(
      wm->wayland_ctx->xdg_wm_base, window->wl_surface);
  if (!window->xdg_surface)
  {
    LOG_ERROR("Failed to create XDG surface");
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  if (xdg_surface_add_listener(window->xdg_surface,
                               &xdg_surface_listener, wm) == -1)
  {
    LOG_ERROR("Failed to add XDG surface listener");
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
  if (!window->xdg_toplevel)
  {
    LOG_ERROR("Failed to create toplevel");
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  xdg_toplevel_set_title(window->xdg_toplevel, title);
  xdg_toplevel_add_listener(window->xdg_toplevel, &toplevel_listener, wm);

  wl_surface_commit(window->wl_surface);
  LOG_INFO("Committing surface for window id %zu", wm->window_count);
  wl_display_roundtrip(wm->wayland_ctx->wl_display);
  LOG_INFO("Surface committed for window id %zu", wm->window_count);

  if (wm->window_count == 0)
  {
    glps_egl_create_ctx(wm);
  }

  if (wm->egl_ctx == NULL)
  {
    LOG_ERROR("glps_wl_window_create: no EGL context available");
    xdg_toplevel_destroy(window->xdg_toplevel);
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  window->egl_window = wl_egl_window_create(
      window->wl_surface, window->properties.width, window->properties.height);
  if (!window->egl_window)
  {
    LOG_ERROR("Failed to create EGL window");
    xdg_toplevel_destroy(window->xdg_toplevel);
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  window->egl_surface =
      eglCreateWindowSurface(wm->egl_ctx->dpy, wm->egl_ctx->conf,
                             (NativeWindowType)window->egl_window, NULL);
  if (window->egl_surface == EGL_NO_SURFACE)
  {
    LOG_ERROR("Failed to create EGL surface (eglGetError: 0x%x)", eglGetError());
    wl_egl_window_destroy(window->egl_window);
    xdg_toplevel_destroy(window->xdg_toplevel);
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    free(window);
    return -1;
  }

  size_t new_window_id   = wm->window_count;
  wm->windows[new_window_id] = window;

  glps_egl_make_ctx_current(wm, new_window_id);

  frame_callback_args *frame_args = malloc(sizeof(frame_callback_args));
  if (frame_args == NULL)
  {
    LOG_ERROR("Failed to allocate frame_callback_args");
    eglDestroySurface(wm->egl_ctx->dpy, window->egl_surface);
    wl_egl_window_destroy(window->egl_window);
    xdg_toplevel_destroy(window->xdg_toplevel);
    xdg_surface_destroy(window->xdg_surface);
    wl_surface_destroy(window->wl_surface);
    wm->windows[new_window_id] = NULL;
    free(window);
    return -1;
  }
  frame_args->wm         = wm;
  frame_args->window_id  = new_window_id;
  window->frame_args     = (void *)frame_args;

  request_frame(window, frame_args);

  if (eglSwapBuffers(wm->egl_ctx->dpy, window->egl_surface) == EGL_FALSE)
  {
    LOG_ERROR("Initial eglSwapBuffers failed for window id %zu (eglGetError: 0x%x)",
              new_window_id, eglGetError());
  }

  wm->window_count = new_window_id + 1;
  return (ssize_t)new_window_id;
}

void glps_wl_window_is_resizable(glps_WindowManager *wm, bool state,
                                 size_t window_id)
{
  if (!__is_valid_window_id(wm, window_id))
  {
    LOG_ERROR("Couldn't change resize hint on window with id %zu", window_id);
    return;
  }

  glps_WaylandWindow *window = wm->windows[window_id];

  int window_width  = window->properties.width;
  int window_height = window->properties.height;
  xdg_toplevel_set_min_size(window->xdg_toplevel,
                            state ? 0 : window_width,
                            state ? 0 : window_height);
  xdg_toplevel_set_max_size(window->xdg_toplevel,
                            state ? INT32_MAX : window_width,
                            state ? INT32_MAX : window_height);
}

bool glps_wl_should_close(glps_WindowManager *wm)
{
  if (wm == NULL || wm->wayland_ctx == NULL || wm->wayland_ctx->wl_display == NULL)
    return true;

  if (wm->should_close)
    return true;

  int ret = wl_display_dispatch(wm->wayland_ctx->wl_display);
  if (ret == -1)
  {
    int err = wl_display_get_error(wm->wayland_ctx->wl_display);
    if (err != 0)
      LOG_ERROR("Wayland display error: %d", err);
    return true;
  }

  return false;
}

void glps_wl_destroy(glps_WindowManager *wm)
{
  if (wm == NULL)
    return;
  glps_egl_destroy(wm);
  _cleanup_wl(wm);
}

void glps_wl_window_destroy(glps_WindowManager *wm, size_t window_id)
{
  __window_destroy(wm, window_id);
}

bool glps_wl_init(glps_WindowManager *wm)
{
  if (wm == NULL)
    return false;

  wm->windows = malloc(sizeof(glps_WaylandWindow *) * MAX_WINDOWS);
  if (!wm->windows)
  {
    LOG_ERROR("Failed to allocate memory for windows array");
    return false;
  }
  memset(wm->windows, 0, sizeof(glps_WaylandWindow *) * MAX_WINDOWS);

  wm->wayland_ctx = malloc(sizeof(glps_WaylandContext));
  if (!wm->wayland_ctx)
  {
    LOG_ERROR("Failed to allocate memory for Wayland context");
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }
  *wm->wayland_ctx = (glps_WaylandContext){0};

  wm->window_count             = 0;
  wm->wayland_ctx->wl_touch    = NULL;
  wm->wayland_ctx->wl_pointer  = NULL;
  wm->wayland_ctx->wl_keyboard = NULL;
  wm->wayland_ctx->xkb_state   = NULL;
  wm->wayland_ctx->xkb_keymap  = NULL;
  wm->wayland_ctx->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (wm->wayland_ctx->xkb_context == NULL)
  {
    LOG_ERROR("Failed to create xkb context");
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }

  wm->wayland_ctx->wl_display = wl_display_connect(NULL);
  if (!wm->wayland_ctx->wl_display)
  {
    LOG_ERROR("Failed to connect to Wayland display");
    xkb_context_unref(wm->wayland_ctx->xkb_context);
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }

  wm->wayland_ctx->wl_registry =
      wl_display_get_registry(wm->wayland_ctx->wl_display);
  if (!wm->wayland_ctx->wl_registry)
  {
    LOG_ERROR("Failed to get Wayland registry");
    wl_display_disconnect(wm->wayland_ctx->wl_display);
    xkb_context_unref(wm->wayland_ctx->xkb_context);
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }

  if (wl_registry_add_listener(wm->wayland_ctx->wl_registry,
                               &registry_listener, wm) != 0)
  {
    LOG_ERROR("Failed to add registry listener");
    wl_registry_destroy(wm->wayland_ctx->wl_registry);
    wl_display_disconnect(wm->wayland_ctx->wl_display);
    xkb_context_unref(wm->wayland_ctx->xkb_context);
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }

  wl_display_roundtrip(wm->wayland_ctx->wl_display);

  if (wm->wayland_ctx->xdg_wm_base)
  {
    xdg_wm_base_add_listener(wm->wayland_ctx->xdg_wm_base,
                             &xdg_wm_base_listener, NULL);
  }
  else
  {
    LOG_WARNING("xdg_wm_base protocol not supported by compositor");
  }

  if (!wm->wayland_ctx->wl_compositor || !wm->wayland_ctx->xdg_wm_base)
  {
    LOG_ERROR("Failed to retrieve Wayland compositor or xdg_wm_base");

    if (wm->wayland_ctx->wl_seat != NULL)
      wl_seat_destroy(wm->wayland_ctx->wl_seat);
    if (wm->wayland_ctx->wl_shm != NULL)
      wl_shm_destroy(wm->wayland_ctx->wl_shm);
    if (wm->wayland_ctx->xdg_wm_base != NULL)
      xdg_wm_base_destroy(wm->wayland_ctx->xdg_wm_base);
    if (wm->wayland_ctx->wl_compositor != NULL)
      wl_compositor_destroy(wm->wayland_ctx->wl_compositor);

    wl_registry_destroy(wm->wayland_ctx->wl_registry);
    wl_display_disconnect(wm->wayland_ctx->wl_display);
    xkb_context_unref(wm->wayland_ctx->xkb_context);
    free(wm->wayland_ctx);
    wm->wayland_ctx = NULL;
    free(wm->windows);
    wm->windows = NULL;
    return false;
  }

  return true;
}

void glps_wl_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE user_cursor)
{
  (void)wm;
  (void)user_cursor;
}