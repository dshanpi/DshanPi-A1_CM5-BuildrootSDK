/* USB socket verdicts use enumeration and the descriptor class, never block
 * I/O. */
static int usb_hex(const char *s) {
  char *end;
  if (!*s)
    return -1;
  long v = strtol(s, &end, 16);
  return *end || v < 0 || v > 65535 ? -1 : (int)v;
}
static const char *usb_type(int c) {
  switch (c) {
  case 1:
    return "音频设备";
  case 2:
    return "通信设备";
  case 3:
    return "HID键鼠设备";
  case 5:
    return "物理设备";
  case 6:
    return "成像设备";
  case 7:
    return "打印机";
  case 8:
    return "存储设备";
  case 9:
    return "USB集线器";
  case 10:
    return "网络数据设备";
  case 11:
    return "智能卡";
  case 13:
    return "安全设备";
  case 14:
    return "视频设备";
  case 15:
    return "个人医疗设备";
  case 16:
    return "音视频设备";
  case 17:
    return "广告牌设备";
  case 18:
    return "Type-C桥接设备";
  case 0xdc:
    return "诊断设备";
  case 0xe0:
    return "无线设备";
  case 0xef:
    return "复合设备";
  case 0xfe:
    return "专用设备";
  case 0xff:
    return "厂商专用设备";
  default:
    return NULL;
  }
}
static int usb_port_valid(const char *s) {
  if (!*s || !strchr(s, '-'))
    return 0;
  for (const char *p = s; *p; p++)
    if ((*p < '0' || *p > '9') && *p != '-' && *p != '.')
      return 0;
  return 1;
}
static void scan_usb(struct factory *f) {
  glob_t g;
  char hub[128] = "", path[2048], vid[64], pid[64], cls[64];
  int hubs = 0;
  if (match_paths(f, "/sys/bus/usb/devices/*/idVendor", &g)) {
    for (size_t i = 0; i < g.gl_pathc; i++) {
      char base[1024];
      copy(base, sizeof(base), g.gl_pathv[i]);
      *strrchr(base, '/') = 0;
      read_text(g.gl_pathv[i], vid, sizeof(vid));
      snprintf(path, sizeof(path), "%s/idProduct", base);
      read_text(path, pid, sizeof(pid));
      if (usb_hex(vid) == 0x1a86 && usb_hex(pid) == 0x8091) {
        copy(hub, sizeof(hub), strrchr(base, '/') + 1);
        hubs++;
      }
    }
  }
  globfree(&g);
  char ports[3][128];
  copy(ports[0], sizeof(ports[0]), f->config.usb1);
  copy(ports[1], sizeof(ports[1]), f->config.usb2);
  copy(ports[2], sizeof(ports[2]), f->config.typec);
  if (hubs == 1)
    for (int i = 0; i < 2; i++)
      if (!*ports[i] || !strcmp(ports[i], "auto"))
        snprintf(ports[i], sizeof(ports[i]), "%.120s.%c", hub, i ? '2' : '1');
  const int ids[] = {T_USB1, T_USB2, T_TYPEC};
  int found[3] = {0, 0, 0};
  char evidence[3][1024];
  for (int i = 0; i < 3; i++)
    snprintf(evidence[i], sizeof(evidence[i]),
             "%s%s：等待USB设备接入并识别类型", i < 2 ? "端口 " : "Type-C USB",
             i < 2 ? ports[i] : "");
  if (match_paths(f, "/sys/bus/usb/devices/*/idVendor", &g)) {
    for (size_t i = 0; i < g.gl_pathc; i++) {
      char base[1024], product[256], speed[64], types[256] = "";
      copy(base, sizeof(base), g.gl_pathv[i]);
      *strrchr(base, '/') = 0;
      const char *name = strrchr(base, '/') + 1;
      if (!strncmp(name, "usb", 3))
        continue;
      read_text(g.gl_pathv[i], vid, sizeof(vid));
      snprintf(path, sizeof(path), "%s/idProduct", base);
      read_text(path, pid, sizeof(pid));
      if (usb_hex(vid) < 0 || usb_hex(pid) < 0 ||
          (usb_hex(vid) == 0x1a86 && usb_hex(pid) == 0x8091))
        continue;
      snprintf(path, sizeof(path), "%s/bDeviceClass", base);
      read_text(path, cls, sizeof(cls));
      const char *type = usb_type(usb_hex(cls));
      if (type && usb_hex(cls) != 0xef)
        copy(types, sizeof(types), type);
      else {
        glob_t interfaces;
        snprintf(path, sizeof(path),
                 "%s/sys/bus/usb/devices/%s:*/bInterfaceClass", f->config.root,
                 name);
        if (!glob(path, 0, NULL, &interfaces))
          for (size_t j = 0; j < interfaces.gl_pathc; j++) {
            char c[64];
            read_text(interfaces.gl_pathv[j], c, sizeof(c));
            type = usb_type(usb_hex(c));
            if (type && !strstr(types, type))
              append(types, sizeof(types), "%s%s", *types ? " / " : "", type);
          }
        globfree(&interfaces);
        if (!*types && usb_hex(cls) == 0xef)
          copy(types, sizeof(types), "复合设备");
      }
      if (!*types)
        continue; /* Descriptor enumeration still incomplete. */
      snprintf(path, sizeof(path), "%s/product", base);
      read_text(path, product, sizeof(product));
      snprintf(path, sizeof(path), "%s/speed", base);
      read_text(path, speed, sizeof(speed));
      append(f->inventory, sizeof(f->inventory),
             "USB %s: %s %s:%s %s [%s Mb/s]\n", name, types, vid, pid, product,
             speed);
      for (int n = 0; n < 3; n++) {
        int matched = usb_port_valid(ports[n]) && !strcmp(name, ports[n]);
        if (n == 2 && (!*ports[n] || !strcmp(ports[n], "auto"))) {
          char resolved[4096];
          matched =
              realpath(base, resolved) && strstr(resolved, "23000000") != NULL;
        }
        if (!matched || (n < 2 && f->config.pcie1_mode))
          continue;
        found[n] = 1;
        snprintf(evidence[n], sizeof(evidence[n]),
                 "%s：%s；%s:%s；%.240s；%s Mb/s（枚举通过）", name, types, vid,
                 pid, product, speed);
      }
    }
  }
  globfree(&g);
  if (!found[2]) {
    /* configured proves enumeration, while the active config's bound function
       identifies the device type. A bare UDC state is insufficient. */
    if (match_paths(f, "/sys/kernel/config/usb_gadget/*/UDC", &g))
      for (size_t i = 0; i < g.gl_pathc; i++) {
        char udc[128], state[64], base[1024], types[256] = "";
        if (read_text(g.gl_pathv[i], udc, sizeof(udc)) || !*udc ||
            strchr(udc, '/'))
          continue;
        snprintf(path, sizeof(path), "/sys/class/udc/%s/state", udc);
        if (rooted_read(f, path, state, sizeof(state)) ||
            strcmp(state, "configured"))
          continue;
        copy(base, sizeof(base), g.gl_pathv[i]);
        *strrchr(base, '/') = 0;
        snprintf(path, sizeof(path), "%s/configs/*/*", base);
        glob_t functions;
        if (!glob(path, 0, NULL, &functions))
          for (size_t j = 0; j < functions.gl_pathc; j++) {
            char resolved[4096];
            if (!realpath(functions.gl_pathv[j], resolved))
              continue;
            const char *name = strstr(resolved, "/functions/");
            if (!name)
              continue;
            name += 11;
            const char *type =
                !strncmp(name, "mass_storage.", 13) ? "存储设备"
                : !strncmp(name, "hid.", 4)         ? "HID设备"
                : !strncmp(name, "uvc.", 4)         ? "视频设备"
                : !strncmp(name, "uac", 3)          ? "音频设备"
                : !strncmp(name, "rndis.", 6) || !strncmp(name, "ecm.", 4) ||
                        !strncmp(name, "ncm.", 4)
                    ? "USB网络设备"
                : !strncmp(name, "acm.", 4) || !strncmp(name, "gser.", 5)
                    ? "USB串口设备"
                : !strcmp(name, "ffs.adb") ? "ADB调试设备"
                : !strcmp(name, "ffs.mtp") || !strncmp(name, "mtp.", 4)
                    ? "MTP文件传输设备"
                    : NULL;
            if (type && !strstr(types, type))
              append(types, sizeof(types), "%s%s", *types ? " / " : "", type);
          }
        globfree(&functions);
        if (!*types) {
          copy(evidence[2], sizeof(evidence[2]),
               "Type-C已枚举，但尚未识别配置的USB功能类型");
          continue;
        }
        found[2] = 1;
        snprintf(evidence[2], sizeof(evidence[2]),
                 "Type-C设备模式：%s；主机已完成枚举（configured）", types);
        break;
      }
    globfree(&g);
  }
  for (int i = 0; i < 3; i++) {
    if (i < 2 && f->config.pcie1_mode)
      copy(evidence[i], sizeof(evidence[i]), "当前PCIe1启动配置未启用USB Hub");
    else if (i < 2 && !usb_port_valid(ports[i]))
      copy(evidence[i], sizeof(evidence[i]),
           "未找到唯一CH334F Hub；请配置对应usb_port映射");
    reset_item(&f->tests[ids[i]], found[i], evidence[i]);
  }
}
