#ifndef FRAMEBUFFER_DISPLAY_H
#define FRAMEBUFFER_DISPLAY_H

#include <stdint.h>

typedef struct framebuffer_display framebuffer_display_t;

int framebuffer_display_open(framebuffer_display_t **out, const char *device);
void framebuffer_display_close(framebuffer_display_t *display);
int framebuffer_display_draw_rgb565(framebuffer_display_t *display,
                                    const uint16_t *pixels,
                                    unsigned int width,
                                    unsigned int height);
int framebuffer_test_pattern(const char *device);

#endif
