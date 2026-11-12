/* Exercise the actual converter, including non-zero offsets and padded rows. */
#define ioctl test_ioctl
#include "../src/hardware.c"
#undef ioctl
#include <stdarg.h>
static int queued;
int test_ioctl(int fd, unsigned long request, ...) {
  (void)fd;
  va_list args;
  va_start(args, request);
  struct v4l2_buffer *b = va_arg(args, struct v4l2_buffer *);
  va_end(args);
  if (request == VIDIOC_DQBUF) {
    b->index = 0;
    b->flags = V4L2_BUF_FLAG_ERROR;
    return 0;
  }
  if (request == VIDIOC_QBUF) {
    queued++;
    return 0;
  }
  errno = EINVAL;
  return -1;
}
#include <assert.h>
int main(void) {
  struct video_capture c = {0};
  c.source_width = c.source_height = 2;
  c.preview_width = c.preview_height = 2;
  c.pixel_format = V4L2_PIX_FMT_NV12;
  c.bytes_per_line[0] = 4; /* Padded Y and UV rows have equal stride. */
  uint8_t frame[] = {81, 81, 0, 0, 81, 81, 0, 0, 90, 240, 0, 0};
  uint32_t pixels[4];
  c.pixels = pixels;
  c.buffers[0].planes[0] = frame;
  c.buffers[0].lengths[0] = sizeof(frame);
  c.buffers[0].plane_count = 1;
  c.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  struct v4l2_buffer b = {.index = 0, .bytesused = sizeof(frame)};
  struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
  assert(convert_frame(&c, &b, planes) == 0);
  for (int i = 0; i < 4; i++)
    assert(pixels[i] == yuv_to_argb(81, 90, 240));
  b.bytesused = 8;
  assert(convert_frame(&c, &b, planes) < 0); /* Truncated UV plane. */
  b.bytesused = sizeof(frame);
  c.bytes_per_line[0] = 1;
  assert(convert_frame(&c, &b, planes) < 0); /* Invalid row stride. */
  c.bytes_per_line[0] = 4;
  uint8_t offset_frame[16] = {0};
  memcpy(offset_frame + 4, frame, sizeof(frame));
  c.buffers[0].planes[0] = offset_frame;
  c.buffers[0].lengths[0] = sizeof(offset_frame);
  c.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  planes[0].data_offset = 4;
  planes[0].bytesused = 16;
  assert(convert_frame(&c, &b, planes) == 0);
  assert(pixels[3] == yuv_to_argb(81, 90, 240));
  planes[0].data_offset = 17;
  assert(convert_frame(&c, &b, planes) < 0);
  c.fd = 123;
  c.buffer_count = 1;
  pixels[0] = 0x12345678;
  assert(video_capture_read_frame(&c) == 0);
  assert(queued == 1 && pixels[0] == 0x12345678);
  puts("video: padded NV12, data offsets and malformed buffers passed");
  return 0;
}
