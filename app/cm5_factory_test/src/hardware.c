#define _GNU_SOURCE

#include "hardware.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/videodev2.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define VIDEO_BUFFER_COUNT 4
#define PREVIEW_MAX_WIDTH 640
#define PREVIEW_MAX_HEIGHT 480

struct mapped_buffer {
  void *planes[VIDEO_MAX_PLANES];
  size_t lengths[VIDEO_MAX_PLANES];
  unsigned int plane_count;
};

struct video_capture {
  int fd;
  enum v4l2_buf_type type;
  uint32_t pixel_format;
  unsigned int source_width;
  unsigned int source_height;
  unsigned int bytes_per_line[VIDEO_MAX_PLANES];
  unsigned int plane_count;
  struct mapped_buffer buffers[VIDEO_BUFFER_COUNT];
  unsigned int buffer_count;
  uint32_t *pixels;
  int preview_width;
  int preview_height;
  int preview_stride;
  char error[192];
  char format[64];
};

static int xioctl(int fd, unsigned long request, void *arg) {
  int result;

  do {
    result = ioctl(fd, request, arg);
  } while (result < 0 && errno == EINTR);
  return result;
}

struct video_capture *video_capture_create(void) {
  struct video_capture *capture = calloc(1, sizeof(*capture));
  if (capture)
    capture->fd = -1;
  return capture;
}

static void video_set_error(struct video_capture *capture, const char *action) {
  snprintf(capture->error, sizeof(capture->error), "%s：%s", action,
           strerror(errno));
}

static int format_supported(uint32_t format) {
  return format == V4L2_PIX_FMT_NV12 || format == V4L2_PIX_FMT_NV12M ||
         format == V4L2_PIX_FMT_NV21 || format == V4L2_PIX_FMT_NV21M ||
         format == V4L2_PIX_FMT_YUYV || format == V4L2_PIX_FMT_UYVY ||
         format == V4L2_PIX_FMT_RGB24 || format == V4L2_PIX_FMT_BGR24;
}

static uint32_t choose_format(struct video_capture *capture) {
  static const uint32_t preferred[] = {
      V4L2_PIX_FMT_NV12,  V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_YUYV,
      V4L2_PIX_FMT_UYVY,  V4L2_PIX_FMT_NV21,  V4L2_PIX_FMT_NV21M,
      V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR24,
  };
  uint32_t available[32];
  unsigned int count = 0;
  unsigned int index;

  for (index = 0; index < ARRAY_SIZE(available); ++index) {
    struct v4l2_fmtdesc description;
    memset(&description, 0, sizeof(description));
    description.type = capture->type;
    description.index = index;
    if (xioctl(capture->fd, VIDIOC_ENUM_FMT, &description) < 0)
      break;
    available[count++] = description.pixelformat;
  }
  for (index = 0; index < ARRAY_SIZE(preferred); ++index) {
    unsigned int i;
    for (i = 0; i < count; ++i) {
      if (available[i] == preferred[index])
        return preferred[index];
    }
  }
  return count ? available[0] : V4L2_PIX_FMT_NV12;
}

static void fourcc_text(uint32_t fourcc, char text[5]) {
  text[0] = fourcc & 0xff;
  text[1] = (fourcc >> 8) & 0xff;
  text[2] = (fourcc >> 16) & 0xff;
  text[3] = (fourcc >> 24) & 0xff;
  text[4] = '\0';
}

static int configure_video(struct video_capture *capture) {
  struct v4l2_format format;
  uint32_t requested = choose_format(capture);
  unsigned int input_width = 0;
  unsigned int input_height = 0;
  char fourcc[5];
  unsigned int i;

  /* Keep the ISP's negotiated sensor output size; never assume an HDMI timing.
   */
  memset(&format, 0, sizeof(format));
  format.type = capture->type;
  if (xioctl(capture->fd, VIDIOC_G_FMT, &format) == 0) {
    if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      input_width = format.fmt.pix_mp.width;
      input_height = format.fmt.pix_mp.height;
    } else {
      input_width = format.fmt.pix.width;
      input_height = format.fmt.pix.height;
    }
  }
  if (input_width < 16 || input_height < 16 || input_width > 8192 ||
      input_height > 8192) {
    snprintf(capture->error, sizeof(capture->error),
             "ISP输出尺寸无效，请先配置media链路");
    return -1;
  }

  memset(&format, 0, sizeof(format));
  format.type = capture->type;
  if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
    format.fmt.pix_mp.width = input_width;
    format.fmt.pix_mp.height = input_height;
    format.fmt.pix_mp.pixelformat = requested;
    format.fmt.pix_mp.field = V4L2_FIELD_ANY;
  } else {
    format.fmt.pix.width = input_width;
    format.fmt.pix.height = input_height;
    format.fmt.pix.pixelformat = requested;
    format.fmt.pix.field = V4L2_FIELD_ANY;
  }
  if (xioctl(capture->fd, VIDIOC_S_FMT, &format) < 0) {
    video_set_error(capture, "设置 摄像头 格式失败");
    return -1;
  }

  if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
    capture->source_width = format.fmt.pix_mp.width;
    capture->source_height = format.fmt.pix_mp.height;
    capture->pixel_format = format.fmt.pix_mp.pixelformat;
    capture->plane_count = format.fmt.pix_mp.num_planes;
    for (i = 0; i < capture->plane_count && i < VIDEO_MAX_PLANES; ++i)
      capture->bytes_per_line[i] = format.fmt.pix_mp.plane_fmt[i].bytesperline;
  } else {
    capture->source_width = format.fmt.pix.width;
    capture->source_height = format.fmt.pix.height;
    capture->pixel_format = format.fmt.pix.pixelformat;
    capture->plane_count = 1;
    capture->bytes_per_line[0] = format.fmt.pix.bytesperline;
  }
  fourcc_text(capture->pixel_format, fourcc);
  if (!capture->source_width || !capture->source_height ||
      capture->source_width > 8192 || capture->source_height > 8192 ||
      !capture->plane_count || capture->plane_count > VIDEO_MAX_PLANES) {
    snprintf(capture->error, sizeof(capture->error),
             "驱动返回无效的分辨率或平面数量");
    return -1;
  }
  snprintf(capture->format, sizeof(capture->format), "%ux%u · %s",
           capture->source_width, capture->source_height, fourcc);
  if (!format_supported(capture->pixel_format)) {
    snprintf(capture->error, sizeof(capture->error),
             "摄像头 输出格式 %s 暂不支持预览", fourcc);
    return -1;
  }
  return 0;
}

static int map_video_buffers(struct video_capture *capture) {
  struct v4l2_requestbuffers request;
  unsigned int i;

  memset(&request, 0, sizeof(request));
  request.count = VIDEO_BUFFER_COUNT;
  request.type = capture->type;
  request.memory = V4L2_MEMORY_MMAP;
  if (xioctl(capture->fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
    video_set_error(capture, "申请 摄像头 缓冲区失败");
    return -1;
  }
  capture->buffer_count =
      request.count > VIDEO_BUFFER_COUNT ? VIDEO_BUFFER_COUNT : request.count;
  for (i = 0; i < capture->buffer_count; ++i) {
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    unsigned int p;

    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));
    buffer.type = capture->type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      buffer.m.planes = planes;
      buffer.length = VIDEO_MAX_PLANES;
    }
    if (xioctl(capture->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
      video_set_error(capture, "查询 摄像头 缓冲区失败");
      return -1;
    }
    capture->buffers[i].plane_count =
        capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? buffer.length : 1;
    if (!capture->buffers[i].plane_count ||
        capture->buffers[i].plane_count > VIDEO_MAX_PLANES) {
      capture->buffers[i].plane_count = 0;
      snprintf(capture->error, sizeof(capture->error), "视频缓冲平面数量无效");
      return -1;
    }
    for (p = 0; p < capture->buffers[i].plane_count; ++p) {
      off_t offset;
      size_t length;
      if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        offset = planes[p].m.mem_offset;
        length = planes[p].length;
      } else {
        offset = buffer.m.offset;
        length = buffer.length;
      }
      capture->buffers[i].lengths[p] = length;
      capture->buffers[i].planes[p] = mmap(NULL, length, PROT_READ | PROT_WRITE,
                                           MAP_SHARED, capture->fd, offset);
      if (capture->buffers[i].planes[p] == MAP_FAILED) {
        capture->buffers[i].planes[p] = NULL;
        video_set_error(capture, "映射 摄像头 缓冲区失败");
        return -1;
      }
    }
    if (xioctl(capture->fd, VIDIOC_QBUF, &buffer) < 0) {
      video_set_error(capture, "提交 摄像头 缓冲区失败");
      return -1;
    }
  }
  return 0;
}

int video_capture_start(struct video_capture *capture, const char *device) {
  struct v4l2_capability capability;
  enum v4l2_buf_type type;
  uint32_t capabilities;
  double aspect;

  if (!capture)
    return -1;
  video_capture_stop(capture);
  capture->error[0] = '\0';
  capture->format[0] = '\0';
  capture->fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (capture->fd < 0) {
    video_set_error(capture, "打开 摄像头 失败");
    return -1;
  }
  memset(&capability, 0, sizeof(capability));
  if (xioctl(capture->fd, VIDIOC_QUERYCAP, &capability) < 0) {
    video_set_error(capture, "读取 摄像头 能力失败");
    goto fail;
  }
  capabilities = capability.capabilities & V4L2_CAP_DEVICE_CAPS
                     ? capability.device_caps
                     : capability.capabilities;
  if (!(capabilities & V4L2_CAP_STREAMING)) {
    snprintf(capture->error, sizeof(capture->error), "%s 不支持 mmap 视频流",
             device);
    goto fail;
  }
  if (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
    capture->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  else if (capabilities & V4L2_CAP_VIDEO_CAPTURE)
    capture->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  else {
    snprintf(capture->error, sizeof(capture->error), "%s 不是视频采集节点",
             device);
    goto fail;
  }
  if (configure_video(capture) < 0 || map_video_buffers(capture) < 0)
    goto fail;

  aspect = capture->source_height
               ? (double)capture->source_width / capture->source_height
               : 16.0 / 9.0;
  capture->preview_width = PREVIEW_MAX_WIDTH;
  capture->preview_height = (int)(capture->preview_width / aspect);
  if (capture->preview_height > PREVIEW_MAX_HEIGHT) {
    capture->preview_height = PREVIEW_MAX_HEIGHT;
    capture->preview_width = (int)(capture->preview_height * aspect);
  }
  if (capture->preview_width < 1 || capture->preview_height < 1) {
    snprintf(capture->error, sizeof(capture->error), "摄像头 分辨率无效");
    goto fail;
  }
  capture->preview_stride = capture->preview_width * 4;
  capture->pixels =
      calloc((size_t)capture->preview_width * capture->preview_height,
             sizeof(uint32_t));
  if (!capture->pixels) {
    snprintf(capture->error, sizeof(capture->error), "预览内存不足");
    goto fail;
  }
  type = capture->type;
  if (xioctl(capture->fd, VIDIOC_STREAMON, &type) < 0) {
    video_set_error(capture, "启动 摄像头 失败");
    goto fail;
  }
  return 0;

fail:
  video_capture_stop(capture);
  return -1;
}

void video_capture_stop(struct video_capture *capture) {
  unsigned int i;

  if (!capture)
    return;
  if (capture->fd >= 0) {
    enum v4l2_buf_type type = capture->type;
    xioctl(capture->fd, VIDIOC_STREAMOFF, &type);
  }
  for (i = 0; i < capture->buffer_count; ++i) {
    unsigned int p;
    for (p = 0; p < capture->buffers[i].plane_count; ++p) {
      if (capture->buffers[i].planes[p])
        munmap(capture->buffers[i].planes[p], capture->buffers[i].lengths[p]);
    }
    memset(&capture->buffers[i], 0, sizeof(capture->buffers[i]));
  }
  if (capture->fd >= 0)
    close(capture->fd);
  capture->fd = -1;
  capture->buffer_count = 0;
  free(capture->pixels);
  capture->pixels = NULL;
}

static inline uint8_t clamp_byte(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (uint8_t)value;
}

static inline uint32_t yuv_to_argb(int y, int u, int v) {
  int c = y - 16;
  int d = u - 128;
  int e = v - 128;
  int r = (298 * c + 409 * e + 128) >> 8;
  int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
  int b = (298 * c + 516 * d + 128) >> 8;
  return 0xff000000u | ((uint32_t)clamp_byte(r) << 16) |
         ((uint32_t)clamp_byte(g) << 8) | clamp_byte(b);
}

static int convert_frame(struct video_capture *capture,
                         const struct v4l2_buffer *buffer,
                         const struct v4l2_plane *planes) {
  const uint8_t *plane0 = capture->buffers[buffer->index].planes[0];
  const uint8_t *plane1 = capture->buffers[buffer->index].plane_count > 1
                              ? capture->buffers[buffer->index].planes[1]
                              : NULL;
  unsigned int stride0 = capture->bytes_per_line[0];
  unsigned int stride1 = capture->bytes_per_line[1];
  int x, y;

  struct mapped_buffer *mapped = &capture->buffers[buffer->index];
  size_t available[2] = {mapped->lengths[0], mapped->lengths[1]};
  if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
    for (unsigned int p = 0; p < mapped->plane_count && p < 2; p++) {
      if (planes[p].data_offset > planes[p].bytesused ||
          planes[p].bytesused > mapped->lengths[p])
        goto invalid;
      available[p] = planes[p].bytesused - planes[p].data_offset;
    }
    plane0 += planes[0].data_offset;
    if (plane1)
      plane1 += planes[1].data_offset;
  } else {
    if (buffer->bytesused > available[0])
      goto invalid;
    available[0] = buffer->bytesused;
  }
  if (!stride0) {
    if (capture->pixel_format == V4L2_PIX_FMT_YUYV ||
        capture->pixel_format == V4L2_PIX_FMT_UYVY)
      stride0 = capture->source_width * 2;
    else if (capture->pixel_format == V4L2_PIX_FMT_RGB24 ||
             capture->pixel_format == V4L2_PIX_FMT_BGR24)
      stride0 = capture->source_width * 3;
    else
      stride0 = capture->source_width;
  }
  if (!stride1)
    stride1 = plane1 ? capture->source_width : stride0;
  unsigned int fmt = capture->pixel_format;
  int semi = fmt == V4L2_PIX_FMT_NV12 || fmt == V4L2_PIX_FMT_NV21 ||
             fmt == V4L2_PIX_FMT_NV12M || fmt == V4L2_PIX_FMT_NV21M;
  unsigned int row_bytes =
      semi ? capture->source_width
      : (fmt == V4L2_PIX_FMT_YUYV || fmt == V4L2_PIX_FMT_UYVY)
          ? capture->source_width * 2
          : capture->source_width * 3;
  if (stride0 < row_bytes || !plane0 || !capture->pixels)
    goto invalid;
  size_t luma = (size_t)stride0 * capture->source_height;
  if (available[0] < luma)
    goto invalid;
  if (semi) {
    if ((capture->source_width & 1) || (capture->source_height & 1) ||
        stride1 < capture->source_width)
      goto invalid;
    size_t chroma = (size_t)stride1 * (capture->source_height / 2);
    if (plane1) {
      if (available[1] < chroma)
        goto invalid;
    } else if (fmt == V4L2_PIX_FMT_NV12M || fmt == V4L2_PIX_FMT_NV21M ||
               available[0] - luma < chroma)
      goto invalid;
  } else if ((fmt == V4L2_PIX_FMT_YUYV || fmt == V4L2_PIX_FMT_UYVY) &&
             (capture->source_width & 1))
    goto invalid;
  if (!plane1 && (capture->pixel_format == V4L2_PIX_FMT_NV12 ||
                  capture->pixel_format == V4L2_PIX_FMT_NV21))
    plane1 = plane0 + (size_t)stride0 * capture->source_height;

  for (y = 0; y < capture->preview_height; ++y) {
    unsigned int sy = (unsigned int)((uint64_t)y * capture->source_height /
                                     capture->preview_height);
    uint32_t *destination =
        capture->pixels + (size_t)y * capture->preview_width;
    for (x = 0; x < capture->preview_width; ++x) {
      unsigned int sx = (unsigned int)((uint64_t)x * capture->source_width /
                                       capture->preview_width);
      uint32_t color = 0xff181b24;

      switch (capture->pixel_format) {
      case V4L2_PIX_FMT_NV12:
      case V4L2_PIX_FMT_NV12M:
      case V4L2_PIX_FMT_NV21:
      case V4L2_PIX_FMT_NV21M: {
        const uint8_t *uv = plane1 + (sy / 2) * stride1 + (sx & ~1u);
        int u = uv[capture->pixel_format == V4L2_PIX_FMT_NV21 ||
                   capture->pixel_format == V4L2_PIX_FMT_NV21M];
        int v = uv[capture->pixel_format == V4L2_PIX_FMT_NV12 ||
                   capture->pixel_format == V4L2_PIX_FMT_NV12M];
        color = yuv_to_argb(plane0[sy * stride0 + sx], u, v);
        break;
      }
      case V4L2_PIX_FMT_YUYV:
      case V4L2_PIX_FMT_UYVY: {
        const uint8_t *pair = plane0 + sy * stride0 + (sx & ~1u) * 2;
        int yy, u, v;
        if (capture->pixel_format == V4L2_PIX_FMT_YUYV) {
          yy = pair[(sx & 1u) ? 2 : 0];
          u = pair[1];
          v = pair[3];
        } else {
          yy = pair[(sx & 1u) ? 3 : 1];
          u = pair[0];
          v = pair[2];
        }
        color = yuv_to_argb(yy, u, v);
        break;
      }
      case V4L2_PIX_FMT_RGB24:
      case V4L2_PIX_FMT_BGR24: {
        const uint8_t *rgb = plane0 + sy * stride0 + sx * 3;
        int r = rgb[capture->pixel_format == V4L2_PIX_FMT_BGR24 ? 2 : 0];
        int b = rgb[capture->pixel_format == V4L2_PIX_FMT_BGR24 ? 0 : 2];
        color = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)rgb[1] << 8) | b;
        break;
      }
      default:
        break;
      }
      destination[x] = color;
    }
  }
  return 0;
invalid:
  snprintf(capture->error, sizeof(capture->error),
           "视频帧长度、步长或偏移无效，已拒绝转换");
  return -1;
}

int video_capture_read_frame(struct video_capture *capture) {
  struct v4l2_buffer buffer;
  struct v4l2_plane planes[VIDEO_MAX_PLANES];
  int result;

  if (!capture || capture->fd < 0)
    return -1;
  memset(&buffer, 0, sizeof(buffer));
  memset(planes, 0, sizeof(planes));
  buffer.type = capture->type;
  buffer.memory = V4L2_MEMORY_MMAP;
  if (capture->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
    buffer.m.planes = planes;
    buffer.length = VIDEO_MAX_PLANES;
  }
  result = xioctl(capture->fd, VIDIOC_DQBUF, &buffer);
  if (result < 0) {
    if (errno == EAGAIN)
      return 0;
    video_set_error(capture, "读取 摄像头 画面失败");
    return -1;
  }
  if (buffer.index >= capture->buffer_count) {
    snprintf(capture->error, sizeof(capture->error), "摄像头 缓冲区编号异常");
    return -1;
  }
  if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
    snprintf(capture->error, sizeof(capture->error),
             "驱动标记损坏的视频帧，已丢弃");
    if (xioctl(capture->fd, VIDIOC_QBUF, &buffer) < 0) {
      video_set_error(capture, "归还损坏帧缓冲区失败");
      return -1;
    }
    return 0;
  }
  if (convert_frame(capture, &buffer, planes) < 0) {
    xioctl(capture->fd, VIDIOC_QBUF, &buffer);
    return -1;
  }
  if (xioctl(capture->fd, VIDIOC_QBUF, &buffer) < 0) {
    video_set_error(capture, "归还 摄像头 缓冲区失败");
    return -1;
  }
  return 1;
}

const uint32_t *video_capture_pixels(const struct video_capture *capture,
                                     int *width, int *height, int *stride) {
  if (!capture || !capture->pixels)
    return NULL;
  if (width)
    *width = capture->preview_width;
  if (height)
    *height = capture->preview_height;
  if (stride)
    *stride = capture->preview_stride;
  return capture->pixels;
}

int video_capture_fd(const struct video_capture *capture) {
  return capture ? capture->fd : -1;
}

const char *video_capture_error(const struct video_capture *capture) {
  return capture && capture->error[0] ? capture->error : "";
}

const char *video_capture_format(const struct video_capture *capture) {
  return capture && capture->format[0] ? capture->format : "";
}

void video_capture_destroy(struct video_capture *capture) {
  if (!capture)
    return;
  video_capture_stop(capture);
  free(capture);
}
