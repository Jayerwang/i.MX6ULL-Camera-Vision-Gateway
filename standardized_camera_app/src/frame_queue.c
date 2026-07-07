#include "frame_queue.h"

#include <stdlib.h>
#include <string.h>

void frame_release(frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

static int frame_copy(frame_t *dst, const frame_t *src)
{
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->data = NULL;

    if (src->size == 0) {
        return 0;
    }

    dst->data = (unsigned char *)malloc(src->size);
    if (dst->data == NULL) {
        memset(dst, 0, sizeof(*dst));
        return -1;
    }

    memcpy(dst->data, src->data, src->size);
    return 0;
}

int frame_queue_init(frame_queue_t *queue, size_t capacity)
{
    if (queue == NULL || capacity == 0) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));
    queue->items = (frame_t *)calloc(capacity, sizeof(frame_t));
    if (queue->items == NULL) {
        return -1;
    }

    queue->capacity = capacity;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->items);
        memset(queue, 0, sizeof(*queue));
        return -1;
    }
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        memset(queue, 0, sizeof(*queue));
        return -1;
    }

    return 0;
}

void frame_queue_destroy(frame_queue_t *queue)
{
    size_t i;

    if (queue == NULL || queue->items == NULL) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    for (i = 0; i < queue->capacity; ++i) {
        frame_release(&queue->items[i]);
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->items);
    memset(queue, 0, sizeof(*queue));
}

int frame_queue_push(frame_queue_t *queue, const frame_t *frame)
{
    size_t index;

    if (queue == NULL || frame == NULL || frame->data == NULL || frame->size == 0) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    if (queue->count == queue->capacity) {
        frame_release(&queue->items[queue->head]);
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        ++queue->dropped_frames;
    }

    index = queue->tail;
    if (frame_copy(&queue->items[index], frame) != 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->tail = (queue->tail + 1) % queue->capacity;
    ++queue->count;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int frame_queue_pop(frame_queue_t *queue, frame_t *out)
{
    if (queue == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->closed) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *out = queue->items[queue->head];
    memset(&queue->items[queue->head], 0, sizeof(queue->items[queue->head]));
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

void frame_queue_close(frame_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    queue->closed = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

size_t frame_queue_size(frame_queue_t *queue)
{
    size_t count;

    if (queue == NULL) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);
    count = queue->count;
    pthread_mutex_unlock(&queue->mutex);

    return count;
}

unsigned long frame_queue_dropped(frame_queue_t *queue)
{
    unsigned long dropped;

    if (queue == NULL) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);
    dropped = queue->dropped_frames;
    pthread_mutex_unlock(&queue->mutex);

    return dropped;
}
