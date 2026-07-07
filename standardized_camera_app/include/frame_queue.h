#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include "frame.h"

#include <pthread.h>
#include <stddef.h>

typedef struct {
    frame_t *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    unsigned long dropped_frames;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
} frame_queue_t;

int frame_queue_init(frame_queue_t *queue, size_t capacity);
void frame_queue_destroy(frame_queue_t *queue);

int frame_queue_push(frame_queue_t *queue, const frame_t *frame);
int frame_queue_pop(frame_queue_t *queue, frame_t *out);
void frame_queue_close(frame_queue_t *queue);

size_t frame_queue_size(frame_queue_t *queue);
unsigned long frame_queue_dropped(frame_queue_t *queue);

#endif
