#ifndef CM5_FACTORY_H
#define CM5_FACTORY_H
#include <stddef.h>
#include <sys/types.h>

enum test_id {
  T_SPEAKER,
  T_HEADPHONE,
  T_BOARD_MIC,
  T_HEADSET_MIC,
  T_ETH0,
  T_ETH1,
  T_USB1,
  T_USB2,
  T_TYPEC,
  T_TF,
  T_M2_0,
  T_M2_1,
  T_HDMI,
  T_DSI,
  T_TOUCH,
  T_CSI1,
  T_CSI2,
  T_CSI3,
  T_RTC,
  T_KEY,
  T_LED,
  T_FAN,
  T_UART,
  T_POWER,
  T_WIFI,
  TEST_COUNT
};
#define ACTIVE_TEST_COUNT 15
extern const int factory_active_tests[ACTIVE_TEST_COUNT];
int factory_test_enabled(int id);
enum test_state { UNKNOWN, READY, RUNNING, PASS, FAIL, SKIP };
struct test_item {
  const char *key, *name, *instruction;
  int page;
  enum test_state state;
  int detected, require_detection, checked, automatic, auto_waiting;
  char evidence[1024];
};
struct factory_config {
  char audio_card[64], audio_pcm[128];
  char usb1[128], usb2[128], typec[128], tf_device[128], m2_0_device[128],
      m2_1_device[128];
  char camera[3][128], rtc[128], led[256], fan[256], uart[128];
  int pcie1_mode;
  char network_peer[2][64];
  char root[256]; /* Read-only diagnostic fixtures; empty on a real board. */
};
struct factory {
  struct factory_config config;
  char ethernet[2][64];
  struct test_item tests[TEST_COUNT];
  char inventory[4096], message[512], log_path[256], run_dir[256];
  pid_t job_pid;
  int job_test, job_automatic, auto_running, auto_next;
  int monitoring, tf_auto;
  unsigned int pending_tests;
  unsigned int revision;
  char artifact_dir[512];
  double job_started;
  double wifi_retry_at;
  unsigned long session_id;
};
void factory_init(struct factory *f);
int factory_load_config(struct factory *f, const char *path);
void factory_scan(struct factory *f);
int factory_start(struct factory *f, int id);
int factory_retry(struct factory *f, int id);
void factory_poll(struct factory *f);
int factory_auto_begin(struct factory *f, const char *report_path);
void factory_auto_cancel(struct factory *f);
const char *factory_auto_scope(int id);
void factory_cancel(struct factory *f);
void factory_destroy(struct factory *f);
int factory_decide(struct factory *f, int id, enum test_state state);
int factory_can_pass(struct factory *f, int id);
int factory_save(struct factory *f, const char *path);
const char *factory_state_name(enum test_state state);
const char *factory_camera(struct factory *f, int id);
#endif
