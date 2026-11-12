#include <cairo/cairo.h>
/* Automatic measurements. Every green verdict names its limited scope. */
static int auto_kind(int id) {
  if (id == T_BOARD_MIC || id == T_TF || id == T_M2_0 || id == T_M2_1 ||
      (id >= T_CSI1 && id <= T_CSI3))
    return 1;
  return id == T_SPEAKER ? 2 : 0;
}
const char *factory_auto_scope(int id) {
  if (id == T_BOARD_MIC)
    return "pcm_capture_5s_complete";
  if (id >= T_CSI1 && id <= T_CSI3)
    return "five_frames_spatial_signal";
  if (id == T_ETH0 || id == T_ETH1)
    return "physical_link_carrier";
  if (id == T_USB1 || id == T_USB2 || id == T_TYPEC)
    return "usb_enumeration_device_type";
  if (id == T_HDMI)
    return "drm_connected";
  if (id == T_TOUCH)
    return "displayed_ui_and_one_real_touch";
  if (id == T_M2_0 || id == T_M2_1)
    return "pcie_slot_nvme_driver_read_1MiB";
  if (id == T_TF)
    return "storage_read_1MiB";
  return "manual_observation";
}
static int camera_auto(struct factory *f, int id) {
  struct video_capture *v = video_capture_create();
  if (!v)
    return -1;
  int rc = -1, frames = 0, good = 0;
  unsigned char *snapshot = NULL;
  int snapshot_w = 0, snapshot_h = 0, snapshot_stride = 0;
  double until = seconds() + 8;
  if (video_capture_start(v, factory_camera(f, id)))
    goto out;
  while (!stopping && seconds() < until && frames < 8) {
    struct pollfd p = {.fd = video_capture_fd(v), .events = POLLIN};
    int r = poll(&p, 1, 200);
    if (r < 0 && errno != EINTR)
      goto out;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
      goto out;
    if (!(p.revents & POLLIN))
      continue;
    int frame = video_capture_read_frame(v);
    if (frame < 0)
      goto out;
    if (!frame)
      continue;
    frames++;
    if (frames > 3) {
      int w, h, stride;
      const uint32_t *pixels = video_capture_pixels(v, &w, &h, &stride);
      struct signal_stats stats;
      if (!check_rgb_frame(pixels, w, h, stride, &stats)) {
        size_t bytes = (size_t)stride * h;
        unsigned char *next = realloc(snapshot, bytes);
        if (!next)
          goto out;
        snapshot = next;
        memcpy(snapshot, pixels, bytes);
        snapshot_w = w;
        snapshot_h = h;
        snapshot_stride = stride;
        good++;
      }
    }
  }
  if (stopping || frames < 8 || good < 4) {
    printf("抓图失败：收到%d帧，有效图像%d/5；检查遮挡、光照和ISP链路。\n",
           frames, good);
    goto out;
  }
  int w = snapshot_w, h = snapshot_h, stride = snapshot_stride;
  const uint32_t *pixels = (const uint32_t *)snapshot;
  char path[640];
  snprintf(path, sizeof(path), "%s/%s.png", f->artifact_dir, f->tests[id].key);
  cairo_surface_t *surface = cairo_image_surface_create_for_data(
      (unsigned char *)pixels, CAIRO_FORMAT_RGB24, w, h, stride);
  cairo_status_t status = cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  if (status != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "保存抓图失败：%s\n", cairo_status_to_string(status));
    goto out;
  }
  printf("抓图通过：丢弃3帧预热、检查5帧（有效%d），预览图像：%"
         "s；未验证色彩或标定。\n",
         good, path);
  rc = 0;
out:
  if (rc)
    fprintf(stderr, "摄像头检查未通过：%s\n", video_capture_error(v));
  free(snapshot);
  video_capture_destroy(v);
  return rc;
}
static int auto_worker(struct factory *f, int id) {
  return id >= T_CSI1 && id <= T_CSI3 ? camera_auto(f, id) : -1;
}
static const char *auto_missing(struct factory *f, int id) {
  if (id == T_M2_1 && !f->config.pcie1_mode)
    return "当前USB Hub启动配置与PCIe1互斥，需另一个启动配置";
  if (id >= T_CSI1 && id <= T_CSI3 && !*factory_camera(f, id))
    return "配置对应CSI的ISP视频节点";
  if (id == T_UART && !*f->config.uart)
    return "配置非控制台串口并连接TX/RX回环";
  if (id == T_LED && !*f->config.led)
    return "配置专属LED类节点";
  if (id == T_FAN && !*f->config.fan)
    return "配置并核对风扇hwmon PWM节点";
  return NULL;
}
