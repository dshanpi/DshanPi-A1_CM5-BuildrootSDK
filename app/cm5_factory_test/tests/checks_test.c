#include "checks.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void word(FILE *f, unsigned v, int bytes) {
  for (int i = 0; i < bytes; i++)
    fputc((v >> (i * 8)) & 255, f);
}
static void wave(const char *path, int mode) {
  FILE *f = fopen(path, "wb");
  assert(f);
  unsigned n = mode == 4 ? 100 : mode == 5 ? 48000 * 4 * 4 : 48000 * 5 * 4;
  fwrite("RIFF", 1, 4, f);
  word(f, n + 36, 4);
  fwrite("WAVEfmt ", 1, 8, f);
  word(f, 16, 4);
  word(f, 1, 2);
  word(f, 2, 2);
  word(f, 48000, 4);
  word(f, 192000, 4);
  word(f, 4, 2);
  word(f, 16, 2);
  fwrite("data", 1, 4, f);
  word(f, n, 4);
  for (unsigned i = 0; i < n / 4; i++) {
    int v = mode == 0   ? 0
            : mode == 1 ? 1200
            : (mode == 2 || mode == 5 || mode == 6)
                ? (i % 120 < 60 ? 2000 : -2000)
            : mode == 3 ? (i % 120 < 60 ? 32767 : -32768)
                        : 100;
    if (mode == 6 && i >= 9600)
      v = 0; /* Capture-only verdict accepts this too. */
    word(f, (unsigned short)v, 2);
    word(f, (unsigned short)(mode == 1 ? -v : v), 2);
  }
  fclose(f);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const char *p = argv[1];
  struct signal_stats s;
  struct recording_info info;
  for (int mode = 0; mode < 7; mode++) {
    wave(p, mode);
    int rc = check_pcm_recording(p, &info);
    assert((rc == 0) == (mode != 4 && mode != 5));
    if (!rc)
      assert(info.frames == 240000 && info.bytes == 960000 &&
             info.rate == 48000 && info.channels == 2 && info.bits == 16);
  }
  FILE *f = fopen(p, "wb");
  fwrite("RIFF", 1, 4, f);
  fclose(f);
  assert(check_pcm_recording(p, &info));
  /* Full-length silence is valid; malformed PCM/container metadata is not. */
  const unsigned offsets[] = {4, 20, 22, 24, 28, 32, 34, 40};
  for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    wave(p, 0);
    f = fopen(p, "r+b");
    assert(f && !fseek(f, offsets[i], SEEK_SET));
    word(f, 3, 2);
    fclose(f);
    assert(check_pcm_recording(p, &info));
  }
  wave(p, 0);
  assert(!truncate(p, 44 + 100));
  assert(check_pcm_recording(p, &info));
  remove(p);
  assert(check_pcm_recording(p, &info));
  uint32_t pixels[64 * 64];
  memset(pixels, 0, sizeof(pixels));
  assert(check_rgb_frame(pixels, 64, 64, 256, &s));
  for (int i = 0; i < 64 * 64; i++)
    pixels[i] = 0xffffff;
  assert(check_rgb_frame(pixels, 64, 64, 256, &s));
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++)
      pixels[y * 64 + x] = ((x / 8 + y / 8) % 2) ? 0xffffff : 0;
  assert(!check_rgb_frame(pixels, 64, 64, 256, &s));
  assert(s.range == 255);
  assert(check_rgb_frame(pixels, 64, 64, 252, &s));
  assert(check_rgb_frame(NULL, 64, 64, 256, &s));
  check_rgb_frame(pixels, 16, 17, 256, &s);
  assert(s.samples == 16 * 17);
  remove(p);
  puts("capture checks: silence/DC/clipping accepted; short/truncated/malformed WAV rejected; blank/valid "
       "RGB passed");
  return 0;
}
