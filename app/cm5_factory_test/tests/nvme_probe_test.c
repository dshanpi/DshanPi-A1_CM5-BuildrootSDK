#include "nvme_probe.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  struct nvme_probe probe;
  int rc = nvme_probe_slot(argv[1], atoi(argv[2]), argv[3], &probe);
  printf("%s\n%s\n", probe.device, probe.evidence);
  return rc ? 1 : 0;
}
