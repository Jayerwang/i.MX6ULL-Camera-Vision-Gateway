#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    unsigned int pixel_format;
    unsigned int sequence;
    unsigned long long timestamp_us;
} frame_t;

void frame_release(frame_t *frame);

#endif
