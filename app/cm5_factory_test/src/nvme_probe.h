#ifndef CM5_NVME_PROBE_H
#define CM5_NVME_PROBE_H
#include <sys/types.h>
struct nvme_probe {
  char device[128];
  char evidence[512];
  dev_t rdev;
};
/* Read-only sysfs discovery. Empty/auto selection follows the physical slot. */
int nvme_probe_slot(const char *root, int slot, const char *selection,
                    struct nvme_probe *out);
#endif
