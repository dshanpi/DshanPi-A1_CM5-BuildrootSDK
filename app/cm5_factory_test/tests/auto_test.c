#define _GNU_SOURCE
#include "factory.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static void job(struct factory *f, int id, int automatic, int delay) {
  f->job_test = id;
  f->tests[id].state = RUNNING;
  f->tests[id].automatic = automatic;
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  f->job_started = t.tv_sec + t.tv_nsec / 1e9;
  pid_t p = fork();
  assert(p >= 0);
  if (!p) {
    setpgid(0, 0);
    if (delay == 2)
      signal(SIGTERM, SIG_IGN);
    if (delay)
      sleep(5);
    _exit(0);
  }
  setpgid(p, p);
  f->job_pid = p;
}
int main(void) {
  struct factory f;
  factory_init(&f);
  char path[] = "/tmp/cm5-auto-test-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  assert(write(fd, "measurement evidence", 20) == 20);
  close(fd);
  snprintf(f.log_path, sizeof(f.log_path), "%s", path);
  job(&f, T_BOARD_MIC, 1, 0);
  while (f.job_pid) {
    factory_poll(&f);
    usleep(1000);
  }
  assert(f.tests[T_BOARD_MIC].state == PASS && f.tests[T_BOARD_MIC].checked);
  assert(strstr(f.tests[T_BOARD_MIC].evidence, "measurement evidence"));
  job(&f, T_SPEAKER, 0, 0);
  while (f.job_pid) {
    factory_poll(&f);
    usleep(1000);
  }
  assert(f.tests[T_SPEAKER].state ==
         READY); /* Playback isn't acoustic proof. */
  job(&f, T_BOARD_MIC, 1, 1);
  f.auto_running = 1;
  factory_auto_cancel(&f);
  assert(!f.auto_running && !f.job_pid && f.tests[T_BOARD_MIC].state == FAIL);
  assert(!f.tests[T_BOARD_MIC].checked);
  job(&f, T_M2_1, 0, 2);
  usleep(50000); /* Child has installed SIG_IGN. */
  unsigned before = f.revision;
  factory_cancel(&f);
  assert(f.revision > before && f.tests[T_M2_1].state == FAIL);
  assert(strstr(f.tests[T_M2_1].evidence, "强制终止"));
  f.auto_running = 1;
  f.auto_next = TEST_COUNT;
  factory_poll(&f);
  assert(!f.auto_running);
  factory_destroy(&f);
  puts("automatic lifecycle: measured pass, manual-ready, stop, completion "
       "passed");
}
