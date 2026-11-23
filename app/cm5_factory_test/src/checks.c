#include "checks.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned le16(const unsigned char *b) {
  return b[0] | (unsigned)b[1] << 8;
}
static uint32_t le32(const unsigned char *b) {
  return le16(b) | (uint32_t)le16(b + 2) << 16;
}
/* Capture completion only: silent and clipped samples are valid PCM data.
 * Validate the entire RIFF container so short/truncated recordings cannot pass. */
int check_pcm_recording(const char *path, struct recording_info *info) {
  memset(info, 0, sizeof(*info));
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  int result = -1, format_ok = 0, data_ok = 0;
  unsigned char h[12], c[8], fmt[16], buffer[8192];
  if (fseek(f, 0, SEEK_END))
    goto out;
  long length = ftell(f);
  if (length < 44 || length > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET) ||
      fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) ||
      memcmp(h + 8, "WAVE", 4) || (uint64_t)le32(h + 4) + 8 != (uint64_t)length)
    goto out;
  while (ftell(f) < length) {
    if (length - ftell(f) < 8 || fread(c, 1, 8, f) != 8)
      goto out;
    uint32_t n = le32(c + 4);
    uint64_t padded = (uint64_t)n + (n & 1);
    if (padded > (uint64_t)(length - ftell(f)))
      goto out;
    if (!memcmp(c, "fmt ", 4)) {
      if (format_ok || n < 16 || fread(fmt, 1, 16, f) != 16 ||
          le16(fmt) != 1 || le16(fmt + 2) != 2 || le32(fmt + 4) != 48000 ||
          le32(fmt + 8) != 192000 || le16(fmt + 12) != 4 ||
          le16(fmt + 14) != 16 || fseek(f, (long)(padded - 16), SEEK_CUR))
        goto out;
      format_ok = 1;
    } else if (!memcmp(c, "data", 4)) {
      if (!format_ok || data_ok || n != 48000 * 2 * 2 * 5)
        goto out;
      for (uint32_t remaining = n; remaining;) {
        size_t take = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (fread(buffer, 1, take, f) != take)
          goto out;
        remaining -= (uint32_t)take;
      }
      data_ok = 1;
    } else if (fseek(f, (long)padded, SEEK_CUR))
      goto out;
  }
  if (format_ok && data_ok && !ferror(f)) {
    info->rate = 48000;
    info->channels = 2;
    info->bits = 16;
    info->frames = 48000 * 5;
    info->bytes = info->frames * 4;
    result = 0;
  }
out:
  fclose(f);
  return result;
}
int check_rgb_frame(const uint32_t *p, int w, int h, int stride,
                    struct signal_stats *s) {
  memset(s, 0, sizeof(*s));
  if (!p || w < 16 || h < 16 || w > 8192 || h > 8192 || stride < w * 4 ||
      stride % 4)
    return -1;
  double sum = 0, sum2 = 0;
  int low = 255, high = 0;
  for (int y = 0; y < h; y += (h / 32 ? h / 32 : 1))
    for (int x = 0; x < w; x += (w / 32 ? w / 32 : 1)) {
      uint32_t v = p[y * (stride / 4) + x];
      int l =
          (77 * ((v >> 16) & 255) + 150 * ((v >> 8) & 255) + 29 * (v & 255)) >>
          8;
      sum += l;
      sum2 += l * l;
      if (l < low)
        low = l;
      if (l > high)
        high = l;
      s->samples++;
    }
  s->mean = sum / s->samples;
  s->deviation = sqrt(fmax(0, sum2 / s->samples - s->mean * s->mean));
  s->range = high - low;
  return s->range >= 8 && s->deviation >= 2 ? 0 : -1;
}
