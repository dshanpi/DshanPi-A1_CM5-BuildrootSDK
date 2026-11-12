#define main cm5_application_main
#include "../src/main.c"
#undef main
#include <assert.h>
static void bounds(struct app *a) {
  int selected = 0;
  for (int i = 0; i < a->hit_count; i++) {
    struct hit_target *h = &a->hits[i];
    assert(h->x >= 0 && h->y >= 0 && h->x + h->width <= 1024 &&
           h->y + h->height <= 768);
    if (h->action >= A_SELECT && h->action < A_SELECT + TEST_COUNT)
      selected++;
    for (int j = 0; j < i; j++) {
      struct hit_target *q = &a->hits[j];
      assert(h->x >= q->x + q->width || q->x >= h->x + h->width ||
             h->y >= q->y + q->height || q->y >= h->y + h->height);
    }
  }
  assert(selected == ACTIVE_TEST_COUNT);
  for (int i = 0; i < a->hit_count; i++) {
    struct hit_target *h = &a->hits[i];
    if (h->action >= A_SELECT && h->action < A_SELECT + TEST_COUNT)
      assert(h->width > 180 && h->height == 144 && h->y + h->height <= 584);
    if (a->selected >= T_CSI1 && a->selected <= T_CSI3 && h->y >= 608)
      assert(h->x + h->width <= 728);
  }
}
int main(void) {
  struct app a = {0};
  factory_init(&a.factory);
  a.video = video_capture_create();
  a.width = 1024;
  a.height = 768;
  a.camera_active = -1;
  char root[] = "/tmp/cm5-ui-test-XXXXXX";
  assert(mkdtemp(root));
  snprintf(a.factory.config.root, sizeof(a.factory.config.root), "%s", root);
  snprintf(a.report_path, sizeof(a.report_path), "%s/report.json", root);
  cairo_surface_t *s =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1024, 768);
  cairo_t *cr = cairo_create(s);
  a.factory.monitoring = 1;
  a.factory.auto_running = 1;
  for (int i = 0; i < ACTIVE_TEST_COUNT; i++) {
    int id = factory_active_tests[i];
    draw_ui(&a, cr);
    bounds(&a);
    activate_at(&a, 24 + (i % 5) * 197.6 + 40, 128 + (i / 5) * 156 + 30);
    assert(a.selected == id && a.operator_selected);
  }
  perform_action(&a, A_SELECT + T_HEADPHONE);
  assert(a.selected == T_CSI3);
  perform_action(&a, A_SELECT + T_RTC);
  assert(a.selected == T_CSI3);
  perform_action(&a, A_SELECT + T_M2_1);
  assert(a.selected == T_CSI3);
  keyboard_key(&a, NULL, 0, 0, KEY_UP, WL_KEYBOARD_KEY_STATE_PRESSED);
  assert(a.selected == factory_active_tests[ACTIVE_TEST_COUNT - 6]);
  keyboard_key(&a, NULL, 0, 0, KEY_DOWN, WL_KEYBOARD_KEY_STATE_PRESSED);
  assert(a.selected == T_CSI3);
  /* Ordinary pointer input never passes touch. One genuine touch does,
     including while capture workers are still in the initial queue. */
  draw_ui(&a, cr);
  activate_at(&a, 500, 710);
  assert(a.factory.tests[T_TOUCH].state != PASS);
  a.touch_event = 1;
  activate_at(&a, 500, 710);
  a.touch_event = 0;
  assert(a.factory.auto_running && a.factory.tests[T_TOUCH].state == PASS &&
         a.factory.tests[T_TOUCH].checked);
  perform_action(&a, A_SELECT + T_CSI1);
  assert(a.selected == T_CSI1);
  a.factory.auto_running = 0;
  /* Pause and real-touch gating; pending insertion verdicts wait for resume. */
  perform_action(&a, A_AUTO);
  assert(!a.factory.monitoring);
  a.factory.tests[T_TOUCH].state = UNKNOWN;
  a.touch_event = 1;
  activate_at(&a, 500, 710);
  a.touch_event = 0;
  assert(a.factory.tests[T_TOUCH].state != PASS);
  a.factory.monitoring = 1;
  perform_action(&a, A_RESET);
  perform_action(&a, A_RESET);
  assert(a.factory.tests[T_TOUCH].state == UNKNOWN);
  a.factory.monitoring = 1;
  for (int i = 0; i < ACTIVE_TEST_COUNT; i++) {
    a.factory.tests[factory_active_tests[i]].state = UNKNOWN;
    a.factory.tests[factory_active_tests[i]].auto_waiting = 1;
  }
  a.factory.tests[T_SPEAKER].state = READY;
  a.factory.tests[T_SPEAKER].checked = 1;
  a.factory.tests[T_SPEAKER].detected = 1;
  strcpy(a.factory.tests[T_SPEAKER].evidence,
         "示例：左右声道播放完成，请听音确认。");
  a.factory.tests[T_SPEAKER].auto_waiting = 0;
  a.factory.tests[T_BOARD_MIC].state = RUNNING;
  a.factory.tests[T_BOARD_MIC].auto_waiting = 0;
  a.factory.tests[T_ETH0].state = PASS;
  a.factory.tests[T_ETH0].detected = 1;
  a.factory.tests[T_USB1].state = PASS;
  a.factory.tests[T_USB1].detected = 1;
  strcpy(a.factory.tests[T_USB1].evidence,
         "1-1.1：存储设备；14cd:1212；USB存储器；480 Mb/s");
  a.factory.tests[T_USB2].state = PASS;
  a.factory.tests[T_USB2].detected = 0;
  strcpy(a.factory.tests[T_USB2].evidence, "1-1.2：HID键鼠设备；067b:2731");
  for (int i = 0; i < TEST_COUNT; i++)
    if (a.factory.tests[i].state == PASS)
      a.factory.tests[i].auto_waiting = 0;
  a.selected = T_BOARD_MIC;
  a.factory.auto_running = 1;
  strcpy(a.factory.tests[T_BOARD_MIC].evidence,
         "正在录音5秒，无需说话。");
  strcpy(a.factory.message,
         "接口持续检测中，可随时插入网线、HDMI、USB。通过记录保留至重新测试。");
  draw_ui(&a, cr);
  bounds(&a);
  cairo_surface_flush(s);
  unsigned char *pixels = cairo_image_surface_get_data(s);
  int stride = cairo_image_surface_get_stride(s);
  uint32_t pixel =
      *(uint32_t *)(pixels + 128 * stride + (int)(24 + 2 * 197.6 + 90) * 4);
  assert(((pixel >> 8) & 255) > ((pixel >> 16) & 255) + 50);
  assert(cairo_surface_write_to_png(s, "build/preview/automatic.png") ==
         CAIRO_STATUS_SUCCESS);
  a.selected = T_SPEAKER;
  draw_ui(&a, cr);
  bounds(&a);
  assert(cairo_surface_write_to_png(s, "build/preview/states.png") ==
         CAIRO_STATUS_SUCCESS);
  /* Red-box detail title/instruction/evidence must no longer be rendered. */
  cairo_surface_flush(s);
  size_t bytes = (size_t)cairo_image_surface_get_stride(s) * 768;
  unsigned char *before = malloc(bytes);
  assert(before);
  memcpy(before, cairo_image_surface_get_data(s), bytes);
  a.factory.tests[T_SPEAKER].instruction = "HIDDEN_INSTRUCTION_MARKER";
  strcpy(a.factory.tests[T_SPEAKER].evidence, "HIDDEN_RAW_LOG_MARKER");
  draw_ui(&a, cr);
  cairo_surface_flush(s);
  assert(!memcmp(before, cairo_image_surface_get_data(s), bytes));
  free(before);
  a.selected = T_CSI2;
  draw_ui(&a, cr);
  bounds(&a);
  assert(cairo_surface_write_to_png(s, "build/preview/camera.png") ==
         CAIRO_STATUS_SUCCESS);
  a.selected = T_USB1;
  draw_ui(&a, cr);
  bounds(&a);
  perform_action(&a, A_RUN);
  perform_action(&a, A_FAIL);
  perform_action(&a, A_SKIP);
  assert(a.factory.tests[T_USB1].state == PASS);
  assert(cairo_surface_write_to_png(s, "build/preview/latched.png") ==
         CAIRO_STATUS_SUCCESS);
  a.factory.auto_running = 0;
  cairo_destroy(cr);
  cairo_surface_destroy(s);
  cleanup(&a);
  unlink(a.report_path);
  rmdir(root);
  puts("UI: 15 visible targets, larger 5x3 tiles, RTC disabled, no overlap, selection during automation, real "
       "touch vs pointer, pause, reset, green pass and removal status passed");
}
