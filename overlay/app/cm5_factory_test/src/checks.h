#ifndef CM5_CHECKS_H
#define CM5_CHECKS_H
#include <stddef.h>
#include <stdint.h>
struct signal_stats {
  double mean, deviation, clipped;
  unsigned long samples;
  int range;
};
struct recording_info {
  unsigned rate, channels, bits, frames, bytes;
};
int check_pcm_recording(const char *path, struct recording_info *info);
int check_rgb_frame(const uint32_t *pixels, int width, int height, int stride,
                    struct signal_stats *stats);
#endif
