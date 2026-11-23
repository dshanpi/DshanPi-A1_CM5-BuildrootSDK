#define _GNU_SOURCE
#include "nvme_probe.h"
#include <ctype.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>

static int attr(const char *base, const char *key, char *buf, size_t n) {
  char *path = NULL;
  if (asprintf(&path, "%s/%s", base, key) < 0)
    return -1;
  FILE *fp = fopen(path, "r");
  free(path);
  if (!fp)
    return -1;
  size_t used = fread(buf, 1, n - 1, fp);
  fclose(fp);
  while (used && isspace((unsigned char)buf[used - 1]))
    used--;
  buf[used] = 0;
  return 0;
}
static int below(const char *path, const char *base) {
  size_t n = strlen(base);
  return !strncmp(path, base, n) && path[n] == '/';
}
static int namespace_name(const char *name) {
  if (strncmp(name, "nvme", 4))
    return 0;
  const char *p = name + 4;
  if (!isdigit((unsigned char)*p))
    return 0;
  while (isdigit((unsigned char)*p))
    p++;
  if (*p++ != 'n' || !isdigit((unsigned char)*p))
    return 0;
  while (isdigit((unsigned char)*p))
    p++;
  return !*p;
}
int nvme_probe_slot(const char *root, int slot, const char *selection,
                    struct nvme_probe *out) {
  memset(out, 0, sizeof(*out));
  snprintf(out->evidence, sizeof(out->evidence),
           "PCIe%d：未发现驱动就绪的NVMe整盘；检查SSD和启动配置", slot);
  if (slot < 0 || slot > 1)
    return -1;
  int explicit = selection && *selection && strcmp(selection, "auto");
  if (explicit && (strncmp(selection, "/dev/", 5) ||
                   !namespace_name(selection + 5))) {
    snprintf(out->evidence, sizeof(out->evidence), "必须选择NVMe整盘，不能使用分区或其他存储设备");
    return -1;
  }
  char *pattern = NULL, *controller_path = NULL;
  char sys[PATH_MAX], controller[PATH_MAX];
  if (asprintf(&pattern, "%s/sys/class/block/nvme*n*", root) < 0)
    return -1;
  if (asprintf(&controller_path, "%s/sys/devices/platform/%s.pcie", root,
               slot ? "2a210000" : "2a200000") < 0) {
    free(pattern);
    return -1;
  }
  int valid = realpath(controller_path, controller) != NULL;
  snprintf(sys, sizeof(sys), "%s/sys", root);
  char resolved_sys[PATH_MAX];
  valid = valid && realpath(sys, resolved_sys) && below(controller, resolved_sys);
  free(controller_path);
  glob_t g = {0};
  int count = 0;
  if (valid && !glob(pattern, 0, NULL, &g)) {
    for (size_t i = 0; i < g.gl_pathc; i++) {
      const char *name = strrchr(g.gl_pathv[i], '/') + 1;
      if (!namespace_name(name) || (explicit && strcmp(name, selection + 5)))
        continue;
      char block[PATH_MAX], nvme[PATH_MAX], endpoint[PATH_MAX], driver[PATH_MAX];
      char *path = NULL;
      if (!realpath(g.gl_pathv[i], block) || !below(block, controller))
        continue;
      if (asprintf(&path, "%s/device", block) < 0)
        continue;
      valid = realpath(path, nvme) && below(nvme, controller);
      free(path);
      if (!valid)
        continue;
      if (asprintf(&path, "%s/device", nvme) < 0)
        continue;
      valid = realpath(path, endpoint) && below(endpoint, controller);
      free(path);
      if (!valid)
        continue;
      char transport[32], state[32], cls[32], width[32], speed[64], size[64], dev[64];
      if (attr(nvme, "transport", transport, sizeof(transport)) || strcmp(transport, "pcie") ||
          attr(nvme, "state", state, sizeof(state)) || strcmp(state, "live") ||
          attr(endpoint, "class", cls, sizeof(cls)) || strcmp(cls, "0x010802") ||
          attr(endpoint, "current_link_width", width, sizeof(width)) || atoi(width) <= 0 ||
          attr(endpoint, "current_link_speed", speed, sizeof(speed)) || atof(speed) <= 0 ||
          attr(block, "size", size, sizeof(size)) || strtoull(size, NULL, 10) < 2048 ||
          attr(block, "dev", dev, sizeof(dev)))
        continue;
      if (asprintf(&path, "%s/driver", endpoint) < 0)
        continue;
      valid = realpath(path, driver) && below(driver, resolved_sys) &&
              !strcmp(strrchr(driver, '/') + 1, "nvme");
      free(path);
      unsigned maj, min;
      char tail;
      if (!valid || sscanf(dev, "%u:%u%c", &maj, &min, &tail) != 2)
        continue;
      char model[96] = "?", serial[64] = "?";
      attr(nvme, "model", model, sizeof(model));
      attr(nvme, "serial", serial, sizeof(serial));
      snprintf(out->device, sizeof(out->device), "/dev/%s", name);
      out->rdev = makedev(maj, min);
      snprintf(out->evidence, sizeof(out->evidence),
               "PCIe%d %s | %s | SN:%s | %s x%s | %s；待只读1MiB",
               slot, strrchr(endpoint, '/') + 1, model, serial, speed, width, out->device);
      count++;
    }
  }
  globfree(&g);
  free(pattern);
  if (count == 1)
    return 0;
  out->device[0] = 0;
  if (count > 1)
    snprintf(out->evidence, sizeof(out->evidence), "PCIe%d存在多个NVMe命名空间，请配置目标整盘", slot);
  return -1;
}
