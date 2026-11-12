/* Real automatic camera measurement with a deterministic capture transport. */
#include "../src/factory.c"
#include <assert.h>
struct video_capture {
  int pipefd[2], frame;
  uint32_t pixels[64 * 64];
};
static int blank_last;
struct video_capture *video_capture_create(void) {
  struct video_capture *v = calloc(1, sizeof(*v));
  assert(v && !pipe(v->pipefd));
  assert(write(v->pipefd[1], "x", 1) == 1);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++)
      v->pixels[y * 64 + x] = (x / 8 + y / 8) % 2 ? 0xffffffff : 0xff000000;
  return v;
}
int video_capture_start(struct video_capture *v, const char *name) {
  (void)v;
  (void)name;
  return 0;
}
int video_capture_fd(const struct video_capture *v) { return v->pipefd[0]; }
int video_capture_read_frame(struct video_capture *v) {
  if (++v->frame == 8 && blank_last)
    memset(v->pixels, 0, sizeof(v->pixels));
  return 1;
}
const uint32_t *video_capture_pixels(const struct video_capture *v, int *w,
                                     int *h, int *stride) {
  *w = 64;
  *h = 64;
  *stride = 256;
  return v->pixels;
}
const char *video_capture_error(const struct video_capture *v) {
  (void)v;
  return "";
}
void video_capture_destroy(struct video_capture *v) {
  close(v->pipefd[0]);
  close(v->pipefd[1]);
  free(v);
}
int main(void) {
  struct factory f;
  factory_init(&f);
  char dir[] = "/tmp/cm5-camera-test-XXXXXX";
  assert(mkdtemp(dir));
  snprintf(f.artifact_dir, sizeof(f.artifact_dir), "%s", dir);
  char path[640];
  snprintf(path, sizeof(path), "%s/csi1.png", dir);
  for (blank_last = 0; blank_last <= 1; blank_last++) {
    assert(camera_auto(&f, T_CSI1) == 0);
    cairo_surface_t *s = cairo_image_surface_create_from_png(path);
    assert(cairo_surface_status(s) == CAIRO_STATUS_SUCCESS);
    cairo_surface_flush(s);
    struct signal_stats stats;
    assert(!check_rgb_frame((const uint32_t *)cairo_image_surface_get_data(s),
                            cairo_image_surface_get_width(s),
                            cairo_image_surface_get_height(s),
                            cairo_image_surface_get_stride(s), &stats));
    cairo_surface_destroy(s);
    assert(!unlink(path));
  }
  stopping = 1;
  assert(camera_auto(&f, T_CSI1) < 0);
  assert(access(path, F_OK));
  stopping = 0;
  rmdir(dir);
  puts("camera: validated PNG retained when final frame is blank; cancellation "
       "cannot create PASS evidence");
}
