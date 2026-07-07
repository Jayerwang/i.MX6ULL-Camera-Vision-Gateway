#include "frame_queue.h"

#include <stdio.h>
#include <string.h>

static int make_frame(frame_t *frame, unsigned int sequence, const char *text)
{
    memset(frame, 0, sizeof(*frame));
    frame->data = (unsigned char *)text;
    frame->size = strlen(text) + 1;
    frame->width = 640;
    frame->height = 480;
    frame->pixel_format = 0x47504a4d;
    frame->sequence = sequence;
    frame->timestamp_us = 1000ULL * sequence;
    return 0;
}

static int expect_sequence(frame_queue_t *queue, unsigned int expected)
{
    frame_t out;
    int ret;

    ret = frame_queue_pop(queue, &out);
    if (ret != 1) {
        fprintf(stderr, "pop failed, ret=%d\n", ret);
        return -1;
    }

    if (out.sequence != expected) {
        fprintf(stderr, "expected sequence %u, got %u\n", expected, out.sequence);
        frame_release(&out);
        return -1;
    }

    frame_release(&out);
    return 0;
}

int main(void)
{
    frame_queue_t queue;
    frame_t frame;
    int ret;

    if (frame_queue_init(&queue, 3) != 0) {
        fprintf(stderr, "frame_queue_init failed\n");
        return 1;
    }

    make_frame(&frame, 1, "frame-1");
    ret = frame_queue_push(&queue, &frame);
    make_frame(&frame, 2, "frame-2");
    ret |= frame_queue_push(&queue, &frame);
    make_frame(&frame, 3, "frame-3");
    ret |= frame_queue_push(&queue, &frame);
    make_frame(&frame, 4, "frame-4");
    ret |= frame_queue_push(&queue, &frame);

    if (ret != 0) {
        fprintf(stderr, "push failed\n");
        frame_queue_destroy(&queue);
        return 1;
    }

    if (frame_queue_size(&queue) != 3) {
        fprintf(stderr, "expected queue size 3, got %lu\n",
                (unsigned long)frame_queue_size(&queue));
        frame_queue_destroy(&queue);
        return 1;
    }

    if (frame_queue_dropped(&queue) != 1) {
        fprintf(stderr, "expected dropped_frames 1, got %lu\n",
                frame_queue_dropped(&queue));
        frame_queue_destroy(&queue);
        return 1;
    }

    if (expect_sequence(&queue, 2) != 0 ||
        expect_sequence(&queue, 3) != 0 ||
        expect_sequence(&queue, 4) != 0) {
        frame_queue_destroy(&queue);
        return 1;
    }

    frame_queue_close(&queue);
    memset(&frame, 0, sizeof(frame));
    ret = frame_queue_pop(&queue, &frame);
    if (ret != 0) {
        fprintf(stderr, "expected closed empty queue ret 0, got %d\n", ret);
        frame_queue_destroy(&queue);
        return 1;
    }

    frame_queue_destroy(&queue);
    printf("frame_queue_test passed\n");
    return 0;
}
