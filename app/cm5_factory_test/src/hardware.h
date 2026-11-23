#ifndef CM5_VIDEO_H
#define CM5_VIDEO_H
#include <stddef.h>
#include <stdint.h>
struct video_capture;
struct video_capture *video_capture_create(void);
void video_capture_destroy(struct video_capture *capture);
int video_capture_start(struct video_capture *capture, const char *device);
void video_capture_stop(struct video_capture *capture);
int video_capture_fd(const struct video_capture *capture);
int video_capture_read_frame(struct video_capture *capture);
const uint32_t *video_capture_pixels(const struct video_capture *capture,
                                     int *width, int *height, int *stride);
const char *video_capture_error(const struct video_capture *capture);
const char *video_capture_format(const struct video_capture *capture);
#endif
