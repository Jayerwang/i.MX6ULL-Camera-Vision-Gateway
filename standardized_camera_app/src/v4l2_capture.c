#include "camera_capture.h"
#include "frame.h"
#include "framebuffer_display.h"
#include "jpeg_decoder.h"
#include "stream_http_mjpeg.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    camera_context_t *ctx;
    framebuffer_display_t *display;
    frame_t latest;
    int has_frame;
    int stop;
    int failed;
    unsigned int timeout_count;
    unsigned int empty_count;
    unsigned int connected_clients;
    unsigned int total_clients;
    unsigned int captured_frames;
    unsigned int lcd_frames;
    unsigned int lcd_errors;
    unsigned int processed_frames;
    unsigned int motion_detected;
    unsigned int motion_events;
    unsigned int motion_snapshots;
    unsigned int motion_errors;
    unsigned int brightness;
    unsigned int motion_delta;
    unsigned int motion_max_delta;
    unsigned int motion_active_frames;
    unsigned int motion_box_valid;
    unsigned int motion_box_x;
    unsigned int motion_box_y;
    unsigned int motion_box_w;
    unsigned int motion_box_h;
    unsigned int motion_box_peak_delta;
    unsigned int motion_active_blocks;
    uint16_t *motion_previous;
    unsigned int motion_previous_width;
    unsigned int motion_previous_height;
    int motion_has_previous;
    int motion_was_detected;
    unsigned long long total_bytes;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} http_service_t;

typedef struct {
    http_service_t *service;
    int client_fd;
    char path[256];
} http_client_args_t;

#define MOTION_GRID_COLS 32
#define MOTION_GRID_ROWS 24
#define MOTION_SAMPLE_STEP 4

static int send_http_service_metrics(http_service_t *service, int client_fd);
static int ensure_directory(const char *path);

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

static int frame_copy_alloc(frame_t *dst, const frame_t *src)
{
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->data = NULL;

    if (src->data == NULL || src->size == 0) {
        return -1;
    }

    dst->data = (unsigned char *)malloc(src->size);
    if (dst->data == NULL) {
        memset(dst, 0, sizeof(*dst));
        return -1;
    }

    memcpy(dst->data, src->data, src->size);
    return 0;
}

static int http_service_init(http_service_t *service, camera_context_t *ctx)
{
    memset(service, 0, sizeof(*service));
    service->ctx = ctx;

    if (pthread_mutex_init(&service->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&service->cond, NULL) != 0) {
        pthread_mutex_destroy(&service->mutex);
        memset(service, 0, sizeof(*service));
        return -1;
    }

    return 0;
}

static void http_service_destroy(http_service_t *service)
{
    pthread_mutex_lock(&service->mutex);
    frame_release(&service->latest);
    free(service->motion_previous);
    pthread_mutex_unlock(&service->mutex);

    pthread_cond_destroy(&service->cond);
    pthread_mutex_destroy(&service->mutex);
    memset(service, 0, sizeof(*service));
}

static void http_service_stop(http_service_t *service)
{
    pthread_mutex_lock(&service->mutex);
    service->stop = 1;
    pthread_cond_broadcast(&service->cond);
    pthread_mutex_unlock(&service->mutex);
}

static int http_service_update_latest(http_service_t *service, const frame_t *frame)
{
    frame_t copy;

    if (frame_copy_alloc(&copy, frame) != 0) {
        return -1;
    }

    pthread_mutex_lock(&service->mutex);
    frame_release(&service->latest);
    service->latest = copy;
    service->has_frame = 1;
    pthread_cond_broadcast(&service->cond);
    pthread_mutex_unlock(&service->mutex);

    return 0;
}

static int http_service_copy_latest(http_service_t *service, frame_t *out)
{
    int ret = -1;

    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&service->mutex);
    if (service->has_frame) {
        ret = frame_copy_alloc(out, &service->latest);
    }
    pthread_mutex_unlock(&service->mutex);

    return ret;
}

static unsigned int rgb565_luma(uint16_t pixel)
{
    unsigned int r = ((pixel >> 11) & 0x1f) * 255 / 31;
    unsigned int g = ((pixel >> 5) & 0x3f) * 255 / 63;
    unsigned int b = (pixel & 0x1f) * 255 / 31;

    return (r * 77 + g * 150 + b * 29) >> 8;
}

static uint16_t red_tint_rgb565(uint16_t pixel)
{
    unsigned int r = ((pixel >> 11) & 0x1f) * 255 / 31;
    unsigned int g = ((pixel >> 5) & 0x3f) * 255 / 63;
    unsigned int b = (pixel & 0x1f) * 255 / 31;

    r = (r + 255) / 2;
    g = g / 3;
    b = b / 3;

    return (uint16_t)(((r * 31 / 255) << 11) |
                      ((g * 63 / 255) << 5) |
                      (b * 31 / 255));
}

static void draw_rgb565_motion_mask(uint16_t *pixels,
                                    unsigned int width,
                                    unsigned int height,
                                    const unsigned char active_blocks[MOTION_GRID_ROWS][MOTION_GRID_COLS])
{
    unsigned int row;
    unsigned int col;

    if (pixels == NULL || width == 0 || height == 0 || active_blocks == NULL) {
        return;
    }

    for (row = 0; row < MOTION_GRID_ROWS; ++row) {
        unsigned int y0 = row * height / MOTION_GRID_ROWS;
        unsigned int y1 = (row + 1) * height / MOTION_GRID_ROWS;

        for (col = 0; col < MOTION_GRID_COLS; ++col) {
            unsigned int x0;
            unsigned int x1;
            unsigned int x;
            unsigned int y;

            if (!active_blocks[row][col]) {
                continue;
            }

            x0 = col * width / MOTION_GRID_COLS;
            x1 = (col + 1) * width / MOTION_GRID_COLS;

            for (y = y0; y < y1; ++y) {
                for (x = x0; x < x1; ++x) {
                    size_t index = (size_t)y * width + x;
                    pixels[index] = red_tint_rgb565(pixels[index]);
                }
            }
        }
    }
}

static int http_service_draw_lcd_frame(http_service_t *service,
                                       const unsigned char *jpeg_data,
                                       unsigned int jpeg_size)
{
    uint16_t *rgb565 = NULL;
    unsigned int width = 0;
    unsigned int height = 0;
    int motion_event = 0;
    int ret = -1;

    if (service->display == NULL && !service->ctx->config.motion_detect) {
        return 0;
    }

    if (jpeg_decode_to_rgb565(jpeg_data, jpeg_size, &rgb565, &width, &height) != 0) {
        goto out;
    }

    if (service->ctx->config.motion_detect) {
        unsigned int brightness = 0;
        unsigned int delta = 0;
        unsigned int detected = 0;
        size_t pixel_count = (size_t)width * height;
        unsigned int block_delta_sum[MOTION_GRID_ROWS][MOTION_GRID_COLS];
        unsigned int block_samples[MOTION_GRID_ROWS][MOTION_GRID_COLS];
        unsigned char active_blocks[MOTION_GRID_ROWS][MOTION_GRID_COLS];
        unsigned int block_min_x = MOTION_GRID_COLS;
        unsigned int block_min_y = MOTION_GRID_ROWS;
        unsigned int block_max_x = 0;
        unsigned int block_max_y = 0;
        unsigned int box_valid = 0;
        unsigned int box_x = 0;
        unsigned int box_y = 0;
        unsigned int box_w = 0;
        unsigned int box_h = 0;
        unsigned int box_peak_delta = 0;
        unsigned int active_block_count = 0;
        unsigned int x;
        unsigned int y;
        unsigned int row;
        unsigned int col;
        unsigned long long brightness_sum = 0;
        unsigned long long delta_sum = 0;
        unsigned int sample_count = 0;

        memset(block_delta_sum, 0, sizeof(block_delta_sum));
        memset(block_samples, 0, sizeof(block_samples));
        memset(active_blocks, 0, sizeof(active_blocks));

        if (service->motion_previous_width != width ||
            service->motion_previous_height != height) {
            free(service->motion_previous);
            service->motion_previous = NULL;
            service->motion_has_previous = 0;
            service->motion_previous_width = width;
            service->motion_previous_height = height;
        }

        if (service->motion_previous == NULL) {
            service->motion_previous = (uint16_t *)malloc(pixel_count * sizeof(*service->motion_previous));
            if (service->motion_previous == NULL) {
                pthread_mutex_lock(&service->mutex);
                ++service->motion_errors;
                pthread_mutex_unlock(&service->mutex);
                goto out;
            }
        }

        for (y = 0; y < height; y += MOTION_SAMPLE_STEP) {
            for (x = 0; x < width; x += MOTION_SAMPLE_STEP) {
                size_t index = (size_t)y * width + x;
                unsigned int luma = rgb565_luma(rgb565[index]);

                brightness_sum += luma;
                if (service->motion_has_previous) {
                    unsigned int prev_luma = rgb565_luma(service->motion_previous[index]);
                    unsigned int sample_delta = luma > prev_luma ?
                                                luma - prev_luma :
                                                prev_luma - luma;
                    unsigned int block_x = x * MOTION_GRID_COLS / width;
                    unsigned int block_y = y * MOTION_GRID_ROWS / height;

                    if (block_x >= MOTION_GRID_COLS) {
                        block_x = MOTION_GRID_COLS - 1;
                    }
                    if (block_y >= MOTION_GRID_ROWS) {
                        block_y = MOTION_GRID_ROWS - 1;
                    }

                    delta_sum += sample_delta;
                    block_delta_sum[block_y][block_x] += sample_delta;
                    ++block_samples[block_y][block_x];
                }
                ++sample_count;
            }
        }

        if (sample_count > 0) {
            brightness = (unsigned int)(brightness_sum / sample_count);
            delta = service->motion_has_previous ?
                    (unsigned int)(delta_sum / sample_count) : 0;
        }

        if (service->motion_has_previous) {
            unsigned int threshold = (unsigned int)service->ctx->config.motion_threshold;

            for (row = 0; row < MOTION_GRID_ROWS; ++row) {
                for (col = 0; col < MOTION_GRID_COLS; ++col) {
                    unsigned int block_delta = 0;

                    if (block_samples[row][col] > 0) {
                        block_delta = block_delta_sum[row][col] / block_samples[row][col];
                    }
                    if (block_delta > box_peak_delta) {
                        box_peak_delta = block_delta;
                    }
                    if (block_delta >= threshold) {
                        active_blocks[row][col] = 1;
                        ++active_block_count;
                        if (col < block_min_x) {
                            block_min_x = col;
                        }
                        if (row < block_min_y) {
                            block_min_y = row;
                        }
                        if (col > block_max_x) {
                            block_max_x = col;
                        }
                        if (row > block_max_y) {
                            block_max_y = row;
                        }
                    }
                }
            }

            box_valid = block_min_x <= block_max_x && block_min_y <= block_max_y;
            if (box_valid) {
                box_x = block_min_x * width / MOTION_GRID_COLS;
                box_y = block_min_y * height / MOTION_GRID_ROWS;
                box_w = (block_max_x + 1) * width / MOTION_GRID_COLS - box_x;
                box_h = (block_max_y + 1) * height / MOTION_GRID_ROWS - box_y;
            }
        }

        detected = service->motion_has_previous && box_valid;

        memcpy(service->motion_previous, rgb565, pixel_count * sizeof(*service->motion_previous));
        service->motion_has_previous = 1;

        pthread_mutex_lock(&service->mutex);
        ++service->processed_frames;
        service->brightness = brightness;
        service->motion_delta = delta;
        if (delta > service->motion_max_delta) {
            service->motion_max_delta = delta;
        }
        service->motion_box_valid = box_valid;
        service->motion_box_x = box_x;
        service->motion_box_y = box_y;
        service->motion_box_w = box_w;
        service->motion_box_h = box_h;
        service->motion_box_peak_delta = box_peak_delta;
        service->motion_active_blocks = active_block_count;
        service->motion_detected = detected;
        if (detected) {
            ++service->motion_active_frames;
        }
        if (detected && !service->motion_was_detected) {
            ++service->motion_events;
            motion_event = 1;
        }
        service->motion_was_detected = detected;
        pthread_mutex_unlock(&service->mutex);

        if (detected && service->display != NULL) {
            draw_rgb565_motion_mask(rgb565, width, height, active_blocks);
        }
    }

    if (service->display != NULL) {
        if (framebuffer_display_draw_rgb565(service->display, rgb565, width, height) != 0) {
            goto out;
        }
    }

    ret = 0;

out:
    if (ret == 0 && motion_event && service->ctx->config.motion_save) {
        char path[512];
        unsigned int event_id;
        FILE *fp;

        pthread_mutex_lock(&service->mutex);
        event_id = service->motion_events;
        pthread_mutex_unlock(&service->mutex);

        int written = snprintf(path, sizeof(path), "%s/motion_%06u.jpg",
                               service->ctx->config.motion_dir,
                               event_id);

        if (written >= 0 && (size_t)written < sizeof(path)) {
            fp = fopen(path, "wb");
            if (fp != NULL) {
                if (fwrite(jpeg_data, jpeg_size, 1, fp) == 1) {
                    pthread_mutex_lock(&service->mutex);
                    ++service->motion_snapshots;
                    pthread_mutex_unlock(&service->mutex);
                } else {
                    pthread_mutex_lock(&service->mutex);
                    ++service->motion_errors;
                    pthread_mutex_unlock(&service->mutex);
                }
                fclose(fp);
            } else {
                pthread_mutex_lock(&service->mutex);
                ++service->motion_errors;
                pthread_mutex_unlock(&service->mutex);
            }
        }
    }

    free(rgb565);
    pthread_mutex_lock(&service->mutex);
    if (ret == 0 && service->display != NULL) {
        ++service->lcd_frames;
    } else if (ret != 0 && service->display != NULL) {
        ++service->lcd_errors;
    }
    pthread_mutex_unlock(&service->mutex);
    return ret;
}

static int http_service_wait_latest(http_service_t *service,
                                    frame_t *out,
                                    unsigned int *last_sequence)
{
    struct timeval now;
    struct timespec deadline;
    int ret = -1;

    memset(out, 0, sizeof(*out));
    gettimeofday(&now, NULL);
    deadline.tv_sec = now.tv_sec + 2;
    deadline.tv_nsec = now.tv_usec * 1000L;

    pthread_mutex_lock(&service->mutex);
    while (!service->stop &&
           (!service->has_frame || service->latest.sequence == *last_sequence)) {
        if (pthread_cond_timedwait(&service->cond, &service->mutex, &deadline) == ETIMEDOUT) {
            break;
        }
    }

    if (!service->stop &&
        service->has_frame &&
        service->latest.sequence != *last_sequence) {
        if (frame_copy_alloc(out, &service->latest) == 0) {
            *last_sequence = out->sequence;
            ret = 0;
        }
    }
    pthread_mutex_unlock(&service->mutex);

    return ret;
}

static void *http_service_capture_thread(void *arg)
{
    http_service_t *service = (http_service_t *)arg;
    camera_context_t *ctx = service->ctx;

    while (1) {
        struct v4l2_buffer buf;
        struct timeval timeout;
        fd_set fds;
        int ret;
        unsigned int bytesused;
        unsigned int sequence;

        pthread_mutex_lock(&service->mutex);
        if (service->stop) {
            pthread_mutex_unlock(&service->mutex);
            break;
        }
        pthread_mutex_unlock(&service->mutex);

        FD_ZERO(&fds);
        FD_SET(ctx->fd, &fds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        ret = select(ctx->fd + 1, &fds, NULL, NULL, &timeout);
        if (ret == -1) {
            perror("select");
            pthread_mutex_lock(&service->mutex);
            service->failed = 1;
            pthread_cond_broadcast(&service->cond);
            pthread_mutex_unlock(&service->mutex);
            break;
        }
        if (ret == 0) {
            pthread_mutex_lock(&service->mutex);
            ++service->timeout_count;
            pthread_mutex_unlock(&service->mutex);
            fprintf(stderr, "Warning: capture timeout\n");
            continue;
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("VIDIOC_DQBUF");
            pthread_mutex_lock(&service->mutex);
            service->failed = 1;
            pthread_cond_broadcast(&service->cond);
            pthread_mutex_unlock(&service->mutex);
            break;
        }

        bytesused = buf.bytesused;
        sequence = buf.sequence;

        if (bytesused == 0) {
            pthread_mutex_lock(&service->mutex);
            ++service->empty_count;
            pthread_mutex_unlock(&service->mutex);
            fprintf(stderr, "\nWarning: empty frame, sequence=%u\n", sequence);
        } else {
            frame_t frame;

            memset(&frame, 0, sizeof(frame));
            frame.data = (unsigned char *)ctx->buffers[buf.index].start;
            frame.size = bytesused;
            frame.width = ctx->config.width;
            frame.height = ctx->config.height;
            frame.pixel_format = ctx->config.pixel_format;
            frame.sequence = sequence;
            frame.timestamp_us = timestamp_now_us();

            if (http_service_update_latest(service, &frame) != 0) {
                fprintf(stderr, "\nFailed to update latest frame\n");
                pthread_mutex_lock(&service->mutex);
                service->failed = 1;
                pthread_cond_broadcast(&service->cond);
                pthread_mutex_unlock(&service->mutex);
            } else {
                if (http_service_draw_lcd_frame(service,
                                                frame.data,
                                                (unsigned int)frame.size) != 0 &&
                    service->display != NULL) {
                    fprintf(stderr, "\nWarning: failed to draw LCD preview frame\n");
                }

                pthread_mutex_lock(&service->mutex);
                ++service->captured_frames;
                service->total_bytes += bytesused;
                pthread_mutex_unlock(&service->mutex);
                printf("\rCaptured frame %u, sequence=%u, bytes=%u",
                       service->captured_frames,
                       sequence,
                       bytesused);
                fflush(stdout);
            }
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            pthread_mutex_lock(&service->mutex);
            service->failed = 1;
            pthread_cond_broadcast(&service->cond);
            pthread_mutex_unlock(&service->mutex);
            break;
        }
    }

    http_service_stop(service);
    return NULL;
}

static void *http_client_thread(void *arg)
{
    http_client_args_t *client = (http_client_args_t *)arg;
    http_service_t *service = client->service;
    int client_fd = client->client_fd;

    if (strcmp(client->path, "/metrics") == 0) {
        (void)send_http_service_metrics(service, client_fd);
    } else if (strcmp(client->path, "/snapshot") == 0) {
        frame_t snapshot;

        if (http_service_copy_latest(service, &snapshot) != 0) {
            (void)http_mjpeg_send_text_response(client_fd,
                                                "503 Service Unavailable",
                                                "snapshot not ready\n");
        } else {
            (void)http_mjpeg_send_jpeg_response(client_fd,
                                                snapshot.data,
                                                snapshot.size);
            frame_release(&snapshot);
        }
    } else if (strcmp(client->path, "/") == 0 || strcmp(client->path, "/stream") == 0) {
        unsigned int last_sequence = 0;

        if (http_mjpeg_send_stream_header(client_fd) == 0) {
            while (1) {
                frame_t frame;
                int should_stop;

                if (http_service_wait_latest(service, &frame, &last_sequence) != 0) {
                    pthread_mutex_lock(&service->mutex);
                    should_stop = service->stop || service->failed;
                    pthread_mutex_unlock(&service->mutex);
                    if (should_stop) {
                        break;
                    }
                    continue;
                }

                if (http_mjpeg_send_frame(client_fd, frame.data, frame.size) != 0) {
                    frame_release(&frame);
                    break;
                }
                frame_release(&frame);
            }
        }
    } else {
        (void)http_mjpeg_send_not_found(client_fd);
    }

    pthread_mutex_lock(&service->mutex);
    if (service->connected_clients > 0) {
        --service->connected_clients;
    }
    pthread_mutex_unlock(&service->mutex);

    http_mjpeg_close(client_fd);
    printf("HTTP request finished: %s\n", client->path);
    free(client);
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

static int clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

static uint16_t rgb_to_rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xf8) << 8) |
                      ((g & 0xfc) << 3) |
                      ((b & 0xf8) >> 3));
}

static void yuyv_to_rgb565(const unsigned char *yuyv,
                           uint16_t *rgb565,
                           unsigned int width,
                           unsigned int height)
{
    unsigned int x;
    unsigned int y;

    for (y = 0; y < height; ++y) {
        const unsigned char *src = yuyv + (size_t)y * width * 2;
        uint16_t *dst = rgb565 + (size_t)y * width;

        for (x = 0; x + 1 < width; x += 2) {
            int y0 = src[0] - 16;
            int u = src[1] - 128;
            int y1 = src[2] - 16;
            int v = src[3] - 128;
            int c0 = y0 < 0 ? 0 : 298 * y0;
            int c1 = y1 < 0 ? 0 : 298 * y1;
            int r0 = clamp_u8((c0 + 409 * v + 128) >> 8);
            int g0 = clamp_u8((c0 - 100 * u - 208 * v + 128) >> 8);
            int b0 = clamp_u8((c0 + 516 * u + 128) >> 8);
            int r1 = clamp_u8((c1 + 409 * v + 128) >> 8);
            int g1 = clamp_u8((c1 - 100 * u - 208 * v + 128) >> 8);
            int b1 = clamp_u8((c1 + 516 * u + 128) >> 8);

            dst[x] = rgb_to_rgb565(r0, g0, b0);
            dst[x + 1] = rgb_to_rgb565(r1, g1, b1);
            src += 4;
        }
    }
}

static int camera_capture_to_framebuffer(camera_context_t *ctx)
{
    framebuffer_display_t *display = NULL;
    uint16_t *yuyv_rgb565 = NULL;
    int result = -1;
    int count = 0;
    unsigned int timeout_count = 0;
    unsigned int empty_count = 0;

    if (ctx->config.pixel_format != V4L2_PIX_FMT_YUYV &&
        ctx->config.pixel_format != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "Framebuffer preview currently supports YUYV and MJPG\n");
        return -1;
    }

    if (ctx->config.pixel_format == V4L2_PIX_FMT_YUYV) {
        yuyv_rgb565 = (uint16_t *)malloc((size_t)ctx->config.width *
                                         (size_t)ctx->config.height *
                                         sizeof(*yuyv_rgb565));
        if (yuyv_rgb565 == NULL) {
            fprintf(stderr, "Failed to allocate RGB565 preview buffer\n");
            return -1;
        }
    }

    if (framebuffer_display_open(&display, ctx->config.fb_device) != 0) {
        goto out;
    }

    while (ctx->config.frame_count == 0 || count < ctx->config.frame_count) {
        struct v4l2_buffer buf;
        struct timeval timeout;
        fd_set fds;
        int ret;
        unsigned int bytesused;

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
        if (bytesused == 0) {
            ++empty_count;
            fprintf(stderr, "\nWarning: empty frame, sequence=%u\n", buf.sequence);
        } else if (ctx->config.pixel_format == V4L2_PIX_FMT_YUYV &&
                   bytesused < (unsigned int)(ctx->config.width * ctx->config.height * 2)) {
            ++empty_count;
            fprintf(stderr, "\nWarning: short YUYV frame, sequence=%u, bytes=%u\n",
                    buf.sequence,
                    bytesused);
        } else if (ctx->config.pixel_format == V4L2_PIX_FMT_YUYV) {
            yuyv_to_rgb565((const unsigned char *)ctx->buffers[buf.index].start,
                           yuyv_rgb565,
                           ctx->config.width,
                           ctx->config.height);

            if (framebuffer_display_draw_rgb565(display,
                                                yuyv_rgb565,
                                                ctx->config.width,
                                                ctx->config.height) != 0) {
                if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                    perror("VIDIOC_QBUF");
                }
                goto out;
            }

            ++count;
            printf("\rDisplayed frame %d, sequence=%u, bytes=%u",
                   count,
                   buf.sequence,
                   bytesused);
            fflush(stdout);
        } else {
            uint16_t *decoded_rgb565 = NULL;
            unsigned int decoded_width = 0;
            unsigned int decoded_height = 0;

            if (jpeg_decode_to_rgb565((const unsigned char *)ctx->buffers[buf.index].start,
                                      bytesused,
                                      &decoded_rgb565,
                                      &decoded_width,
                                      &decoded_height) != 0) {
                if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                    perror("VIDIOC_QBUF");
                }
                goto out;
            }

            if (framebuffer_display_draw_rgb565(display,
                                                decoded_rgb565,
                                                decoded_width,
                                                decoded_height) != 0) {
                free(decoded_rgb565);
                if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
                    perror("VIDIOC_QBUF");
                }
                goto out;
            }

            free(decoded_rgb565);
            ++count;
            printf("\rDisplayed frame %d, sequence=%u, jpeg=%u, decoded=%ux%u",
                   count,
                   buf.sequence,
                   bytesused,
                   decoded_width,
                   decoded_height);
            fflush(stdout);
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            goto out;
        }
    }

    printf("\nFramebuffer preview finished: %d frame(s)\n", count);
    printf("Timeouts: %u, empty frames: %u\n", timeout_count, empty_count);
    result = 0;

out:
    framebuffer_display_close(display);
    free(yuyv_rgb565);
    return result;
}

static int send_http_service_metrics(http_service_t *service, int client_fd)
{
    camera_context_t *ctx = service->ctx;
    frame_t latest;
    unsigned int connected_clients;
    unsigned int total_clients;
    unsigned int captured_frames;
    unsigned int lcd_frames;
    unsigned int lcd_errors;
    unsigned int processed_frames;
    unsigned int motion_detected;
    unsigned int motion_events;
    unsigned int motion_snapshots;
    unsigned int motion_errors;
    unsigned int brightness;
    unsigned int motion_delta;
    unsigned int motion_max_delta;
    unsigned int motion_active_frames;
    unsigned int motion_box_valid;
    unsigned int motion_box_x;
    unsigned int motion_box_y;
    unsigned int motion_box_w;
    unsigned int motion_box_h;
    unsigned int motion_box_peak_delta;
    unsigned int motion_active_blocks;
    unsigned int timeout_count;
    unsigned int empty_count;
    unsigned long long total_bytes;
    unsigned int latest_sequence = 0;
    size_t latest_size = 0;
    char body[2048];
    int len;

    pthread_mutex_lock(&service->mutex);
    latest = service->latest;
    connected_clients = service->connected_clients;
    total_clients = service->total_clients;
    captured_frames = service->captured_frames;
    lcd_frames = service->lcd_frames;
    lcd_errors = service->lcd_errors;
    processed_frames = service->processed_frames;
    motion_detected = service->motion_detected;
    motion_events = service->motion_events;
    motion_snapshots = service->motion_snapshots;
    motion_errors = service->motion_errors;
    brightness = service->brightness;
    motion_delta = service->motion_delta;
    motion_max_delta = service->motion_max_delta;
    motion_active_frames = service->motion_active_frames;
    motion_box_valid = service->motion_box_valid;
    motion_box_x = service->motion_box_x;
    motion_box_y = service->motion_box_y;
    motion_box_w = service->motion_box_w;
    motion_box_h = service->motion_box_h;
    motion_box_peak_delta = service->motion_box_peak_delta;
    motion_active_blocks = service->motion_active_blocks;
    timeout_count = service->timeout_count;
    empty_count = service->empty_count;
    total_bytes = service->total_bytes;
    if (service->has_frame) {
        latest_sequence = latest.sequence;
        latest_size = latest.size;
    }
    pthread_mutex_unlock(&service->mutex);

    len = snprintf(body, sizeof(body),
                   "device=%s\n"
                   "format=%s\n"
                   "width=%d\n"
                   "height=%d\n"
                   "fps_request=%d\n"
                   "mode=multi-client-service\n"
                   "connected_clients=%u\n"
                   "total_clients=%u\n"
                   "captured_frames=%u\n"
                   "lcd_preview=%s\n"
                   "lcd_frames=%u\n"
                   "lcd_errors=%u\n"
                   "motion_detect=%s\n"
                   "processed_frames=%u\n"
                   "brightness=%u\n"
                   "motion_delta=%u\n"
                   "motion_max_delta=%u\n"
                   "motion_threshold=%d\n"
                   "motion_detected=%u\n"
                   "motion_active_frames=%u\n"
                   "motion_box_valid=%u\n"
                   "motion_box_x=%u\n"
                   "motion_box_y=%u\n"
                   "motion_box_w=%u\n"
                   "motion_box_h=%u\n"
                   "motion_box_peak_delta=%u\n"
                   "motion_active_blocks=%u\n"
                   "motion_events=%u\n"
                   "motion_snapshots=%u\n"
                   "motion_errors=%u\n"
                   "latest_sequence=%u\n"
                   "latest_frame_size=%lu\n"
                   "timeouts=%u\n"
                   "empty_frames=%u\n"
                   "total_bytes=%llu\n",
                   ctx->config.device,
                   camera_pixel_format_name(ctx->config.pixel_format),
                   ctx->config.width,
                   ctx->config.height,
                   ctx->config.fps,
                   connected_clients,
                   total_clients,
                   captured_frames,
                   service->display != NULL ? "enabled" : "disabled",
                   lcd_frames,
                   lcd_errors,
                   ctx->config.motion_detect ? "enabled" : "disabled",
                   processed_frames,
                   brightness,
                   motion_delta,
                   motion_max_delta,
                   ctx->config.motion_threshold,
                   motion_detected,
                   motion_active_frames,
                   motion_box_valid,
                   motion_box_x,
                   motion_box_y,
                   motion_box_w,
                   motion_box_h,
                   motion_box_peak_delta,
                   motion_active_blocks,
                   motion_events,
                   motion_snapshots,
                   motion_errors,
                   latest_sequence,
                   (unsigned long)latest_size,
                   timeout_count,
                   empty_count,
                   total_bytes);
    if (len < 0 || (size_t)len >= sizeof(body)) {
        return -1;
    }

    return http_mjpeg_send_text_response(client_fd, "200 OK", body);
}

static int camera_capture_http_mjpeg(camera_context_t *ctx)
{
    int server_fd = -1;
    int result = -1;
    int request_count = 0;
    int service_initialized = 0;
    int capture_started = 0;
    framebuffer_display_t *display = NULL;
    pthread_t capture_thread;
    http_service_t service;

    if (ctx->config.pixel_format != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "HTTP MJPEG streaming requires -f MJPG or -f MJPEG\n");
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

    if (ctx->config.fb_preview) {
        if (framebuffer_display_open(&display, ctx->config.fb_device) != 0) {
            goto out;
        }
        printf("LCD preview enabled: %s\n", ctx->config.fb_device);
    }
    if (ctx->config.motion_save) {
        if (ensure_directory(ctx->config.motion_dir) != 0) {
            goto out;
        }
        printf("Motion snapshot directory: %s\n", ctx->config.motion_dir);
    }

    if (http_service_init(&service, ctx) != 0) {
        fprintf(stderr, "Failed to initialize HTTP MJPEG service\n");
        goto out;
    }
    service_initialized = 1;
    service.display = display;

    if (pthread_create(&capture_thread, NULL, http_service_capture_thread, &service) != 0) {
        fprintf(stderr, "Failed to create HTTP capture thread\n");
        goto out;
    }
    capture_started = 1;

    server_fd = http_mjpeg_listen(ctx->config.http_port);
    if (server_fd == -1) {
        goto out;
    }

    do {
        int client_fd;
        char path[256];
        http_client_args_t *client_args;
        pthread_t client_thread;

        memset(path, 0, sizeof(path));
        client_fd = http_mjpeg_accept_request(server_fd, path, sizeof(path));
        if (client_fd == -1) {
            goto out;
        }

        ++request_count;
        client_args = (http_client_args_t *)calloc(1, sizeof(*client_args));
        if (client_args == NULL) {
            fprintf(stderr, "Failed to allocate HTTP client args\n");
            http_mjpeg_close(client_fd);
            goto out;
        }

        client_args->service = &service;
        client_args->client_fd = client_fd;
        strncpy(client_args->path, path, sizeof(client_args->path) - 1);

        pthread_mutex_lock(&service.mutex);
        ++service.connected_clients;
        ++service.total_clients;
        pthread_mutex_unlock(&service.mutex);

        if (pthread_create(&client_thread, NULL, http_client_thread, client_args) != 0) {
            fprintf(stderr, "Failed to create HTTP client thread\n");
            pthread_mutex_lock(&service.mutex);
            if (service.connected_clients > 0) {
                --service.connected_clients;
            }
            pthread_mutex_unlock(&service.mutex);
            http_mjpeg_close(client_fd);
            free(client_args);
            goto out;
        }

        if (ctx->config.frame_count == 0) {
            pthread_detach(client_thread);
        } else {
            pthread_join(client_thread, NULL);
        }
    } while (ctx->config.frame_count == 0);

    result = 0;

out:
    if (service_initialized) {
        http_service_stop(&service);
    }
    if (capture_started) {
        pthread_join(capture_thread, NULL);
    }
    printf("HTTP server handled %d request(s)\n", request_count);
    http_mjpeg_close(server_fd);
    if (service_initialized) {
        http_service_destroy(&service);
    }
    framebuffer_display_close(display);
    return result;
}

void camera_config_init(camera_config_t *config)
{
    memset(config, 0, sizeof(*config));
    strncpy(config->device, "/dev/video1", sizeof(config->device) - 1);
    strncpy(config->output, "output.yuv", sizeof(config->output) - 1);
    strncpy(config->fb_device, "/dev/fb0", sizeof(config->fb_device) - 1);
    strncpy(config->motion_dir, "/tmp/motion", sizeof(config->motion_dir) - 1);
    config->width = 1024;
    config->height = 600;
    config->fps = 30;
    config->frame_count = 10;
    config->http_port = 8080;
    config->motion_threshold = 20;
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
    int result = -1;
    int count = 0;
    unsigned long long total_bytes = 0;
    struct timeval start_time;
    struct timeval end_time;
    unsigned int timeout_count = 0;
    unsigned int empty_count = 0;

    if (ctx->config.http_mjpeg) {
        return camera_capture_http_mjpeg(ctx);
    }
    if (ctx->config.fb_preview) {
        return camera_capture_to_framebuffer(ctx);
    }

    if (ctx->config.save_frames) {
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

        if (ctx->config.save_frames) {
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
               (ctx->config.save_frames ? "frames" : "enabled"));
    }

    result = 0;

out:
    if (fp != NULL)
        fclose(fp);
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
