#include "factory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void put(struct factory *f, const char *path, const char *value) {
  char p[1024];
  snprintf(p, sizeof(p), "%s%s", f->config.root, path);
  FILE *fp = fopen(p, "w");
  assert(fp);
  fputs(value, fp);
  fclose(fp);
}
int main(int argc, char **argv) {
  assert(argc == 4);
  struct factory f;
  factory_init(&f);
  snprintf(f.config.root, sizeof(f.config.root), "%s", argv[1]);
  assert(!factory_load_config(&f, argv[2]));
  factory_scan(&f);
  assert(f.tests[T_SPEAKER].detected && f.tests[T_TF].detected);
  assert(f.tests[T_ETH0].detected && !f.tests[T_ETH1].detected); /* no IPv4 */
  assert(f.tests[T_USB1].detected &&
         !f.tests[T_USB2].detected); /* missing type */
  assert(!factory_test_enabled(T_RTC) && !f.tests[T_RTC].detected);
  assert(factory_auto_begin(&f, argv[3]) < 0 &&
         factory_start(&f, T_SPEAKER) < 0);
  for (int i = 0; i < TEST_COUNT; i++)
    if (!factory_test_enabled(i)) {
      assert(factory_start(&f, i) < 0 && factory_decide(&f, i, PASS) < 0);
    }
  assert(factory_decide(&f, T_USB1, PASS) < 0 &&
         factory_decide(&f, T_TOUCH, PASS) < 0);
  f.monitoring = 1;
  factory_scan(&f);
  assert(f.tests[T_ETH0].state == PASS && f.tests[T_USB1].state == PASS);
  assert(strstr(f.tests[T_USB1].evidence, "存储设备"));
  assert(f.tests[T_USB2].state != PASS && f.tests[T_HDMI].state != PASS);
  assert(f.tests[T_TYPEC].state == PASS &&
         strstr(f.tests[T_TYPEC].evidence, "ADB"));
  assert(factory_retry(&f, T_TYPEC) < 0 && factory_start(&f, T_USB1) < 0);
  assert(factory_decide(&f, T_USB1, FAIL) < 0 &&
         factory_decide(&f, T_USB1, SKIP) < 0);
  assert(f.tests[T_USB1].state == PASS);
  char function_link[1024];
  snprintf(function_link, sizeof(function_link),
           "%s/sys/kernel/config/usb_gadget/test/configs/b.1/adb", argv[1]);
  assert(!unlink(function_link));
  f.tests[T_TYPEC].state = UNKNOWN;
  f.tests[T_TYPEC].checked = 0;
  factory_scan(&f);
  assert(f.tests[T_TYPEC].state != PASS && !f.tests[T_TYPEC].detected);
  /* restore this round's already verified record after negative fixture case */
  f.tests[T_TYPEC].state = PASS;
  f.tests[T_TYPEC].checked = 1;

  /* Insertion after the initial finite queue, without any IP/peer or block
   * node. */
  unsigned revision = f.revision;
  put(&f, "/sys/class/net/eth1/carrier", "1\n");
  put(&f, "/sys/bus/usb/devices/2-1.2/bDeviceClass", "03\n");
  put(&f, "/sys/class/drm/card0-HDMI-A-1/status", "connected\n");
  factory_scan(&f);
  assert(f.revision > revision && f.tests[T_ETH1].state == PASS);
  assert(f.tests[T_USB2].state == PASS &&
         strstr(f.tests[T_USB2].evidence, "HID"));
  assert(f.tests[T_HDMI].state == PASS);
  /* Moving one fixture does not erase evidence from this round. */
  char path[1024];
  snprintf(path, sizeof(path), "%s/sys/bus/usb/devices/2-1.1/idVendor",
           argv[1]);
  assert(!unlink(path));
  put(&f, "/sys/class/net/eth0/carrier", "0\n");
  put(&f, "/sys/class/drm/card0-HDMI-A-1/status", "disconnected\n");
  factory_scan(&f);
  assert(!f.tests[T_USB1].detected && f.tests[T_USB1].state == PASS &&
         strstr(f.tests[T_USB1].evidence, "存储"));
  assert(!f.tests[T_ETH0].detected && f.tests[T_ETH0].state == PASS);
  assert(!f.tests[T_HDMI].detected && f.tests[T_HDMI].state == PASS);
  /* TF removal/reinsert is rediscovered and queued, not stuck on the old
   * device. */
  f.tests[T_TF].state = UNKNOWN;
  put(&f, "/sys/block/mmcblk1/device/type", "MMC\n");
  factory_scan(&f);
  assert(!f.tests[T_TF].detected);
  put(&f, "/sys/block/mmcblk1/device/type", "SD\n");
  factory_scan(&f);
  assert(f.tests[T_TF].detected && (f.pending_tests & (1u << T_TF)));
  factory_poll(&f);
  assert(!f.job_pid); /* fixtures remain read-only, even pending */
  f.tests[T_SPEAKER].checked = 1;
  strcpy(f.tests[T_SPEAKER].evidence, "quoted \"value\"\n真实操作证据\\");
  assert(!factory_decide(&f, T_SPEAKER, PASS));
  factory_scan(&f);
  assert(strstr(f.tests[T_SPEAKER].evidence, "真实操作证据"));
  assert(factory_decide(&f, T_M2_1, SKIP) < 0);
  strcpy(f.tests[T_CSI3].evidence, "Invalid byte: \xff / truncated: \xe4\xb8");
  assert(!factory_save(&f, argv[3]));
  factory_destroy(&f);
  puts("backend: Link without IP, USB storage/HID class, mid-run insertion, "
       "latched passes, TF rediscovery, disabled tests, fixture isolation "
       "passed");
}
