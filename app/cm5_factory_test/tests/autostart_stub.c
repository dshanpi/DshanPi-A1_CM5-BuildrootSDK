#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }
int main(int argc, char **argv) {
  const char *trace = getenv("CM5_STUB_TRACE");
  FILE *fp = fopen(trace, "a");
  if (!fp) return 2;
  fputs("started\n", fp);
  fclose(fp);
  const char *report = NULL;
  for (int i = 0; i < argc; i++) {
    printf("arg: %s\n", argv[i]);
    if (!strcmp(argv[i], "--report") && i + 1 < argc) report = argv[i + 1];
  }
  printf("XDG_RUNTIME_DIR=%s\n", getenv("XDG_RUNTIME_DIR"));
  fflush(stdout);
  signal(SIGTERM, stop);
  while (!stopped) usleep(10000);
  if (report) {
    fp = fopen(report, "w");
    if (!fp) return 3;
    fputs("{\"stub_stopped_cleanly\":true}\n", fp);
    fclose(fp);
  }
  return 0;
}
