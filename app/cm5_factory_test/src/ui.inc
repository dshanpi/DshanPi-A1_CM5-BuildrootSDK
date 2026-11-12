/* One-screen factory overview, 1024 x 768 logical coordinates. */
enum { TEST_GRID_COLUMNS = 5, TEST_TILE_HEIGHT = 144, TEST_GRID_GAP = 12 };
static const double TEST_TILE_WIDTH = (976.0 - 4 * TEST_GRID_GAP) / TEST_GRID_COLUMNS;
static struct color state_color(enum test_state s) {
  return s == PASS      ? COLOR_GREEN
         : s == FAIL    ? COLOR_RED
         : s == RUNNING ? COLOR_BLUE
                        : COLOR_MUTED;
}
static void button(struct app *a, cairo_t *cr, const char *text, double x,
                   double y, double w, int action, int primary) {
  draw_button(a, cr, text, x, y, w, 44, primary ? COLOR_BLUE : COLOR_BORDER,
              primary ? COLOR_WHITE : COLOR_TEXT, (enum action)action,
              !primary);
}
static void text(cairo_t *cr, const char *s, double x, double y, double w,
                 double size, int bold, struct color c) {
  draw_text(cr, s, x, y, w, size, bold, c, PANGO_ALIGN_LEFT);
}
static void paragraph(cairo_t *cr, const char *s, double x, double y, double w,
                      double h, double size, struct color c) {
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  PangoLayout *l = pango_cairo_create_layout(cr);
  PangoFontDescription *d =
      pango_font_description_from_string("Source Han Sans CN");
  pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
  pango_layout_set_font_description(l, d);
  pango_layout_set_text(l, s, -1);
  pango_layout_set_width(l, (int)(w * PANGO_SCALE));
  pango_layout_set_height(l, (int)(h * PANGO_SCALE));
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  set_source(cr, c);
  cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, l);
  pango_font_description_free(d);
  g_object_unref(l);
  cairo_restore(cr);
}
static void camera_preview(struct app *a, cairo_t *cr) {
  int w = 0, h = 0, stride = 0;
  const uint32_t *pixels = video_capture_pixels(a->video, &w, &h, &stride);
  fill_round(cr, 754, 604, 246, 142, 6, COLOR_NAVY);
  cairo_surface_t *surface = NULL;
  if (pixels && a->camera_frames)
    surface = cairo_image_surface_create_for_data(
        (unsigned char *)pixels, CAIRO_FORMAT_RGB24, w, h, stride);
  else if (a->factory.tests[a->selected].automatic &&
           a->factory.tests[a->selected].checked) {
    char path[640];
    snprintf(path, sizeof(path), "%s/%s.png", a->factory.artifact_dir,
             a->factory.tests[a->selected].key);
    surface = cairo_image_surface_create_from_png(path);
    w = cairo_image_surface_get_width(surface);
    h = cairo_image_surface_get_height(surface);
  }
  if (surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS &&
      w > 0 && h > 0) {
    double scale = fmin(246.0 / w, 142.0 / h);
    cairo_save(cr);
    cairo_translate(cr, 754 + (246 - w * scale) / 2,
                    604 + (142 - h * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    draw_centered_text(cr,
                       a->factory.tests[a->selected].state == RUNNING
                           ? "自动抓图验证中"
                           : "尚未取得抓图",
                       754, 604, 246, 142, 17, 0, COLOR_WHITE);
  }
  if (surface)
    cairo_surface_destroy(surface);
}
static void draw_ui(struct app *a, cairo_t *cr) {
  cairo_save(cr);
  cairo_scale(cr, a->width / 1024.0, a->height / 768.0);
  a->hit_count = 0;
  set_source(cr, COLOR_BG);
  cairo_paint(cr);
  fill_round(cr, 0, 0, 1024, 76, 0, COLOR_NAVY);
  text(cr, "CM5 底板产测", 24, 21, 480, 28, 1, COLOR_WHITE);
  button(a, cr, a->factory.monitoring ? "暂停检测" : "继续检测", 460, 16, 148,
         A_AUTO, 1);
  button(a, cr, "保存报告", 620, 16, 136, A_SAVE, 0);
  button(a, cr, "重新测试", 768, 16, 136, A_RESET, 0);
  button(a, cr, "退出", 916, 16, 84, A_EXIT, 0);
  int pass = 0, fail = 0, skip = 0;
  for (int i = 0; i < TEST_COUNT; i++) {
    if (!factory_test_enabled(i))
      continue;
    pass += a->factory.tests[i].state == PASS;
    fail += a->factory.tests[i].state == FAIL;
    skip += a->factory.tests[i].state == SKIP;
  }
  char summary[192];
  snprintf(summary, sizeof(summary),
           "通过 %d / %d     失败 %d     跳过 %d     待完成 %d", pass,
           ACTIVE_TEST_COUNT, fail, skip,
           ACTIVE_TEST_COUNT - pass - fail - skip);
  text(cr, summary, 24, 92, 730, 18, 1,
       fail                        ? COLOR_RED
       : pass == ACTIVE_TEST_COUNT ? COLOR_GREEN
                                   : COLOR_TEXT);
  text(cr, a->factory.monitoring ? "持续检测 · 随时接入" : "检测已暂停", 790,
       94, 210, 15, 0, COLOR_MUTED);
  for (int index = 0; index < ACTIVE_TEST_COUNT; index++) {
    int i = factory_active_tests[index];
    struct test_item *t = &a->factory.tests[i];
    double x = 24 + (index % TEST_GRID_COLUMNS) * (TEST_TILE_WIDTH + TEST_GRID_GAP);
    double y = 128 + (index / TEST_GRID_COLUMNS) * (TEST_TILE_HEIGHT + TEST_GRID_GAP);
    struct color border = state_color(t->state), fill = COLOR_WHITE;
    if (t->state == PASS)
      fill = (struct color){.91, .97, .93, 1};
    if (t->state == FAIL)
      fill = (struct color){1, .94, .94, 1};
    if (t->state == RUNNING)
      fill = (struct color){.91, .95, 1, 1};
    fill_round(cr, x, y, TEST_TILE_WIDTH, TEST_TILE_HEIGHT, 6, fill);
    stroke_round(cr, x, y, TEST_TILE_WIDTH, TEST_TILE_HEIGHT, 6, border,
                 t->state == PASS || t->state == FAIL ? 3 : 1.5);
    if (i == a->selected)
      stroke_round(cr, x - 3, y - 3, TEST_TILE_WIDTH + 6, TEST_TILE_HEIGHT + 6, 8, COLOR_BLUE, 1.5);
    text(cr, t->name, x + 14, y + 18, TEST_TILE_WIDTH - 28, 20, 1, COLOR_TEXT);
    text(cr,
         t->state == PASS && !t->detected && t->require_detection
             ? "通过 · 已拔出"
         : t->auto_waiting ? "等待接入"
                           : factory_state_name(t->state),
         x + 14, y + 62, TEST_TILE_WIDTH - 28, 18, 1,
         t->state == RUNNING ? (struct color){.10, .29, .65, 1} : border);
    if (t->state == PASS && (i == T_USB1 || i == T_USB2 || i == T_TYPEC)) {
      const char *kind = strstr(t->evidence, "：");
      text(cr, kind ? kind + 3 : "USB枚举通过", x + 14, y + 108, TEST_TILE_WIDTH - 28, 13, 0,
           COLOR_GREEN);
    }
    add_hit(a, x, y, TEST_TILE_WIDTH, TEST_TILE_HEIGHT, (enum action)(A_SELECT + i));
  }
  struct test_item *t = &a->factory.tests[a->selected];
  int camera = a->selected >= T_CSI1 && a->selected <= T_CSI3;
  /* Detailed instructions and raw evidence stay in the saved report only. */
  if (camera)
    camera_preview(a, cr);
  const char *run_label = a->selected == T_BOARD_MIC ? "重新录音"
                          : camera                   ? "重新抓图"
                          : a->selected == T_TOUCH   ? "触摸屏幕即可"
                                                     : "复测此项";
  if (t->state == PASS) {
    fill_round(cr, 24, 608, camera ? 704 : 976, 44, 6, COLOR_BORDER);
    draw_centered_text(cr,
                       "本轮已通过；重测请点顶部“重新测试”",
                       24, 608, camera ? 704 : 976, 44, 18, 1, COLOR_MUTED);
    paragraph(cr, a->factory.message, 24, 680, camera ? 704 : 976, 64, 14, COLOR_TEXT);
    cairo_restore(cr);
    return;
  }
  button(a, cr, run_label, 24, 608, 184, A_RUN, 1);
  if (factory_can_pass(&a->factory, a->selected))
    draw_button(a, cr, a->selected == T_SPEAKER ? "声音正常" : "按键正常", 220,
                608, 184, 44, COLOR_GREEN, COLOR_WHITE, A_PASS, 0);
  else {
    fill_round(cr, 220, 608, 184, 44, 6, COLOR_BORDER);
    draw_centered_text(cr, a->selected == T_SPEAKER ? "播放后确认" : "自动判定",
                       220, 608, 184, 44, 18, 1, COLOR_MUTED);
  }
  button(a, cr, "标记失败", 416, 608, 184, A_FAIL, 0);
  button(a, cr, "跳过此项", camera ? 24 : 612, camera ? 660 : 608, 184, A_SKIP, 0);
  button(a, cr, "刷新检测", camera ? 220 : 808, camera ? 660 : 608, 192, A_SCAN, 0);
  paragraph(cr, a->factory.message, 24, camera ? 716 : 680, camera ? 704 : 976,
            camera ? 44 : 64, 14, COLOR_TEXT);
  cairo_restore(cr);
}
static void stop_camera(struct app *a) {
  if (a->camera_active >= 0) {
    struct test_item *t = &a->factory.tests[a->camera_active];
    if (t->state == RUNNING) {
      t->state = UNKNOWN;
      t->checked = 0;
      snprintf(t->evidence, sizeof(t->evidence),
               "预览已停止，尚未取得有效帧；可重新运行或标记失败/跳过。");
    }
  }
  video_capture_stop(a->video);
  a->camera_active = -1;
  a->camera_frames = 0;
}
static void save_report(struct app *a) {
  if (factory_save(&a->factory, a->report_path))
    snprintf(a->factory.message, sizeof(a->factory.message),
             "报告保存失败：%.360s (%.100s)", a->report_path, strerror(errno));
  else
    snprintf(a->factory.message, sizeof(a->factory.message),
             "报告已保存：%.420s", a->report_path);
}
static void persist_report(struct app *a) {
  if (factory_save(&a->factory, a->report_path))
    snprintf(a->factory.message, sizeof(a->factory.message), "自动保存失败：%s",
             strerror(errno));
}
static void perform_action(struct app *a, int action) {
  if (action >= A_SELECT && action < A_SELECT + TEST_COUNT) {
    int id = action - A_SELECT;
    if (!factory_test_enabled(id))
      return;
    a->operator_selected = 1;
    if (a->camera_active >= 0 && id != a->camera_active)
      stop_camera(a);
    a->selected = id;
    a->reset_armed = 0;
    a->redraw = 1;
    return;
  }
  int id = a->selected;
  struct test_item *t = &a->factory.tests[id];
  switch (action) {
  case A_EXIT:
    factory_auto_cancel(&a->factory);
    a->running = 0;
    break;
  case A_SAVE:
    save_report(a);
    break;
  case A_SCAN:
    factory_scan(&a->factory);
    break;
  case A_AUTO:
    if (a->factory.monitoring)
      factory_auto_cancel(&a->factory);
    else {
      stop_camera(a);
      if (factory_auto_begin(&a->factory, a->report_path))
        snprintf(a->factory.message, sizeof(a->factory.message),
                 "自动测试未启动：报告目录不可写或任务忙，请先检查配置。");
    }
    persist_report(a);
    break;
  case A_RUN:
    if (t->state == PASS) {
      snprintf(a->factory.message, sizeof(a->factory.message),
               "本轮已通过，结果保留；需要重测请点击顶部“重新测试”。");
      break;
    }
    if (id == T_TOUCH) {
      snprintf(a->factory.message, sizeof(a->factory.message),
               "触摸屏幕任意位置即可完成显示与触摸测试。");
      break;
    }
    if (id == T_ETH0 || id == T_ETH1 || id == T_USB1 || id == T_USB2 ||
        id == T_TYPEC || id == T_HDMI) {
      factory_scan(&a->factory);
      break;
    }
    if (factory_retry(&a->factory, id))
      snprintf(a->factory.message, sizeof(a->factory.message),
               a->factory.job_pid
                   ? "正在执行采样任务，完成后可复测；接口接入检测持续进行。"
                   : "尚未检测到设备或配置，请接入设备后复测。");
    break;
  case A_PASS:
    if (a->camera_active == id)
      stop_camera(a);
    if (factory_decide(&a->factory, id, PASS) == 0)
      save_report(a);
    break;
  case A_FAIL:
    if (a->camera_active == id) {
      stop_camera(a);
      t->state = READY;
    }
    if (factory_decide(&a->factory, id, FAIL) == 0)
      save_report(a);
    break;
  case A_SKIP:
    if (a->camera_active == id) {
      stop_camera(a);
      t->state = READY;
    }
    if (factory_decide(&a->factory, id, SKIP) == 0)
      save_report(a);
    break;
  case A_RESET:
    if (!a->reset_armed) {
      a->reset_armed = 1;
      snprintf(a->factory.message, sizeof(a->factory.message),
               "再次点击“重新测试”清除本轮结果，并立即开始新一轮测试。");
      break;
    }
    factory_auto_cancel(&a->factory);
    stop_camera(a);
    for (int i = 0; i < TEST_COUNT; i++) {
      a->factory.tests[i].state = UNKNOWN;
      a->factory.tests[i].checked = 0;
      a->factory.tests[i].automatic = 0;
      a->factory.tests[i].auto_waiting = 0;
    }
    a->factory.session_id = (unsigned long)time(NULL);
    a->reset_armed = 0;
    factory_scan(&a->factory);
    a->selected = T_SPEAKER;
    a->operator_selected = 0;
    a->factory.artifact_dir[0] = 0;
    if (!*a->factory.config.root)
      factory_auto_begin(&a->factory, a->report_path);
    break;
  }
  if (action != A_RESET)
    a->reset_armed = 0;
  a->redraw = 1;
}
static void activate_at(struct app *a, double physical_x, double physical_y) {
  double x = physical_x * 1024 / a->width, y = physical_y * 768 / a->height;
  /* A real touch anywhere certifies the visible UI + touch path, even while
     capture workers run. Pointer/keyboard events never certify touch. */
  if (a->touch_event && a->factory.monitoring &&
      a->factory.tests[T_TOUCH].state != PASS) {
    struct test_item *t = &a->factory.tests[T_TOUCH];
    t->checked = t->detected = t->automatic = 1;
    t->auto_waiting = 0;
    t->state = PASS;
    snprintf(t->evidence, sizeof(t->evidence),
             "界面正常显示，收到真实Wayland触摸输入（%.0f, %.0f）。", x, y);
    a->factory.revision++;
    persist_report(a);
    a->redraw = 1;
  }
  for (int i = 0; i < a->hit_count; i++) {
    struct hit_target *h = &a->hits[i];
    if (x >= h->x && x < h->x + h->width && y >= h->y && y < h->y + h->height) {
      perform_action(a, h->action);
      return;
    }
  }
}
