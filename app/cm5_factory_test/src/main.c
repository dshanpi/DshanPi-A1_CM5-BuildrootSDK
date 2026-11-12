#define _GNU_SOURCE

#include "factory.h"
#include "hardware.h"
#include "xdg-shell-client-protocol.h"
#include <sys/wait.h>

#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/memfd.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BUFFER_COUNT 3
enum action {
  A_EXIT = 1,
  A_SAVE,
  A_SCAN,
  A_AUTO,
  A_RUN,
  A_PASS,
  A_FAIL,
  A_SKIP,
  A_RESET,
  A_PATTERN,
  A_TOUCH,
  A_PATTERN_BACK,
  A_SELECT = 200
};
struct color {
  double r, g, b, a;
};
struct shm_buffer {
  struct wl_buffer *buffer;
  void *data;
  size_t size;
  int width, height, stride, busy;
};
struct hit_target {
  double x, y, width, height;
  enum action action;
};
struct app {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct wl_pointer *pointer;
  struct wl_touch *touch;
  struct wl_keyboard *keyboard;
  struct xdg_wm_base *wm_base;
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *toplevel;
  struct shm_buffer buffers[BUFFER_COUNT];
  int width, height, pending_width, pending_height, configured, running, redraw;
  double pointer_x, pointer_y;
  struct hit_target hits[100];
  int hit_count, selected, pattern, pattern_test, touch_mask, touch_event,
      reset_armed, operator_selected;
  struct factory factory;
  struct video_capture *video;
  int camera_active;
  unsigned int camera_frames;
  double camera_started;
  double last_scan;
  unsigned int saved_revision;
  char report_path[512];
};
static const struct color COLOR_NAVY = {0.055, 0.086, 0.145, 1.0};
static const struct color COLOR_TEXT = {0.075, 0.106, 0.165, 1.0};
static const struct color COLOR_MUTED = {0.30, 0.36, 0.44, 1.0};
static const struct color COLOR_WHITE = {1.0, 1.0, 1.0, 1.0};
static const struct color COLOR_BG = {0.945, 0.961, 0.980, 1.0};
static const struct color COLOR_BORDER = {0.855, 0.886, 0.925, 1.0};
static const struct color COLOR_BLUE = {0.169, 0.431, 0.925, 1.0};
static const struct color COLOR_GREEN = {0.025, 0.43, 0.23, 1.0};
static const struct color COLOR_RED = {0.72, 0.10, 0.16, 1.0};

static double monotonic_seconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec + now.tv_nsec / 1000000000.0;
}

static void set_source(cairo_t *cr, struct color color) {
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
}

static void rounded_rectangle(cairo_t *cr, double x, double y, double width,
                              double height, double radius) {
  double r = fmin(radius, fmin(width, height) / 2.0);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + width - r, y + r, r, -M_PI / 2.0, 0);
  cairo_arc(cr, x + width - r, y + height - r, r, 0, M_PI / 2.0);
  cairo_arc(cr, x + r, y + height - r, r, M_PI / 2.0, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
  cairo_close_path(cr);
}

static void fill_round(cairo_t *cr, double x, double y, double width,
                       double height, double radius, struct color color) {
  rounded_rectangle(cr, x, y, width, height, radius);
  set_source(cr, color);
  cairo_fill(cr);
}

static void stroke_round(cairo_t *cr, double x, double y, double width,
                         double height, double radius, struct color color,
                         double line_width) {
  rounded_rectangle(cr, x, y, width, height, radius);
  set_source(cr, color);
  cairo_set_line_width(cr, line_width);
  cairo_stroke(cr);
}

static void draw_text(cairo_t *cr, const char *text, double x, double y,
                      double width, double size, int bold, struct color color,
                      PangoAlignment alignment) {
  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *description = pango_font_description_new();

  pango_font_description_set_family(description, "Source Han Sans CN");
  pango_font_description_set_absolute_size(description, size * PANGO_SCALE);
  pango_font_description_set_weight(description, bold ? PANGO_WEIGHT_BOLD
                                                      : PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, description);
  pango_layout_set_text(layout, text ? text : "", -1);
  pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(layout, alignment);
  set_source(cr, color);
  cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, layout);
  pango_font_description_free(description);
  g_object_unref(layout);
}

static void draw_centered_text(cairo_t *cr, const char *text, double x,
                               double y, double width, double height,
                               double size, int bold, struct color color) {
  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *description = pango_font_description_new();
  PangoRectangle logical;

  pango_font_description_set_family(description, "Source Han Sans CN");
  pango_font_description_set_absolute_size(description, size * PANGO_SCALE);
  pango_font_description_set_weight(description, bold ? PANGO_WEIGHT_BOLD
                                                      : PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, description);
  pango_layout_set_text(layout, text ? text : "", -1);
  pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
  pango_layout_get_pixel_extents(layout, NULL, &logical);
  set_source(cr, color);
  cairo_move_to(cr, x, y + (height - logical.height) / 2.0 - logical.y);
  pango_cairo_show_layout(cr, layout);
  pango_font_description_free(description);
  g_object_unref(layout);
}

static void add_hit(struct app *app, double x, double y, double width,
                    double height, enum action action) {
  struct hit_target *hit;
  if (app->hit_count >= (int)ARRAY_SIZE(app->hits))
    return;
  hit = &app->hits[app->hit_count++];
  hit->x = x;
  hit->y = y;
  hit->width = width;
  hit->height = height;
  hit->action = action;
}

static void draw_button(struct app *app, cairo_t *cr, const char *label,
                        double x, double y, double width, double height,
                        struct color fill, struct color text_color,
                        enum action action, int outline) {
  if (outline) {
    fill_round(cr, x, y, width, height, 13, COLOR_WHITE);
    stroke_round(cr, x, y, width, height, 13, fill, 2);
  } else {

    fill_round(cr, x, y, width, height, 13, fill);
  }
  draw_centered_text(cr, label, x, y, width, height, 18, 1, text_color);
  add_hit(app, x, y, width, height, action);
}

#include "ui.inc"
static int create_anonymous_file(size_t size) {
  int fd = (int)syscall(SYS_memfd_create, "cm5-factory-ui", MFD_CLOEXEC);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, (off_t)size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  struct shm_buffer *buffer = data;
  (void)wl_buffer;
  buffer->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void destroy_buffers(struct app *app) {
  int i;
  for (i = 0; i < BUFFER_COUNT; ++i) {
    if (app->buffers[i].buffer)
      wl_buffer_destroy(app->buffers[i].buffer);
    if (app->buffers[i].data)
      munmap(app->buffers[i].data, app->buffers[i].size);
    memset(&app->buffers[i], 0, sizeof(app->buffers[i]));
  }
}

static int create_buffers(struct app *app) {
  int i;
  int stride = app->width * 4;
  size_t size = (size_t)stride * app->height;

  destroy_buffers(app);
  for (i = 0; i < BUFFER_COUNT; ++i) {
    struct wl_shm_pool *pool;
    int fd = create_anonymous_file(size);
    if (fd < 0)
      return -1;
    app->buffers[i].data =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (app->buffers[i].data == MAP_FAILED) {
      app->buffers[i].data = NULL;
      close(fd);
      return -1;
    }
    pool = wl_shm_create_pool(app->shm, fd, (int)size);
    app->buffers[i].buffer = wl_shm_pool_create_buffer(
        pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(app->buffers[i].buffer, &buffer_listener,
                           &app->buffers[i]);
    wl_shm_pool_destroy(pool);
    close(fd);
    app->buffers[i].size = size;
    app->buffers[i].width = app->width;
    app->buffers[i].height = app->height;
    app->buffers[i].stride = stride;
  }
  return 0;
}

static void render(struct app *app) {
  struct shm_buffer *buffer = NULL;
  cairo_surface_t *surface;
  cairo_t *cr;
  int i;

  if (!app->configured || app->width < 320 || app->height < 240)
    return;
  if (!app->buffers[0].buffer && create_buffers(app) < 0) {
    fprintf(stderr, "无法创建 Wayland 图形缓冲区\n");
    app->running = 0;
    return;
  }
  if (app->buffers[0].width != app->width ||
      app->buffers[0].height != app->height) {
    int busy = 0;
    for (i = 0; i < BUFFER_COUNT; ++i)
      busy |= app->buffers[i].busy;
    if (busy)
      return;
    if (create_buffers(app) < 0)
      return;
  }
  for (i = 0; i < BUFFER_COUNT; ++i) {
    if (!app->buffers[i].busy) {
      buffer = &app->buffers[i];
      break;
    }
  }
  if (!buffer)
    return;
  surface = cairo_image_surface_create_for_data(buffer->data,
                                                CAIRO_FORMAT_ARGB32, app->width,
                                                app->height, buffer->stride);
  cr = cairo_create(surface);
  draw_ui(app, cr);
  cairo_destroy(cr);
  cairo_surface_flush(surface);
  cairo_surface_destroy(surface);
  buffer->busy = 1;
  wl_surface_attach(app->surface, buffer->buffer, 0, 0);
  if (wl_surface_get_version(app->surface) >= 4)
    wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);
  else
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
  wl_surface_commit(app->surface);
  app->redraw = 0;
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
  struct app *app = data;
  (void)pointer;
  (void)serial;
  (void)surface;
  app->pointer_x = wl_fixed_to_double(sx);
  app->pointer_y = wl_fixed_to_double(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
  (void)data;
  (void)pointer;
  (void)serial;
  (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
  struct app *app = data;
  (void)pointer;
  (void)time;
  app->pointer_x = wl_fixed_to_double(sx);
  app->pointer_y = wl_fixed_to_double(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  struct app *app = data;
  (void)pointer;
  (void)serial;
  (void)time;
  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED)
    activate_at(app, app->pointer_x, app->pointer_y);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
  (void)data;
  (void)pointer;
  (void)time;
  (void)axis;
  (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void touch_down(void *data, struct wl_touch *touch, uint32_t serial,
                       uint32_t time, struct wl_surface *surface, int32_t id,
                       wl_fixed_t x, wl_fixed_t y) {
  struct app *app = data;
  (void)touch;
  (void)serial;
  (void)time;
  (void)surface;
  (void)id;
  app->touch_event = 1;
  activate_at(app, wl_fixed_to_double(x), wl_fixed_to_double(y));
  app->touch_event = 0;
}

static void touch_up(void *data, struct wl_touch *touch, uint32_t serial,
                     uint32_t time, int32_t id) {
  (void)data;
  (void)touch;
  (void)serial;
  (void)time;
  (void)id;
}

static void touch_motion(void *data, struct wl_touch *touch, uint32_t time,
                         int32_t id, wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)touch;
  (void)time;
  (void)id;
  (void)x;
  (void)y;
}

static void touch_frame(void *data, struct wl_touch *touch) {
  (void)data;
  (void)touch;
}

static void touch_cancel(void *data, struct wl_touch *touch) {
  (void)data;
  (void)touch;
}

static const struct wl_touch_listener touch_listener = {
    .down = touch_down,
    .up = touch_up,
    .motion = touch_motion,
    .frame = touch_frame,
    .cancel = touch_cancel,
};

static void keyboard_keymap(void *data, struct wl_keyboard *k, uint32_t format,
                            int32_t fd, uint32_t size) {
  (void)data;
  (void)k;
  (void)format;
  (void)size;
  close(fd);
}
static void keyboard_enter(void *data, struct wl_keyboard *k, uint32_t serial,
                           struct wl_surface *s, struct wl_array *keys) {
  (void)data;
  (void)k;
  (void)serial;
  (void)s;
  (void)keys;
}
static void keyboard_leave(void *data, struct wl_keyboard *k, uint32_t serial,
                           struct wl_surface *s) {
  (void)data;
  (void)k;
  (void)serial;
  (void)s;
}
static void keyboard_key(void *data, struct wl_keyboard *k, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
  struct app *a = data;
  (void)k;
  (void)serial;
  (void)time;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
    return;
  if (a->selected == T_KEY) {
    snprintf(a->factory.tests[T_KEY].evidence,
             sizeof(a->factory.tests[T_KEY].evidence),
             "Wayland按键码 %u；请人工核对是否来自底板按键。", key);
    a->redraw = 1;
  }
  if (key == KEY_ESC) {
    a->pattern = 0;
    a->redraw = 1;
    return;
  }
  if (a->pattern)
    return;
  if (key == KEY_ENTER)
    perform_action(a, A_RUN);
  else if (key == KEY_P)
    perform_action(a, A_PASS);
  else if (key == KEY_F)
    perform_action(a, A_FAIL);
  else if (key == KEY_S)
    perform_action(a, A_SAVE);
  else if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT ||
           key == KEY_RIGHT) {
    int step = key == KEY_UP     ? -TEST_GRID_COLUMNS
               : key == KEY_DOWN ? TEST_GRID_COLUMNS
               : key == KEY_LEFT ? -1
                                 : 1;
    int index = 0;
    while (index < ACTIVE_TEST_COUNT &&
           factory_active_tests[index] != a->selected)
      index++;
    int next = index + step;
    if (next >= 0 && next < ACTIVE_TEST_COUNT)
      perform_action(a, A_SELECT + factory_active_tests[next]);
  }
}
static void keyboard_modifiers(void *data, struct wl_keyboard *k,
                               uint32_t serial, uint32_t depressed,
                               uint32_t latched, uint32_t locked,
                               uint32_t group) {
  (void)data;
  (void)k;
  (void)serial;
  (void)depressed;
  (void)latched;
  (void)locked;
  (void)group;
}
static void keyboard_repeat(void *data, struct wl_keyboard *k, int32_t rate,
                            int32_t delay) {
  (void)data;
  (void)k;
  (void)rate;
  (void)delay;
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities) {
  struct app *app = data;
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
    app->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
  }
  if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && app->keyboard) {
    wl_keyboard_destroy(app->keyboard);
    app->keyboard = NULL;
  }
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
    app->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app->pointer, &pointer_listener, app);
  } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
    wl_pointer_destroy(app->pointer);
    app->pointer = NULL;
  }
  if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !app->touch) {
    app->touch = wl_seat_get_touch(seat);
    wl_touch_add_listener(app->touch, &touch_listener, app);
  } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && app->touch) {
    wl_touch_destroy(app->touch);
    app->touch = NULL;
  }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
  struct app *app = data;
  if (!strcmp(interface, wl_compositor_interface.name))
    app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                       version < 4 ? version : 4);
  else if (!strcmp(interface, wl_shm_interface.name))
    app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  else if (!strcmp(interface, xdg_wm_base_interface.name)) {
    app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
  } else if (!strcmp(interface, wl_seat_interface.name)) {
    app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                 version < 5 ? version : 5);
    wl_seat_add_listener(app->seat, &seat_listener, app);
  }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name) {
  (void)data;
  (void)registry;
  (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states) {
  struct app *app = data;
  (void)toplevel;
  (void)states;
  if (width > 0)
    app->pending_width = width;
  if (height > 0)
    app->pending_height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
  struct app *app = data;
  (void)toplevel;
  app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void surface_configure(void *data, struct xdg_surface *xdg_surface,
                              uint32_t serial) {
  struct app *app = data;
  xdg_surface_ack_configure(xdg_surface, serial);
  app->width = app->pending_width > 0 ? app->pending_width : 1024;
  app->height = app->pending_height > 0 ? app->pending_height : 768;
  app->configured = 1;
  app->redraw = 1;
}

static const struct xdg_surface_listener surface_listener = {
    .configure = surface_configure,
};

static int initialize_wayland(struct app *app) {
  app->display = wl_display_connect(NULL);
  if (!app->display) {
    fprintf(stderr, "无法连接 Wayland。请在 Weston 桌面中启动本程序。\n");
    return -1;
  }
  app->registry = wl_display_get_registry(app->display);
  wl_registry_add_listener(app->registry, &registry_listener, app);
  wl_display_roundtrip(app->display);
  wl_display_roundtrip(app->display);
  if (!app->compositor || !app->shm || !app->wm_base) {
    fprintf(stderr, "Wayland 合成器缺少 wl_shm 或 xdg-shell 支持。\n");
    return -1;
  }
  app->surface = wl_compositor_create_surface(app->compositor);
  app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
  xdg_surface_add_listener(app->xdg_surface, &surface_listener, app);
  app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
  xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
  xdg_toplevel_set_title(app->toplevel, "CM5 底板测试");
  xdg_toplevel_set_app_id(app->toplevel, "com.100ask.cm5-factory-test");
  xdg_toplevel_set_fullscreen(app->toplevel, NULL);
  wl_surface_commit(app->surface);
  return 0;
}

static void cleanup(struct app *app) {
  factory_destroy(&app->factory);
  if (app->keyboard)
    wl_keyboard_destroy(app->keyboard);
  video_capture_destroy(app->video);
  destroy_buffers(app);
  if (app->touch)
    wl_touch_destroy(app->touch);
  if (app->pointer)
    wl_pointer_destroy(app->pointer);
  if (app->seat)
    wl_seat_destroy(app->seat);
  if (app->toplevel)
    xdg_toplevel_destroy(app->toplevel);
  if (app->xdg_surface)
    xdg_surface_destroy(app->xdg_surface);
  if (app->surface)
    wl_surface_destroy(app->surface);
  if (app->wm_base)
    xdg_wm_base_destroy(app->wm_base);
  if (app->shm)
    wl_shm_destroy(app->shm);
  if (app->compositor)
    wl_compositor_destroy(app->compositor);
  if (app->registry)
    wl_registry_destroy(app->registry);
  if (app->display)
    wl_display_disconnect(app->display);
}

static volatile sig_atomic_t interrupted;
static void quit_signal(int sig) {
  (void)sig;
  interrupted = 1;
}
static void periodic_update(struct app *a) {
  factory_poll(&a->factory);
  if (!a->operator_selected && a->factory.auto_running && a->factory.job_pid)
    a->selected = a->factory.job_test;
  if (a->saved_revision != a->factory.revision) {
    a->saved_revision = a->factory.revision;
    persist_report(a);
    a->redraw = 1;
  }
  double now = monotonic_seconds();
  if (now - a->last_scan >= 1) {
    factory_scan(&a->factory);
    a->last_scan = now;
    a->redraw = 1;
  }
  if (a->factory.job_pid)
    a->redraw = 1;
  if (a->camera_active >= 0 && !a->camera_frames &&
      now - a->camera_started > 8) {
    struct test_item *t = &a->factory.tests[a->camera_active];
    t->state = FAIL;
    t->checked = 0;
    snprintf(t->evidence, sizeof(t->evidence),
             "8秒内未收到视频帧；检查ISP节点、传感器及media链路。");
    stop_camera(a);
    a->redraw = 1;
  }
}
static int run(struct app *app) {
  int display_fd = wl_display_get_fd(app->display);

  while (app->running && !interrupted) {
    struct pollfd descriptors[2];
    int count = 1;
    int video_fd = video_capture_fd(app->video);
    int result;

    wl_display_dispatch_pending(app->display);
    if (app->redraw)
      render(app);
    wl_display_flush(app->display);
    descriptors[0].fd = display_fd;
    descriptors[0].events = POLLIN;
    descriptors[0].revents = 0;
    if (video_fd >= 0) {
      descriptors[1].fd = video_fd;
      descriptors[1].events = POLLIN | POLLERR;
      descriptors[1].revents = 0;
      count = 2;
    }
    result = poll(descriptors, count, app->factory.job_pid ? 70 : 120);
    if (result < 0 && errno != EINTR)
      return -1;
    if (descriptors[0].revents & (POLLERR | POLLHUP))
      return -1;
    if (descriptors[0].revents & POLLIN) {
      if (wl_display_dispatch(app->display) < 0)
        return -1;
    }
    if (count == 2 && app->camera_active >= 0 &&
        video_capture_fd(app->video) == video_fd &&
        (descriptors[1].revents & POLLIN)) {
      int frame = video_capture_read_frame(app->video);
      if (frame > 0) {
        app->camera_frames++;
        struct test_item *t = &app->factory.tests[app->camera_active];
        t->checked = 1;
        t->detected = 1;
        if (t->state != PASS)
          t->state = READY;
        snprintf(t->evidence, sizeof(t->evidence),
                 "已实际取帧 %u：%s；仍需人工检查画面。", app->camera_frames,
                 video_capture_format(app->video));
        app->redraw = 1;
      } else if (frame < 0) {
        app->factory.tests[app->camera_active].state = FAIL;
        app->factory.tests[app->camera_active].checked = 0;
        snprintf(app->factory.tests[app->camera_active].evidence,
                 sizeof(app->factory.tests[app->camera_active].evidence),
                 "取帧失败：%s", video_capture_error(app->video));
        stop_camera(app);
        app->redraw = 1;
      }
    }
    if (count == 2 && video_capture_fd(app->video) == video_fd &&
        (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
      if (app->camera_active >= 0) {
        struct test_item *t = &app->factory.tests[app->camera_active];
        t->state = FAIL;
        t->checked = 0;
        snprintf(t->evidence, sizeof(t->evidence),
                 "摄像头流异常，请检查连线/驱动。");
        stop_camera(app);
      }
      app->redraw = 1;
    }
    periodic_update(app);
  }
  return 0;
}

int main(int argc, char **argv) {
  struct app a;
  memset(&a, 0, sizeof(a));
  factory_init(&a.factory);
  a.running = 1;
  a.width = a.pending_width = 1024;
  a.height = a.pending_height = 768;
  a.camera_active = -1;
  const char *config = "/etc/cm5-factory-test.conf", *preview = NULL;
  int explicit_config = 0, scan = 0, automatic = 0;
  snprintf(a.report_path, sizeof(a.report_path),
           "/tmp/cm5-factory-report-%lu-%ld.json", a.factory.session_id,
           (long)getpid());
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help")) {
      puts("cm5-factory-test [--config FILE] [--report FILE] [--scan] "
           "[--auto] [--probe-root DIR] [--render-preview DIR]\n--scan "
           "仅只读枚举并导出JSON，不打开Wayland或执行硬件动作。");
      return 0;
    } else if (!strcmp(argv[i], "--auto"))
      automatic = 1;
    else if (!strcmp(argv[i], "--scan"))
      scan = 1;
    else if (i + 1 < argc && !strcmp(argv[i], "--config")) {
      config = argv[++i];
      explicit_config = 1;
    } else if (i + 1 < argc && !strcmp(argv[i], "--report")) {
      if (strlen(argv[i + 1]) >= sizeof(a.report_path))
        return 2;
      snprintf(a.report_path, sizeof(a.report_path), "%s", argv[++i]);
    } else if (i + 1 < argc && !strcmp(argv[i], "--probe-root")) {
      if (strlen(argv[i + 1]) >= sizeof(a.factory.config.root))
        return 2;
      snprintf(a.factory.config.root, sizeof(a.factory.config.root), "%s",
               argv[++i]);
    } else if (i + 1 < argc && !strcmp(argv[i], "--render-preview"))
      preview = argv[++i];
    else {
      fprintf(stderr, "未知参数或缺少参数值：%s\n", argv[i]);
      return 2;
    }
  }
  int rc = factory_load_config(&a.factory, config);
  if (rc == -2 || (rc < 0 && explicit_config)) {
    fprintf(stderr, "读取配置失败：%s / %s\n", config, a.factory.message);
    return 2;
  }
  if ((automatic && (*a.factory.config.root || scan || preview)) ||
      (*a.factory.config.root && !scan && !preview)) {
    fprintf(stderr, "--probe-root仅用于--scan诊断/预览\n");
    return 2;
  }
  factory_scan(&a.factory);
  if (scan) {
    if (factory_save(&a.factory, a.report_path)) {
      perror(a.report_path);
      return 1;
    }
    printf("只读检测报告：%s\n", a.report_path);
    return 0;
  }
  if (automatic) {
    signal(SIGTERM, quit_signal);
    signal(SIGINT, quit_signal);
    if (factory_auto_begin(&a.factory, a.report_path)) {
      fprintf(stderr, "无法启动自动测试：报告目录不可写或任务忙\n");
      return 1;
    }
    unsigned revision = 0;
    while (a.factory.auto_running && !interrupted) {
      factory_poll(&a.factory);
      if (revision != a.factory.revision) {
        revision = a.factory.revision;
        printf("%s\n", a.factory.message);
        if (factory_save(&a.factory, a.report_path)) {
          perror("保存自动测试报告");
          factory_auto_cancel(&a.factory);
          factory_destroy(&a.factory);
          return 1;
        }
      }
      usleep(50000);
    }
    factory_auto_cancel(&a.factory);
    int failed = interrupted ? 1 : 0;
    for (int j = 0; j < TEST_COUNT; j++)
      failed |= a.factory.tests[j].state == FAIL;
    if (factory_save(&a.factory, a.report_path))
      failed = 1;
    factory_destroy(&a.factory);
    printf("自动测试报告：%s\n", a.report_path);
    return failed;
  }
  a.video = video_capture_create();
  if (!a.video)
    return 1;
  if (preview) {
    {
      cairo_surface_t *s =
          cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1024, 768);
      cairo_t *cr = cairo_create(s);
      draw_ui(&a, cr);
      cairo_destroy(cr);
      char name[1024];
      snprintf(name, sizeof(name), "%s/overview.png", preview);
      cairo_status_t status = cairo_surface_write_to_png(s, name);
      cairo_surface_destroy(s);
      if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "截图导出失败：%s\n", cairo_status_to_string(status));
        cleanup(&a);
        return 1;
      }
    }
    cleanup(&a);
    return 0;
  }
  signal(SIGTERM, quit_signal);
  signal(SIGINT, quit_signal);
  if (initialize_wayland(&a)) {
    cleanup(&a);
    return 1;
  }
  if (!getenv("CM5_AUTO_START") || strcmp(getenv("CM5_AUTO_START"), "0"))
    perform_action(&a, A_AUTO);
  rc = run(&a);
  factory_auto_cancel(&a.factory);
  if (factory_save(&a.factory, a.report_path)) {
    perror("保存退出报告");
    rc = -1;
  } else
    printf("报告：%s\n", a.report_path);
  cleanup(&a);
  return rc < 0 ? 1 : 0;
}
