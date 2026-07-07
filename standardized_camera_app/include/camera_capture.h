#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <linux/videodev2.h>
#include <stddef.h>

#define CAMERA_MAX_BUFFERS 4

typedef struct {
    char device[128];
    char output[256];
    char frame_dir[256];
    int width;
    int height;
    int fps;
    int frame_count;
    int no_save;
    int save_frames;
    int http_mjpeg;
    int http_port;
    unsigned int pixel_format;
} camera_config_t;

typedef struct {
    void *start;
    size_t length;
} camera_buffer_t;

typedef struct {
    int fd;
    camera_config_t config;
    camera_buffer_t buffers[CAMERA_MAX_BUFFERS];
    unsigned int buffer_count;
} camera_context_t;

void camera_config_init(camera_config_t *config);
int camera_parse_pixel_format(const char *name, unsigned int *format);
const char *camera_pixel_format_name(unsigned int format);

int camera_open(camera_context_t *ctx, const camera_config_t *config);
int camera_configure(camera_context_t *ctx);
int camera_start(camera_context_t *ctx);
int camera_capture_to_file(camera_context_t *ctx);
void camera_stop(camera_context_t *ctx);
void camera_close(camera_context_t *ctx);

#endif
