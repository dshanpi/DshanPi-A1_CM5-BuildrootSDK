#define _GNU_SOURCE
#include "factory.h"
#include "checks.h"
#include "hardware.h"
#include "nvme_probe.h"
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <ifaddrs.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static double seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
static void terminate_job(int sig) {
  (void)sig;
  stopping = 1;
}
static void copy(char *out, size_t n, const char *s) {
  if (!n)
    return;
  if (!s)
    s = "";
  size_t k = strlen(s);
  if (k >= n)
    k = n - 1;
  memcpy(out, s, k);
  out[k] = 0;
}
static int read_text(const char *path, char *out, size_t n) {
  FILE *fp = fopen(path, "r");
  size_t k;
  if (!fp) {
    if (n)
      out[0] = 0;
    return -1;
  }
  k = fread(out, 1, n - 1, fp);
  out[k] = 0;
  fclose(fp);
  while (k && (out[k - 1] == '\n' || out[k - 1] == '\r'))
    out[--k] = 0;
  return 0;
}
static int write_text(const char *path, const char *value) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  size_t n = strlen(value);
  ssize_t r = write(fd, value, n);
  int saved = errno;
  close(fd);
  errno = saved;
  return r == (ssize_t)n ? 0 : -1;
}
static void append(char *out, size_t n, const char *fmt, ...) {
  size_t used = strlen(out);
  if (used >= n - 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(out + used, n - used, fmt, ap);
  va_end(ap);
}
static int rooted_read(struct factory *f, const char *path, char *out,
                       size_t n) {
  char *p = NULL;
  if (asprintf(&p, "%s%s", f->config.root, path) < 0)
    return -1;
  int rc = read_text(p, out, n);
  free(p);
  return rc;
}
const int factory_active_tests[ACTIVE_TEST_COUNT] = {
    T_SPEAKER, T_BOARD_MIC, T_ETH0, T_ETH1, T_USB1, T_USB2,
    T_TYPEC,   T_TF,        T_M2_0, T_HDMI, T_TOUCH,
    T_CSI1,    T_CSI2,      T_CSI3, T_WIFI};
int factory_test_enabled(int id) {
  for (int i = 0; i < ACTIVE_TEST_COUNT; i++)
    if (factory_active_tests[i] == id)
      return 1;
  return 0;
}
static void reset_item(struct test_item *t, int detected,
                       const char *evidence) {
  t->detected = detected;
  /* A round records successful insertion even after moving the fixture. */
  if (t->state == PASS || t->state == RUNNING || t->state == FAIL ||
      t->state == SKIP || t->checked)
    return;
  t->auto_waiting = !detected;
  copy(t->evidence, sizeof(t->evidence), evidence);
  t->state = detected ? READY : UNKNOWN;
}
static void live_result(struct factory *f, int id) {
  struct test_item *t = &f->tests[id];
  if (!f->monitoring || !t->detected || t->state == PASS || t->state == SKIP ||
      (t->state == FAIL && !t->automatic))
    return;
  t->state = PASS;
  t->checked = t->automatic = 1;
  t->auto_waiting = 0;
}
static int match_paths(struct factory *f, const char *pattern, glob_t *g) {
  char p[1024];
  snprintf(p, sizeof(p), "%s%s", f->config.root, pattern);
  memset(g, 0, sizeof(*g));
  return glob(p, 0, NULL, g) == 0;
}
static const struct test_item definitions[TEST_COUNT] = {
    [T_SPEAKER] = {.key = "speaker",
                   .name = "双扬声器",
                   .page = 0,
                   .instruction = "U42/U46：确认左右扬声器均有清晰声音。",
                   .require_detection = 1},
    [T_HEADPHONE] = {.key = "headphone",
                     .name = "耳机输出",
                     .page = 0,
                     .instruction = "CN4：插入耳机，确认左右声道；输出1。",
                     .require_detection = 1},
    [T_BOARD_MIC] =
        {.key = "board_mic",
         .name = "板载麦克风",
         .page = 0,
         .instruction =
             "MIC2：自动录音5秒，录制完成且文件完整即通过，无需说话。",
         .require_detection = 1},
    [T_HEADSET_MIC] =
        {.key = "headset_mic",
         .name = "耳机麦克风",
         .page = 0,
         .instruction =
             "CN4 / MIC1：录音5秒，检查录制完成与文件完整性。",
         .require_detection = 1},
    [T_ETH0] =
        {.key = "ethernet_0",
         .name = "网口 A",
         .page = 1,
         .instruction =
             "RJ1/RJ2 均需接线；检测到物理Link UP即通过，可随时插入网线。",
         .require_detection = 1},
    [T_ETH1] = {.key = "ethernet_1",
                .name = "网口 B",
                .page = 1,
                .instruction = "接入已上电的交换机或对端，Link UP即通过。",
                .require_detection = 1},
    [T_WIFI] = {.key = "onboard_wifi",
                .name = "板载 Wi-Fi",
                .page = 1,
                .instruction = "装好天线并连接无线网络。板载 AIC8800D80 无线链路就绪后通过。",
                .require_detection = 1},
    [T_USB1] = {.key = "usb_a1",
                .name = "USB-A 上口",
                .page = 2,
                .instruction =
                    "USB5上口：接入USB外设，枚举并识别设备类型即通过。",
                .require_detection = 1},
    [T_USB2] = {.key = "usb_a2",
                .name = "USB-A 下口",
                .page = 2,
                .instruction =
                    "USB5下口：支持存储、鼠标、键盘等设备，识别类型即通过。",
                .require_detection = 1},
    [T_TYPEC] = {.key = "typec",
                 .name = "Type-C USB",
                 .page = 2,
                 .instruction = "Type-C：识别USB外设或设备模式连接即通过。"},
    [T_TF] = {.key = "tf",
              .name = "TF 卡",
              .page = 3,
              .instruction =
                  "SIM1 / SDMMC0：运行只读抽样，再确认；不写裸设备。",
              .require_detection = 1},
    [T_M2_0] = {.key = "m2_0",
                .name = "M.2 / PCIe0",
                .page = 3,
                .instruction = "U95：自动识别PCIe0的NVMe，驱动就绪且只读1MiB成功即通过。",
                .require_detection = 1},
    [T_M2_1] = {.key = "m2_1",
                .name = "M.2 / PCIe1",
                .page = 3,
                .instruction =
                    "U16：与当前USB1 Hub配置互斥；切换启动配置后复测。",
                .require_detection = 1},
    [T_HDMI] = {.key = "hdmi_out",
                .name = "HDMI 输出",
                .page = 4,
                .instruction = "接入已上电的HDMI显示器，检测到连接即通过。",
                .require_detection = 1},
    [T_DSI] = {.key = "dsi",
               .name = "MIPI DSI 屏",
               .page = 4,
               .instruction = "CN5：1024×768，观察色条、背光及坏点。",
               .require_detection = 1},
    [T_TOUCH] = {.key = "display_touch",
                 .name = "触摸输入",
                 .page = 4,
                 .instruction =
                     "轻触小屏任意位置，检查触摸输入。小屏画面请目视确认。",
                 .require_detection = 1},
    [T_CSI1] = {.key = "csi1",
                .name = "CSI / FPC4",
                .page = 5,
                .instruction = "使用已配置的ISP输出节点，预览画面后确认。",
                .require_detection = 1},
    [T_CSI2] = {.key = "csi2",
                .name = "CSI / FPC7",
                .page = 5,
                .instruction = "每个接口分别指定节点；摄像头型号由设备树决定。",
                .require_detection = 1},
    [T_CSI3] = {.key = "csi3",
                .name = "CSI / FPC6",
                .page = 5,
                .instruction = "支持原工程NV12/YUYV/RGB预览；不支持RAW Bayer。",
                .require_detection = 1},
    [T_RTC] = {.key = "rtc",
               .name = "PCF8563 RTC",
               .page = 6,
               .instruction =
                   "U11 / I2C音频总线：读取并检查走时；掉电保持须人工复测。",
               .require_detection = 1},
    [T_KEY] =
        {.key = "keys",
         .name = "按键",
         .page = 6,
         .instruction =
             "RECOVERY/PWRON由input驱动上报；RESET/MASKROM需单独重启验证。"},
    [T_LED] = {.key = "user_led",
               .name = "用户指示灯",
               .page = 7,
               .instruction =
                   "LED5 / GPIO4_C7：需专属LED驱动；运行后观察闪烁。",
               .require_detection = 1},
    [T_FAN] = {.key = "fan",
               .name = "风扇",
               .page = 7,
               .instruction = "U15：需核对PWM引脚，运行3秒后恢复原占空比。",
               .require_detection = 1},
    [T_UART] = {.key = "debug_uart",
                .name = "DEBUG UART",
                .page = 8,
                .instruction =
                    "U35：1.8V调试串口，默认人工收发验证；不占用控制台。"},
    [T_POWER] = {.key = "power",
                 .name = "电源与电源灯",
                 .page = 8,
                 .instruction =
                     "DC1 / LED6：人工测量12V、5V、3.3V并观察电源灯。"}};
void factory_init(struct factory *f) {
  memset(f, 0, sizeof(*f));
  memcpy(f->tests, definitions, sizeof(definitions));
  copy(f->config.audio_card, sizeof(f->config.audio_card), "auto");
  copy(f->config.audio_pcm, sizeof(f->config.audio_pcm), "auto");
  copy(f->config.tf_device, sizeof(f->config.tf_device), "auto");
  f->job_test = -1;
  f->tf_auto = 1;
  f->session_id = (unsigned long)time(NULL);
  copy(f->message, sizeof(f->message),
       "启动自动测试；可随时接入网线、HDMI及USB外设。");
}
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
               s[n - 1] == '\n'))
    s[--n] = 0;
  return s;
}
int factory_load_config(struct factory *f, const char *path) {
  FILE *fp = fopen(path, "r");
  char line[1024];
  int lineno = 0;
  if (!fp)
    return -1;
  struct {
    const char *key;
    char *value;
    size_t size;
  } fields[] = {
#define FIELD(k, v) {k, f->config.v, sizeof(f->config.v)}
      FIELD("audio_card", audio_card),
      FIELD("audio_pcm", audio_pcm),
      FIELD("ethernet0_peer", network_peer[0]),
      FIELD("ethernet1_peer", network_peer[1]),
      FIELD("usb1_port", usb1),
      FIELD("usb2_port", usb2),
      FIELD("typec_port", typec),
      FIELD("tf_device", tf_device),
      FIELD("m2_0_device", m2_0_device),
      FIELD("m2_1_device", m2_1_device),
      FIELD("csi1_video", camera[0]),
      FIELD("csi2_video", camera[1]),
      FIELD("csi3_video", camera[2]),
      FIELD("rtc_device", rtc),
      FIELD("led_path", led),
      FIELD("fan_pwm_path", fan),
      FIELD("uart_device", uart)
#undef FIELD
  };
  while (fgets(line, sizeof(line), fp)) {
    lineno++;
    char *s = trim(line);
    if (!*s || *s == '#')
      continue;
    char *eq = strchr(s, '=');
    if (!eq) {
      snprintf(f->message, sizeof(f->message), "配置第%d行缺少等号", lineno);
      fclose(fp);
      return -2;
    }
    *eq++ = 0;
    s = trim(s);
    eq = trim(eq);
    int found = 0;
    if (!strcmp(s, "pcie1_mode")) {
      if (strcmp(eq, "0") && strcmp(eq, "1"))
        goto invalid;
      f->config.pcie1_mode = atoi(eq);
      continue;
    }
    for (size_t j = 0; j < sizeof(fields) / sizeof(fields[0]); j++)
      if (!strcmp(s, fields[j].key)) {
        if (strlen(eq) >= fields[j].size)
          goto invalid;
        copy(fields[j].value, fields[j].size, eq);
        if (!strcmp(s, "tf_device"))
          f->tf_auto = !strcmp(eq, "auto");
        found = 1;
        break;
      }
    if (!found)
      goto invalid;
    continue;
  invalid:
    snprintf(f->message, sizeof(f->message), "配置第%d行的键或值无效：%.120s",
             lineno, s);
    fclose(fp);
    return -2;
  }
  fclose(fp);
  return 0;
}
static int codec_card(struct factory *f) {
  char text[256], path[128];
  for (int i = 0; i < 32; i++) {
    snprintf(path, sizeof(path), "/proc/asound/card%d/id", i);
    if (rooted_read(f, path, text, sizeof(text)))
      continue;
    if (!strcmp(f->config.audio_card, "auto")) {
      if (strcasestr(text, "es8388") || strcasestr(text, "es8323"))
        return i;
    } else if (!strcmp(text, f->config.audio_card))
      return i;
    else {
      char *end;
      long n = strtol(f->config.audio_card, &end, 10);
      if (*f->config.audio_card && !*end && n == i)
        return i;
    }
  }
  return -1;
}
#include "usb.inc"
static void scan_devices(struct factory *f) {
  glob_t g;
  char a[256], b[256], p[2048], text[512];
  f->inventory[0] = 0;
  scan_usb(f);
  for (int id = T_TF; id <= T_TF; id++) {
    char *dev = id == T_TF     ? f->config.tf_device
                : id == T_M2_0 ? f->config.m2_0_device
                               : f->config.m2_1_device;
    if (id == T_TF && f->tf_auto) {
      copy(dev, 128, "auto");
      if (match_paths(f, "/sys/block/mmcblk*/device/type", &g))
        for (size_t i = 0; i < g.gl_pathc; i++) {
          if (read_text(g.gl_pathv[i], a, sizeof(a)) || strcmp(a, "SD"))
            continue;
          char d[1024];
          copy(d, sizeof(d), g.gl_pathv[i]);
          char *q = strstr(d, "/device/type");
          if (q)
            *q = 0;
          snprintf(dev, 128, "/dev/%s", strrchr(d, '/') + 1);
          break;
        }
      globfree(&g);
    }
    int found = 0;
    if (!strncmp(dev, "/dev/", 5)) {
      snprintf(p, sizeof(p), "/sys/class/block/%s/size", dev + 5);
      found = rooted_read(f, p, a, sizeof(a)) == 0 && strtoull(a, NULL, 10) > 0;
      snprintf(text, sizeof(text), "%s：%s", dev,
               found ? "已识别，待只读抽样" : "未检测到块设备");
    } else
      copy(text, sizeof(text),
           id == T_TF ? "未发现SD类型TF卡"
                      : "未配置块设备；请按插槽核对NVMe设备名。");
    reset_item(&f->tests[id], found, text);
  }
  for (int slot = 0; slot < 2; slot++) {
    struct nvme_probe probe;
    int found = !nvme_probe_slot(f->config.root, slot,
                                slot ? f->config.m2_1_device : f->config.m2_0_device,
                                &probe);
    if (slot && !f->config.pcie1_mode) {
      found = 0;
      copy(probe.evidence, sizeof(probe.evidence),
           "USB Hub启动模式；PCIe1未启用，需另一个启动配置复测");
    }
    reset_item(&f->tests[T_M2_0 + slot], found, probe.evidence);
  }
  if (match_paths(f, "/sys/class/nvme/nvme*/model", &g))
    for (size_t i = 0; i < g.gl_pathc; i++) {
      read_text(g.gl_pathv[i], a, sizeof(a));
      append(f->inventory, sizeof(f->inventory), "NVMe: %s\n", a);
    }
  globfree(&g);
  if (match_paths(f, "/sys/bus/pci/devices/*/class", &g))
    for (size_t i = 0; i < g.gl_pathc; i++) {
      read_text(g.gl_pathv[i], a, sizeof(a));
      append(f->inventory, sizeof(f->inventory), "PCIe %s: %s\n",
             g.gl_pathv[i] + strlen(f->config.root) + 21, a);
    }
  globfree(&g);
  for (int id = T_HDMI; id <= T_DSI; id++) {
    int found = 0;
    text[0] = 0;
    if (match_paths(f,
                    id == T_HDMI ? "/sys/class/drm/card*-HDMI-A-*/status"
                                 : "/sys/class/drm/card*-DSI-*/status",
                    &g))
      for (size_t i = 0; i < g.gl_pathc; i++) {
        read_text(g.gl_pathv[i], a, sizeof(a));
        if (strcmp(a, "connected"))
          continue;
        found = 1;
        copy(p, sizeof(p), g.gl_pathv[i]);
        char *q = strrchr(p, '/');
        if (q)
          strcpy(q + 1, "modes");
        read_text(p, b, sizeof(b));
        snprintf(text, sizeof(text), "已连接；模式：%.200s", b);
        break;
      }
    globfree(&g);
    if (!found)
      copy(text, sizeof(text), "DRM未报告连接；检查显示器、面板设备树与驱动。");
    reset_item(&f->tests[id], found, text);
  }
  for (int id = T_CSI1; id <= T_CSI3; id++) {
    const char *v = factory_camera(f, id);
    int found = 0;
    if (!strncmp(v, "/dev/video", 10)) {
      snprintf(p, sizeof(p), "/sys/class/video4linux/%s/name", v + 5);
      found = rooted_read(f, p, a, sizeof(a)) == 0;
    }
    snprintf(text, sizeof(text),
             *v ? "%s：%s；仍需实际取帧预览" : "未配置CSI对应的ISP视频节点", v,
             found ? a : "未检测到节点");
    reset_item(&f->tests[id], found, text);
  }
  if (match_paths(f, "/sys/class/video4linux/video*/name", &g))
    for (size_t i = 0; i < g.gl_pathc; i++) {
      read_text(g.gl_pathv[i], a, sizeof(a));
      append(f->inventory, sizeof(f->inventory), "Video: %s (%s)\n",
             g.gl_pathv[i] + strlen(f->config.root) + 26, a);
    }
  globfree(&g);
  for (int id = T_LED; id <= T_FAN; id++) {
    const char *v = id == T_LED ? f->config.led : f->config.fan;
    int found = 0;
    if (*v) {
      snprintf(p, sizeof(p), "%s%s%s", f->config.root, v,
               id == T_LED ? "/brightness" : "");
      found = read_text(p, a, sizeof(a)) == 0;
    }
    snprintf(text, sizeof(text), "%s",
             *v ? (found ? v : "配置节点不存在；核对设备树")
                : "未配置专属驱动节点；不直接操作未核对GPIO/PWM。");
    reset_item(&f->tests[id], found, text);
  }
}
void factory_scan(struct factory *f) {
  char evidence[512], path[256], a[128], b[128];
  struct test_item before[TEST_COUNT];
  memcpy(before, f->tests, sizeof(before));
  glob_t g;
  int card = codec_card(f);
  snprintf(evidence, sizeof(evidence),
           card >= 0 ? "ES8388兼容声卡 card%d；MIC2板载麦克风"
                     : "未找到ES8388声卡；检查I2C4@0x11 / SAI2驱动",
           card);
  for (int id = T_SPEAKER; id <= T_HEADSET_MIC; id++)
    reset_item(&f->tests[id], card >= 0, evidence);
  int n = 0;
  if (match_paths(f, "/sys/class/net/*/carrier", &g))
    for (size_t i = 0; i < g.gl_pathc && n < 2; i++) {
      char base[1024];
      copy(base, sizeof(base), g.gl_pathv[i]);
      *strrchr(base, '/') = 0;
      const char *name = strrchr(base, '/') + 1;
      if (!strncmp(name, "lo", 2) || !strncmp(name, "wl", 2) ||
          !strncmp(name, "usb", 3) || !strncmp(name, "veth", 4))
        continue;
      snprintf(path, sizeof(path), "/sys/class/net/%s/phydev/phy_id", name);
      char phy[64];
      if (rooted_read(f, path, phy, sizeof(phy)))
        continue;
      read_text(g.gl_pathv[i], a, sizeof(a));
      snprintf(path, sizeof(path), "/sys/class/net/%s/speed", name);
      rooted_read(f, path, b, sizeof(b));
      char ip[INET_ADDRSTRLEN] = "未获取IPv4";
      if (!*f->config.root) {
        struct ifaddrs *list = NULL;
        if (!getifaddrs(&list)) {
          for (struct ifaddrs *it = list; it; it = it->ifa_next)
            if (it->ifa_addr && it->ifa_addr->sa_family == AF_INET &&
                !strcmp(it->ifa_name, name)) {
              struct sockaddr_in *addr = (struct sockaddr_in *)it->ifa_addr;
              inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
              break;
            }
          freeifaddrs(list);
        }
      } else {
        snprintf(path, sizeof(path), "/sys/class/net/%s/fixture_ipv4", name);
        rooted_read(f, path, ip, sizeof(ip));
      }
      snprintf(evidence, sizeof(evidence),
               "%.64s：Link %s / %.64s Mb/s / IPv4 %s / PHY %s", name,
               !strcmp(a, "1") ? "UP" : "DOWN", b, ip, phy);
      copy(f->ethernet[n], sizeof(f->ethernet[n]), name);
      reset_item(&f->tests[T_ETH0 + n], !strcmp(a, "1"), evidence);
      n++;
    }
  globfree(&g);
  while (n < 2)
    reset_item(&f->tests[T_ETH0 + n++], 0,
               "未找到带PHY的以太网口；核对GMAC与PHY驱动。");
  scan_devices(f);
  rooted_read(f, "/sys/class/net/wlan0/device/modalias", a, sizeof(a));
  int onboard = !strcmp(a, "sdio:c07vC8A1d0082");
  rooted_read(f, "/sys/class/net/wlan0/carrier", b, sizeof(b));
  int wifi_link = onboard && !strcmp(b, "1");
  reset_item(&f->tests[T_WIFI], wifi_link,
             wifi_link ? "板载 SDIO 无线接口 wlan0 链路已就绪。" :
             onboard ? "已识别板载无线网卡，等待连接热点。" :
                       "未发现板载 AIC8800D80 无线接口。");
  const int live[] = {T_ETH0, T_ETH1, T_USB1, T_USB2, T_TYPEC, T_HDMI, T_WIFI};
  for (size_t i = 0; i < sizeof(live) / sizeof(live[0]); i++)
    live_result(f, live[i]);
  for (int i = 0; i < ACTIVE_TEST_COUNT; i++) {
    int id = factory_active_tests[i];
    struct test_item *t = &f->tests[id];
    if (f->monitoring && !before[id].detected && t->detected &&
        t->state != PASS &&
        (id == T_TF || id == T_M2_0 || id == T_M2_1 ||          (id >= T_CSI1 && id <= T_CSI3) || id == T_BOARD_MIC ||
         id == T_SPEAKER))
      f->pending_tests |= 1u << id;
    if (memcmp(&before[id], t, sizeof(*t)))
      f->revision++;
  }
}
const char *factory_camera(struct factory *f, int id) {
  return id >= T_CSI1 && id <= T_CSI3 ? f->config.camera[id - T_CSI1] : "";
}
static int command(char *const argv[]) {
  if (stopping)
    return -1;
  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (!pid) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    execvp(argv[0], argv);
    perror(argv[0]);
    _exit(127);
  }
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    return -1;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
struct saved_control {
  snd_ctl_elem_value_t *value;
};
static int mixer_set(snd_ctl_t *ctl, struct saved_control *saved, int *count,
                     const char *name, const char *setting) {
  snd_ctl_elem_info_t *info;
  snd_ctl_elem_value_t *value;
  snd_ctl_elem_info_alloca(&info);
  snd_ctl_elem_value_alloca(&value);
  snd_ctl_elem_info_set_interface(info, SND_CTL_ELEM_IFACE_MIXER);
  snd_ctl_elem_info_set_name(info, name);
  int rc = snd_ctl_elem_info(ctl, info);
  if (rc < 0) {
    fprintf(stderr, "缺少mixer控件 %s: %s\n", name, snd_strerror(rc));
    return -1;
  }
  snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_MIXER);
  snd_ctl_elem_value_set_name(value, name);
  if (snd_ctl_elem_read(ctl, value) < 0)
    return -1;
  if (snd_ctl_elem_value_malloc(&saved[*count].value) < 0)
    return -1;
  snd_ctl_elem_value_copy(saved[*count].value, value);
  (*count)++;
  unsigned int num = snd_ctl_elem_info_get_count(info);
  if (snd_ctl_elem_info_get_type(info) == SND_CTL_ELEM_TYPE_ENUMERATED) {
    unsigned int j;
    for (j = 0; j < snd_ctl_elem_info_get_items(info); j++) {
      snd_ctl_elem_info_set_item(info, j);
      snd_ctl_elem_info(ctl, info);
      if (!strcmp(setting, snd_ctl_elem_info_get_item_name(info)))
        break;
    }
    if (j == snd_ctl_elem_info_get_items(info)) {
      fprintf(stderr, "%s 无枚举值 %s\n", name, setting);
      return -1;
    }
    for (unsigned int k = 0; k < num; k++)
      snd_ctl_elem_value_set_enumerated(value, k, j);
  } else
    for (unsigned int k = 0; k < num; k++)
      snd_ctl_elem_value_set_integer(value, k, strtol(setting, NULL, 10));
  rc = snd_ctl_elem_write(ctl, value);
  if (rc < 0) {
    fprintf(stderr, "写mixer %s失败: %s\n", name, snd_strerror(rc));
    return -1;
  }
  return 0;
}
static int make_wave(const char *path, int speaker) {
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return -1;
  unsigned char h[44] = {
      'R', 'I', 'F', 'F', 0,  0, 0,   0,   'W', 'A',  'V',  'E', 'f', 'm', 't',
      ' ', 16,  0,   0,   0,  1, 0,   2,   0,   0x80, 0xbb, 0,   0,   0,   0xee,
      2,   0,   4,   0,   16, 0, 'd', 'a', 't', 'a',  0,    0,   0,   0};
  uint32_t n = 48000 * 4 * 4, sz = n + 36;
  for (int k = 0; k < 4; k++) {
    h[4 + k] = (sz >> (8 * k)) & 255;
    h[40 + k] = (n >> (8 * k)) & 255;
  }
  if (fwrite(h, 1, 44, fp) != 44) {
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < 48000 * 4;
       i++) { /* Alternating left/right square tone at modest amplitude. */
    int16_t s[2] = {0, 0};
    if (speaker) {
      int channel = i / 96000;
      double hz = channel ? 1190.0 : 730.0;
      if (i % 96000 < 76800)
        s[channel] = (int16_t)(2400.0 * sin(2.0 * M_PI * hz * i / 48000.0));
    } else {
      s[i / 48000 % 2] = (int16_t)((i % 120 < 60 ? 1 : -1) * 1197);
    }
    if (fwrite(s, sizeof(s), 1, fp) != 1) {
      fclose(fp);
      return -1;
    }
  }
  return fclose(fp);
}
static int audio_run(struct factory *f, int id) {
  int card = codec_card(f);
  if (card < 0)
    return -1;
  char ctlname[64], pcm[128], wav[512];
  snprintf(ctlname, sizeof(ctlname), "hw:%d", card);
  snprintf(pcm, sizeof(pcm), "plughw:%d,0", card);
  if (strcmp(f->config.audio_pcm, "auto"))
    copy(pcm, sizeof(pcm), f->config.audio_pcm);
  snprintf(wav, sizeof(wav), "%.450s/%s.wav",
           f->job_automatic ? f->artifact_dir : f->run_dir,
           id == T_BOARD_MIC     ? "board-mic"
           : id == T_HEADSET_MIC ? "headset-mic"
                                 : "playback");
  snd_ctl_t *ctl;
  int rc = snd_ctl_open(&ctl, ctlname, 0);
  if (rc < 0) {
    fprintf(stderr, "打开声卡失败: %s\n", snd_strerror(rc));
    return -1;
  }
  struct saved_control saved[24];
  int count = 0;
  rc = -1;
#define SET(n, v)                                                              \
  do {                                                                         \
    if (mixer_set(ctl, saved, &count, n, v))                                   \
      goto out;                                                                \
  } while (0)
  SET("Left Mixer Left Playback Switch", "1");
  SET("Right Mixer Right Playback Switch", "1");
  SET("OUT1 Switch", id == T_SPEAKER ? "0" : "1");
  SET("OUT2 Switch", "1");
  SET("PCM Volume", id == T_SPEAKER ? "189" : "170");
  SET("Output 1 Playback Volume", "26");
  SET("Output 2 Playback Volume", id == T_SPEAKER ? "27" : "26");
  SET("Headphone Switch", id == T_SPEAKER ? "0" : "1");
  SET("Speaker Switch", "1");
  if (id == T_BOARD_MIC || id == T_HEADSET_MIC) {
    SET("Main Mic Switch", id == T_BOARD_MIC ? "1" : "0");
    SET("Headset Mic Switch", id == T_HEADSET_MIC ? "1" : "0");
    SET("Left PGA Mux", "DifferentialL");
    SET("Right PGA Mux", "DifferentialR");
    SET("Differential Mux", id == T_BOARD_MIC ? "Line 2" : "Line 1");
    SET("Capture Mute", "0");
    SET("Capture Digital Volume", "192");
    SET("Left Channel Capture Volume", "6");
    SET("Right Channel Capture Volume", "6");
    puts("录音增益：PGA 18dB / ADC数字0dB；结束后恢复原设置。");
    char *args[] = {"arecord", "-q", "-D", pcm,  "-f", "S16_LE", "-r",
                    "48000",   "-c", "2",  "-d", "5",  wav,      NULL};
    puts("正在录音5秒，无需说话……");
    fflush(stdout);
    char *suspended[] = {"/usr/bin/cm5-audio-run", "arecord", "-q",    "-D", pcm,
                         "-f",          "S16_LE", "-r",      "48000", "-c", "2",
                         "-d",          "5",      wav,       NULL};
    if (command(access("/usr/bin/cm5-audio-run", X_OK) == 0 ? suspended : args))
      goto out;
    if (f->job_automatic) {
      struct recording_info info;
      rc = check_pcm_recording(wav, &info);
      if (!rc)
        printf("录音完成：%.1f秒 / %u Hz / %u声道 / %u位 / %u bytes；"
               "文件完整，录音通过。\n录音文件：%s\n",
               (double)info.frames / info.rate, info.rate, info.channels,
               info.bits, info.bytes, wav);
      else
        fprintf(stderr, "录音未通过：WAV格式无效、数据不完整或未录满5秒。\n");
      goto out;
    }
  } else if (make_wave(wav, id == T_SPEAKER))
    goto out;
  if (!stopping) {
    puts("正在播放，先左后右；完成后仍需人工听音确认。");
    fflush(stdout);
    char *args[] = {"aplay", "-q", "-D", pcm, wav, NULL};
    char *suspended[] = {"/usr/bin/cm5-audio-run", "aplay", "-q",
                         "-D",          pcm,  wav,     NULL};
    rc = command(access("/usr/bin/cm5-audio-run", X_OK) == 0 ? suspended : args);
  }
out:
  for (int k = count - 1; k >= 0; k--) {
    if (snd_ctl_elem_write(ctl, saved[k].value) < 0) {
      fprintf(stderr, "恢复mixer失败\n");
      rc = -1;
    }
    snd_ctl_elem_value_free(saved[k].value);
  }
  snd_ctl_close(ctl);
  if (!f->job_automatic || (id != T_BOARD_MIC && id != T_HEADSET_MIC))
    unlink(wav);
  if (rc)
    fprintf(stderr, "音频未完成；若设备忙，请先释放PulseAudio对声卡的占用，或显"
                    "式配置已路由到ES8388的PCM。\n");
  return rc;
#undef SET
}
static int read_storage_checked(const char *path, const struct nvme_probe *expected) {
  if (strncmp(path, "/dev/", 5)) {
    fprintf(stderr, "请配置实际块设备\n");
    return -1;
  }
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | (expected ? O_NOFOLLOW : 0));
  if (fd < 0) {
    perror(path);
    return -1;
  }
  struct stat st;
  if (fstat(fd, &st) || !S_ISBLK(st.st_mode) ||
      (expected && st.st_rdev != expected->rdev)) {
    fprintf(stderr, "不是块设备\n");
    close(fd);
    return -1;
  }
  unsigned char data[4096];
  unsigned long hash = 2166136261u;
  size_t total = 0;
  while (total < 1024 * 1024 && !stopping) {
    ssize_t n = read(fd, data, sizeof(data));
    if (n <= 0) {
      perror("read");
      close(fd);
      return -1;
    }
    for (ssize_t j = 0; j < n; j++)
      hash = (hash ^ data[j]) * 16777619u;
    total += (size_t)n;
  }
  close(fd);
  printf(
      "只读抽样 %s：%zu bytes / checksum %08lx；未验证写入、全容量或性能。\n",
      path, total, hash & 0xffffffffUL);
  return stopping ? -1 : 0;
}
static int read_storage(const char *path) {
  return read_storage_checked(path, NULL);
}
static int read_nvme(struct factory *f, int slot) {
  struct nvme_probe before, after;
  const char *selection = slot ? f->config.m2_1_device : f->config.m2_0_device;
  if ((slot && !f->config.pcie1_mode) ||
      nvme_probe_slot(f->config.root, slot, selection, &before)) {
    fprintf(stderr, "NVMe目标不可用或启动配置不匹配\n");
    return -1;
  }
  printf("%s\n", before.evidence);
  if (read_storage_checked(before.device, &before))
    return -1;
  if (nvme_probe_slot(f->config.root, slot, selection, &after) ||
      before.rdev != after.rdev || strcmp(before.device, after.device) ||
      strcmp(before.evidence, after.evidence)) {
    fprintf(stderr, "NVMe设备在读取期间发生变化\n");
    return -1;
  }
  return 0;
}
static int actuator_run(struct factory *f, int id) {
  char p[512], old[128], trigger[512] = "", max[64] = "255";
  const char *base = id == T_LED ? f->config.led : f->config.fan;
  if ((id == T_LED && strncmp(base, "/sys/class/leds/", 16)) ||
      (id == T_FAN && strncmp(base, "/sys/class/hwmon/", 17))) {
    fprintf(stderr, "必须配置专属LED / hwmon节点\n");
    return -1;
  }
  snprintf(p, sizeof(p), "%s%s", base, id == T_LED ? "/brightness" : "");
  if (read_text(p, old, sizeof(old)))
    return -1;
  char tp[512];
  snprintf(tp, sizeof(tp), "%s/trigger", base);
  if (id == T_LED) {
    char all[1024];
    if (!read_text(tp, all, sizeof(all))) {
      char *a = strchr(all, '['), *b = a ? strchr(a, ']') : NULL;
      if (a && b) {
        *b = 0;
        copy(trigger, sizeof(trigger), a + 1);
        if (write_text(tp, "none"))
          return -1;
      }
    }
    char mp[512];
    snprintf(mp, sizeof(mp), "%s/max_brightness", base);
    read_text(mp, max, sizeof(max));
  }
  int rc = 0;
  for (int k = 0; k < 6 && !stopping; k++) {
    if (write_text(p, id == T_FAN ? "255" : k % 2 ? "0" : max)) {
      rc = -1;
      break;
    }
    usleep(500000);
  }
  if (write_text(p, old))
    rc = -1;
  if (*trigger && write_text(tp, trigger))
    rc = -1;
  printf("%s运行结束，已尝试恢复原状态；请观察确认。\n",
         id == T_LED ? "指示灯" : "风扇");
  return stopping ? -1 : rc;
}
static int uart_run(struct factory *f) {
  const char *dev = f->config.uart;
  char cmdline[4096];
  read_text("/proc/cmdline", cmdline, sizeof(cmdline));
  const char *name = strrchr(dev, '/');
  if (strncmp(dev, "/dev/tty", 8) || !name || strstr(cmdline, name + 1)) {
    fprintf(stderr,
            "未配置可用回环串口，或该串口为内核控制台；保持人工验证。\n");
    return -1;
  }
  int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct termios old, t;
  int rc = -1;
  if (tcgetattr(fd, &old)) {
    close(fd);
    return -1;
  }
  t = old;
  cfmakeraw(&t);
  cfsetispeed(&t, B115200);
  cfsetospeed(&t, B115200);
  t.c_cflag |= CLOCAL | CREAD;
  t.c_cflag &= ~CRTSCTS;
  if (ioctl(fd, TIOCEXCL) || tcsetattr(fd, TCSANOW, &t))
    goto out;
  tcflush(fd, TCIOFLUSH);
  const char msg[] = "CM5_UART_LOOP_0123456789\r\n";
  char got[sizeof(msg)] = {0};
  size_t sent = 0, received = 0;
  double until = seconds() + 2;
  while (!stopping && seconds() < until && received < sizeof(msg) - 1) {
    struct pollfd p = {
        .fd = fd, .events = POLLIN | (sent < sizeof(msg) - 1 ? POLLOUT : 0)};
    if (poll(&p, 1, 50) < 0 && errno != EINTR)
      break;
    if (p.revents & POLLOUT) {
      ssize_t n = write(fd, msg + sent, sizeof(msg) - 1 - sent);
      if (n > 0)
        sent += (size_t)n;
    }
    if (p.revents & POLLIN) {
      ssize_t n = read(fd, got + received, sizeof(msg) - 1 - received);
      if (n > 0)
        received += (size_t)n;
    }
  }
  rc = received == sizeof(msg) - 1 && !memcmp(msg, got, received) ? 0 : -1;
  printf("UART 115200回环：%zu/%zu字节，%s\n", received, sizeof(msg) - 1,
         rc ? "失败" : "匹配");
out:
  tcsetattr(fd, TCSANOW, &old);
  ioctl(fd, TIOCNXCL);
  close(fd);
  return rc;
}
#include "auto.inc"
static int start_job(struct factory *f, int id, int automatic) {
  if (!factory_test_enabled(id) || f->job_pid || *f->config.root ||
      f->tests[id].state == PASS)
    return -1;
  if (id == T_UART && !*f->config.uart)
    return -1;
  if (f->tests[id].require_detection && !f->tests[id].detected)
    return -1;
  if (!automatic && id > T_HEADSET_MIC && id != T_TF && id != T_M2_0 &&
      id != T_M2_1 && id != T_LED && id != T_FAN && id != T_UART)
    return -1;
  f->pending_tests &= ~(1u << id);
  f->job_automatic = automatic;
  if (!*f->run_dir) {
    copy(f->run_dir, sizeof(f->run_dir), "/tmp/cm5-factory-XXXXXX");
    if (!mkdtemp(f->run_dir)) {
      f->run_dir[0] = 0;
      return -1;
    }
  }
  snprintf(f->log_path, sizeof(f->log_path), "%.200s/job.log", f->run_dir);
  int log = open(f->log_path,
                 O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (log < 0)
    return -1;
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    close(log);
    return -1;
  }
  if (!pid) {
    setpgid(0, 0);
    dup2(log, STDOUT_FILENO);
    dup2(log, STDERR_FILENO);
    close(log);
    signal(SIGTERM, terminate_job);
    signal(SIGINT, terminate_job);
    stopping = 0;
    int rc = -1;
    if (automatic && ((id >= T_CSI1 && id <= T_CSI3) || id == T_USB1 ||
                      id == T_USB2 || id == T_ETH0 || id == T_ETH1))
      rc = auto_worker(f, id);
    else if (id <= T_HEADSET_MIC)
      rc = audio_run(f, id);
    else if (id == T_TF)
      rc = read_storage(f->config.tf_device);
    else if (id == T_M2_0 || id == T_M2_1)
      rc = read_nvme(f, id - T_M2_0);
    else if (id == T_LED || id == T_FAN)
      rc = actuator_run(f, id);
    else if (id == T_UART)
      rc = uart_run(f);
    fflush(NULL);
    _exit(rc ? 1 : 0);
  }
  close(log);
  setpgid(pid, pid);
  f->job_pid = pid;
  f->job_test = id;
  f->job_started = seconds();
  f->tests[id].state = RUNNING;
  f->tests[id].checked = 0;
  f->tests[id].auto_waiting = 0;
  f->tests[id].automatic = automatic && auto_kind(id) == 1;
  f->revision++;
  return 0;
}
int factory_start(struct factory *f, int id) {
  if (f->auto_running)
    return -1;
  return start_job(f, id, 0);
}
int factory_retry(struct factory *f, int id) {
  if (!factory_test_enabled(id) || !auto_kind(id) || f->job_pid ||
      f->auto_running || !*f->artifact_dir || f->tests[id].state == PASS)
    return -1;
  factory_scan(f);
  if (auto_missing(f, id) ||
      (f->tests[id].require_detection && !f->tests[id].detected))
    return -1;
  return start_job(f, id, 1);
}
static void auto_step(struct factory *f);
void factory_poll(struct factory *f) {
  if (!f->job_pid) {
    if (f->auto_running)
      auto_step(f);
    else if (f->monitoring && f->pending_tests && !*f->config.root) {
      for (int i = 0; i < ACTIVE_TEST_COUNT; i++) {
        int id = factory_active_tests[i];
        if (!(f->pending_tests & (1u << id)))
          continue;
        f->pending_tests &= ~(1u << id);
        if (f->tests[id].state != PASS && f->tests[id].detected &&
            !auto_missing(f, id)) {
          factory_retry(f, id);
          break;
        }
      }
    }
    return;
  }
  int status;
  pid_t r = waitpid(f->job_pid, &status, WNOHANG);
  if (r == f->job_pid) {
    struct test_item *t = &f->tests[f->job_test];
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    t->state = ok ? (t->automatic ? PASS : READY) : FAIL;
    t->checked = ok;
    read_text(f->log_path, t->evidence, sizeof(t->evidence));
    copy(f->message, sizeof(f->message),
         ok ? "操作完成，请根据实际声音/画面/机械动作确认结果。"
            : "操作失败，详见当前项证据与运行日志。");
    f->job_pid = 0;
    f->revision++;
  } else if (r < 0 && errno == ECHILD) {
    f->tests[f->job_test].state = FAIL;
    f->job_pid = 0;
    f->revision++;
  } else if (seconds() - f->job_started > 30)
    factory_cancel(f);
}
void factory_cancel(struct factory *f) {
  if (!f->job_pid)
    return;
  f->job_started = seconds();
  kill(-f->job_pid, SIGTERM);
  double until = seconds() + 3;
  while (f->job_pid && seconds() < until) {
    factory_poll(f);
    usleep(20000);
  }
  if (f->job_pid) {
    kill(-f->job_pid, SIGKILL);
    waitpid(f->job_pid, NULL, 0);
    f->tests[f->job_test].state = FAIL;
    f->tests[f->job_test].checked = 0;
    snprintf(f->tests[f->job_test].evidence,
             sizeof(f->tests[f->job_test].evidence),
             "任务取消或超时后被强制终止，未完成有效测量；请复测。");
    f->job_pid = 0;
    f->revision++;
  }
  copy(f->message, sizeof(f->message),
       "任务已停止；请核对当前测试项后重新运行。");
}
static void auto_step(struct factory *f) {
  while (f->auto_running && !f->job_pid && f->auto_next < TEST_COUNT) {
    int id = f->auto_next++;
    if (!factory_test_enabled(id) || !auto_kind(id) || f->tests[id].checked ||
        f->tests[id].state == SKIP)
      continue;
    factory_scan(f);
    struct test_item *t = &f->tests[id];
    const char *missing = auto_missing(f, id);
    if (missing || (t->require_detection && !t->detected)) {
      t->state = UNKNOWN;
      t->checked = 0;
      t->automatic = 0;
      t->auto_waiting = 1;
      snprintf(t->evidence, sizeof(t->evidence), "自动测试待准备：%s。",
               missing ? missing : "等待设备接入，接入后自动测试");
      f->revision++;
      continue;
    }
    if (start_job(f, id, 1)) {
      t->state = UNKNOWN;
      t->auto_waiting = 1;
      t->checked = 0;
      t->automatic = 0;
      snprintf(t->evidence, sizeof(t->evidence),
               "自动测试无法启动：设备未连接或节点不可用；检查接线与配置。");
      f->revision++;
      continue;
    }
    snprintf(f->message, sizeof(f->message), "自动测试：%s%s", t->name,
             id == T_BOARD_MIC || id == T_HEADSET_MIC ? "，录音5秒，无需说话"
                                                      : "");
  }
  if (f->auto_running && !f->job_pid && f->auto_next >= TEST_COUNT) {
    f->auto_running = 0;
    f->revision++;
    copy(f->message, sizeof(f->message),
         "首轮测试完成，持续检测中。可随时接入网线、HDMI或USB；"
         "轻触小屏测试输入，扬声器听音后确认。");
  }
}
int factory_auto_begin(struct factory *f, const char *report_path) {
  if (f->job_pid || f->auto_running || *f->config.root)
    return -1;
  if (!*f->artifact_dir &&
      snprintf(f->artifact_dir, sizeof(f->artifact_dir), "%s.assets-XXXXXX",
               report_path) >= (int)sizeof(f->artifact_dir))
    return -1;
  if (strstr(f->artifact_dir, "XXXXXX") && !mkdtemp(f->artifact_dir)) {
    f->artifact_dir[0] = 0;
    return -1;
  }
  f->monitoring = 1;
  f->auto_running = 1;
  f->auto_next = 0;
  factory_scan(f);
  f->revision++;
  auto_step(f);
  return 0;
}
void factory_auto_cancel(struct factory *f) {
  int active = f->monitoring;
  f->monitoring = 0;
  f->pending_tests = 0;
  f->auto_running = 0;
  factory_cancel(f);
  if (active) {
    f->revision++;
    copy(f->message, sizeof(f->message),
         "检测已暂停；已完成结果保留，点击继续检测可恢复。");
  }
}
void factory_destroy(struct factory *f) {
  factory_auto_cancel(f);
  if (*f->log_path)
    unlink(f->log_path);
  if (*f->run_dir)
    rmdir(f->run_dir);
}
int factory_can_pass(struct factory *f, int id) {
  if (!factory_test_enabled(id) || f->tests[id].state == RUNNING)
    return 0;
  if (id != T_SPEAKER && id != T_KEY)
    return 0;
  struct test_item *t = &f->tests[id];
  if (t->require_detection && !t->detected)
    return 0;
  if ((id <= T_HEADSET_MIC || id == T_TF || id == T_M2_0 || id == T_M2_1 ||
       id == T_RTC || id == T_LED || id == T_FAN ||
       (id >= T_CSI1 && id <= T_CSI3) || id == T_TOUCH || id == T_DSI ||
       id == T_HDMI) &&
      !t->checked)
    return 0;
  return 1;
}
int factory_decide(struct factory *f, int id, enum test_state state) {
  if (!factory_test_enabled(id) || f->tests[id].state == RUNNING)
    return -1;
  struct test_item *t = &f->tests[id];
  if (t->state == PASS) {
    if (state == PASS)
      return 0;
    copy(f->message, sizeof(f->message),
         "本轮已通过，结果保留；需要重测请点击顶部“重新测试”。");
    return -1;
  }
  if (state == PASS && !factory_can_pass(f, id))
    goto denied;
  t->state = state;
  t->automatic = 0;
  t->auto_waiting = 0;
  f->revision++;
  return 0;
denied:
  copy(f->message, sizeof(f->message),
       "尚未完成此项操作或必要设备未检测到，不能确认通过。");
  return -1;
}
const char *factory_state_name(enum test_state s) {
  static const char *names[] = {"未测试", "待确认", "运行中",
                                "通过",   "失败",   "跳过"};
  return s >= UNKNOWN && s <= SKIP ? names[s] : "未知";
}
static void json_string(FILE *fp, const char *s) {
  fputc('"', fp);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') {
      fputc('\\', fp);
      fputc(*p, fp);
    } else if (*p < 32)
      fprintf(fp, "\\u%04x", *p);
    else if (*p >= 128) {
      /* USB strings and truncated diagnostic logs may contain invalid UTF-8. */
      int n = *p >= 0xc2 && *p <= 0xdf   ? 2
              : *p >= 0xe0 && *p <= 0xef ? 3
              : *p >= 0xf0 && *p <= 0xf4 ? 4
                                         : 0;
      int valid = n != 0;
      for (int j = 1; j < n; j++) {
        if (!p[j] || (p[j] & 0xc0) != 0x80) {
          valid = 0;
          break;
        }
      }
      if (valid && ((n == 3 && ((*p == 0xe0 && p[1] < 0xa0) ||
                                (*p == 0xed && p[1] >= 0xa0))) ||
                    (n == 4 && ((*p == 0xf0 && p[1] < 0x90) ||
                                (*p == 0xf4 && p[1] >= 0x90)))))
        valid = 0;
      if (valid) {
        fwrite(p, 1, (size_t)n, fp);
        p += n - 1;
      } else
        fputs("\\ufffd", fp);
    } else
      fputc(*p, fp);
  }
  fputc('"', fp);
}
int factory_save(struct factory *f, const char *path) {
  char temp[1024];
  if (snprintf(temp, sizeof(temp), "%s.tmp-XXXXXX", path) >= (int)sizeof(temp))
    return -1;
  int fd = mkstemp(temp);
  if (fd < 0)
    return -1;
  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    close(fd);
    unlink(temp);
    return -1;
  }
  int pass = 0, fail = 0, skip = 0;
  for (int i = 0; i < TEST_COUNT; i++) {
    if (!factory_test_enabled(i))
      continue;
    pass += f->tests[i].state == PASS;
    fail += f->tests[i].state == FAIL;
    skip += f->tests[i].state == SKIP;
  }
  char host[128] = "", stamp[64];
  gethostname(host, sizeof(host) - 1);
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
  fprintf(fp,
          "{\n  \"schema\": 3, \"board\": \"DshanPI A1 CM5 Base V1.2\", "
          "\"session\": %lu,\n  \"timestamp_utc\": ",
          f->session_id);
  json_string(fp, stamp);
  fprintf(fp, ", \"hostname\": ");
  json_string(fp, host);
  fprintf(fp,
          ",\n  \"overall\": \"%s\", \"pass\": %d, \"fail\": %d, \"skip\": "
          "%d,\n  \"pcie1_mode\": %s,\n  \"tests\": [\n",
          fail                        ? "FAIL"
          : pass == ACTIVE_TEST_COUNT ? "PASS"
                                      : "INCOMPLETE",
          pass, fail, skip, f->config.pcie1_mode ? "true" : "false");
  for (int index = 0; index < ACTIVE_TEST_COUNT; index++) {
    int i = factory_active_tests[index];
    struct test_item *t = &f->tests[i];
    fprintf(fp, "    {\"id\": ");
    json_string(fp, t->key);
    fprintf(fp, ", \"name\": ");
    json_string(fp, t->name);
    fprintf(fp, ", \"state\": ");
    json_string(fp, factory_state_name(t->state));
    fprintf(fp,
            ", \"detected\": %s, \"operation_completed\": %s, \"evidence\": ",
            t->detected ? "true" : "false", t->checked ? "true" : "false");
    json_string(fp, t->evidence);
    fprintf(fp, ", \"automatic_verdict\": %s, \"auto_scope\": ",
            t->automatic ? "true" : "false");
    json_string(fp, factory_auto_scope(i));
    fprintf(fp, "}%s\n", index == ACTIVE_TEST_COUNT - 1 ? "" : ",");
  }
  fprintf(fp, "  ],\n  \"inventory\": ");
  json_string(fp, f->inventory);
  fprintf(fp, "\n}\n");
  int rc = fflush(fp);
  if (!rc)
    rc = fsync(fd);
  if (fclose(fp))
    rc = -1;
  if (!rc)
    rc = rename(temp, path);
  if (rc)
    unlink(temp);
  return rc;
}
