#include "camera_capture.h"
#include "frame_queue.h"
#include "stream_http_mjpeg.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define HTTP_FRAME_QUEUE_CAPACITY 4

typedef struct {
    frame_queue_t *queue;
    int client_fd;
    int sent_frames;
    int send_failed;
    unsigned long long sent_bytes;
} http_sender_args_t;

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static unsigned long long timestamp_now_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000000ULL +
           (unsigned long long)tv.tv_usec;
}

static void *http_sender_thread(void *arg)
{
    http_sender_args_t *sender = (http_sender_args_t *)arg;
    frame_t frame;
    int ret;

    while ((ret = frame_queue_pop(sender->queue, &frame)) == 1) {
        if (http_mjpeg_send_frame(sender->client_fd, frame.data, frame.size) != 0) {
            fprintf(stderr, "\nHTTP client disconnected or send failed\n");
            sender->send_failed = 1;
            frame_release(&frame);
            frame_queue_close(sender->queue);
            return NULL;
        }

        ++sender->sent_frames;
        sender->sent_bytes += frame.size;
        frame_release(&frame);
    }

    return NULL;
}

static int ensure_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "Output path exists but is not a directory: %s\n", path);
        return -1;
    }

    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory %s: %s\n", path, strerror(errno));
        return -1;
    }

    return 0;
}

static int write_frame_file(const camera_context_t *ctx,
                            const void *data,
                            unsigned int bytesused,
                            int frame_number)
{
    char path[512];
    const char *ext = ctx->config.pixel_format == V4L2_PIX_FMT_MJPEG ? "jpg" : "raw";
    FILE *fp;
    int written;

    written = snprintf(path, sizeof(path), "%s/frame_%03d.%s",
                       ctx->config.frame_dir,
                       frame_number,
                       ext);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "Frame output path is too long\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (fwrite(data, bytesused, 1, fp) != 1) {
        fprintf(stderr, "Failed to write %s: %s\n", path, strerror(errno));
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

void camera_config_init(camera_config_t *config)
{
    memset(config, 0, sizeof(*config));
    strncpy(config->device, "/dev/video1", sizeof(config->device) - 1);
    strncpy(config->output, "output.yuv", sizeof(config->output) - 1);
    config->width = 1024;
    config->height = 600;
    config->fps = 30;
    config->frame_count = 10;
    config->http_port = 8080;
    config->pixel_format = V4L2_PIX_FMT_YUYV;
    
}

int camera_parse_pixel_format(const char *name, unsigned int *format)
{
    if (strcmp(name, "YUYV") == 0) {
        *format = V4L2_PIX_FMT_YUYV;
        return 0;
    }
    if (strcmp(name, "NV12") == 0) {
        *format = V4L2_PIX_FMT_NV12;
        return 0;
    }
    if (strcmp(name, "RGBP"  ) == 0) {
        *format = V4L2_PIX_FMT_RGB565;
        return 0;
    }
    if (strcmp(name, "MJPG") == 0 || strcmp(name, "MJPEG") == 0) {
        *format = V4L2_PIX_FMT_MJPEG;
        return 0;
    }

    return -1;
}

const char *camera_pixel_format_name(unsigned int format)
{
    switch (format) {
    case V4L2_PIX_FMT_YUYV:
        return "YUYV";
    case V4L2_PIX_FMT_NV12:
        return "NV12";
    case V4L2_PIX_FMT_RGB565:
        return "RGBP";
    case V4L2_PIX_FMT_MJPEG:
        return "MJPG";
    default:
        return "UNKNOWN";
    }
}

int camera_open(camera_context_t *ctx, const camera_config_t *config)
{
    struct stat st;

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    ctx->config = *config;

    if (stat(config->device, &st) == -1) {
        fprintf(stderr, "Device not found: %s: %s\n", config->device, strerror(errno));
        return -1;
    }
    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "Not a character device: %s\n", config->device);
        return -1;
    }

    ctx->fd = open(config->device, O_RDWR | O_NONBLOCK, 0);
    if (ctx->fd == -1) {
        fprintf(stderr, "Failed to open %s: %s\n", config->device, strerror(errno));
        return -1;
    }

    return 0;
}

int camera_configure(camera_context_t *ctx)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm parm;
    struct v4l2_requestbuffers req;
    unsigned int i;

    memset(&cap, 0, sizeof(cap));
    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming I/O\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = ctx->config.width;
    fmt.fmt.pix.height = ctx->config.height;
    fmt.fmt.pix.pixelformat = ctx->config.pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    ctx->config.width = fmt.fmt.pix.width;
    ctx->config.height = fmt.fmt.pix.height;
    ctx->config.pixel_format = fmt.fmt.pix.pixelformat;

    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = ctx->config.fps;
    if (xioctl(ctx->fd, VIDIOC_S_PARM, &parm) == -1) {
        fprintf(stderr, "Warning: failed to set fps: %s\n", strerror(errno));
    }

    memset(&req, 0, sizeof(req));
    req.count = CAMERA_MAX_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }
    if (req.count < 2) {
        fprintf(stderr, "Not enough V4L2 buffers\n");
        return -1;
    }

    ctx->buffer_count = req.count;
    for (i = 0; i < ctx->buffer_count; ++i) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            ctx->buffers[i].start = NULL;
            perror("mmap");
            return -1;
        }
    }

    return 0;
}

int camera_start(camera_context_t *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    unsigned int i;

    for (i = 0; i < ctx->buffer_count; ++i) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

int camera_capture_to_file(camera_context_t *ctx)
{
    FILE *fp = NULL;
    int server_fd = -1;
    int client_fd = -1;
    int result = -1;
    int sender_started = 0;
    int sender_joined = 0;
    int count = 0;
    unsigned long long total_bytes = 0;
    struct timeval start_time;
    struct timeval end_time;
    unsigned int timeout_count = 0;
    unsigned int empty_count = 0;
    frame_queue_t http_queue;
    http_sender_args_t sender_args;
    pthread_t sender_thread;

    memset(&http_queue, 0, sizeof(http_queue));
    memset(&sender_args, 0, sizeof(sender_args));

    if (ctx->config.http_mjpeg) {
        if (ctx->config.pixel_format != V4L2_PIX_FMT_MJPEG) {
            fprintf(stderr, "HTTP MJPEG streaming requires -f MJPG or -f MJPEG\n");
            return -1;
        }
        signal(SIGPIPE, SIG_IGN);
        server_fd = http_mjpeg_listen(ctx->config.http_port);
        if (server_fd == -1) {
            return -1;
        }
        client_fd = http_mjpeg_accept_client(server_fd);
        if (client_fd == -1) {
            http_mjpeg_close(server_fd);
            return -1;
        }
        if (frame_queue_init(&http_queue, HTTP_FRAME_QUEUE_CAPACITY) != 0) {
            fprintf(stderr, "Failed to initialize HTTP frame queue\n");
            http_mjpeg_close(client_fd);
            http_mjpeg_close(server_fd);
            return -1;
        }
        sender_args.queue = &http_queue;
        sender_args.client_fd = client_fd;
        if (pthread_create(&sender_thread, NULL, http_sender_thread, &sender_args) != 0) {
            fprintf(stderr, "Failed to create HTTP sender thread\n");
            frame_queue_destroy(&http_queue);
            http_mjpeg_close(client_fd);
            http_mjpeg_close(server_fd);
            return -1;
        }
        sender_started = 1;
    } else if (ctx->config.save_frames) {
        if (ensure_directory(ctx->config.frame_dir) != 0) {
            return -1;
        }
        printf("Saving frames into directory: %s\n", ctx->config.frame_dir);
    } else if (!ctx->config.no_save) {
        fp = fopen(ctx->config.output, "wb");
        if (fp == NULL) {
            perror("fopen");
            return -1;
        }
    } else {
        printf("Output file disabled, capture metrics only\n");
    }

    gettimeofday(&start_time, NULL);

    while (ctx->config.frame_count == 0 || count < ctx->config.frame_count) {
        struct v4l2_buffer buf;
        struct timeval timeout;
        fd_set fds;
        int ret;
        unsigned int bytesused;
        unsigned int sequence;

        FD_ZERO(&fds);
        FD_SET(ctx->fd, &fds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        ret = select(ctx->fd + 1, &fds, NULL, NULL, &timeout);
        if (ret == -1) {
            perror("select");
            goto out;
        }
        if (ret == 0) {
            ++timeout_count;
            fprintf(stderr, "Warning: capture timeout\n");
            continue;
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("VIDIOC_DQBUF");
            goto out;
        }

        bytesused = buf.bytesused;
        sequence = buf.sequence;

        if (bytesused == 0) {
            ++empty_count;
            fprintf(stderr, "\nWarning: empty frame, sequence=%u\n", sequence);
            if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                perror("VIDIOC_QBUF");
                goto out;
            }
            continue;
        }

        if (ctx->config.http_mjpeg) {
            frame_t frame;

            memset(&frame, 0, sizeof(frame));
            frame.data = (unsigned char *)ctx->buffers[buf.index].start;
            frame.size = bytesused;
            frame.width = ctx->config.width;
            frame.height = ctx->config.height;
            frame.pixel_format = ctx->config.pixel_format;
            frame.sequence = sequence;
            frame.timestamp_us = timestamp_now_us();

            if (frame_queue_push(&http_queue, &frame) != 0) {
                fprintf(stderr, "\nHTTP frame queue closed or push failed\n");
                if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                    perror("VIDIOC_QBUF");
                }
                break;
            }
        } else if (ctx->config.save_frames) {
            if (write_frame_file(ctx,
                                 ctx->buffers[buf.index].start,
                                 bytesused,
                                 count + 1) != 0) {
                if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                    perror("VIDIOC_QBUF");
                }
                goto out;
            }
        } else if (fp != NULL) {
            if (fwrite(ctx->buffers[buf.index].start, bytesused, 1, fp) != 1) {
                perror("fwrite");
                goto out;
            }
        }
        total_bytes += bytesused;

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            goto out;
        }

        ++count;
        printf("\rCaptured frame %d, sequence=%u, bytes=%u", count, sequence, bytesused);
        fflush(stdout);
    }

    if (ctx->config.http_mjpeg) {
        frame_queue_close(&http_queue);
        if (sender_started) {
            pthread_join(sender_thread, NULL);
            sender_joined = 1;
        }
    }

    gettimeofday(&end_time, NULL);
    {
        double elapsed = (double)(end_time.tv_sec - start_time.tv_sec) +
                         (double)(end_time.tv_usec - start_time.tv_usec) / 1000000.0;
        double fps = elapsed > 0.0 ? (double)count / elapsed : 0.0;
        double avg_size = count > 0 ? (double)total_bytes / (double)count : 0.0;

        printf("\nCapture finished: %d frame(s)\n", count);
        printf("Elapsed: %.3f sec, fps: %.2f, total: %llu bytes, avg frame: %.1f bytes\n",
               elapsed,
               fps,
               total_bytes,
               avg_size);
        printf("Timeouts: %u, empty frames: %u, save: %s\n",
               timeout_count,
               empty_count,
               ctx->config.no_save ? "disabled" :
               (ctx->config.save_frames ? "frames" :
                (ctx->config.http_mjpeg ? "http-mjpeg" : "enabled")));
        if (ctx->config.http_mjpeg) {
            printf("HTTP sent frames: %d, sent bytes: %llu, queued dropped frames: %lu, send failed: %s\n",
                   sender_args.sent_frames,
                   sender_args.sent_bytes,
                   frame_queue_dropped(&http_queue),
                   sender_args.send_failed ? "yes" : "no");
        }
    }

    result = 0;

out:
    if (ctx->config.http_mjpeg) {
        frame_queue_close(&http_queue);
        if (sender_started && !sender_joined) {
            pthread_join(sender_thread, NULL);
        }
        frame_queue_destroy(&http_queue);
    }
    if (fp != NULL)
        fclose(fp);
    http_mjpeg_close(client_fd);
    http_mjpeg_close(server_fd);
    return result;
}

void camera_stop(camera_context_t *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ctx->fd != -1) {
        if (xioctl(ctx->fd, VIDIOC_STREAMOFF, &type) == -1) {
            fprintf(stderr, "Warning: VIDIOC_STREAMOFF failed: %s\n", strerror(errno));
        }
    }
}

void camera_close(camera_context_t *ctx)
{
    unsigned int i;

    for (i = 0; i < ctx->buffer_count; ++i) {
        if (ctx->buffers[i].start != NULL) {
            munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            ctx->buffers[i].start = NULL;
            ctx->buffers[i].length = 0;
        }
    }

    if (ctx->fd != -1) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}
