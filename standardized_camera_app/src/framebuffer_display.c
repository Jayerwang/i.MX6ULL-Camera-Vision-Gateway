#include "framebuffer_display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct framebuffer_display {
    int fd;
    unsigned char *mem;
    size_t mem_size;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
};

int framebuffer_display_open(framebuffer_display_t **out, const char *device)
{
    framebuffer_display_t *fb;

    fb = (framebuffer_display_t *)calloc(1, sizeof(*fb));
    if (fb == NULL) {
        return -1;
    }

    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = open(device, O_RDWR);
    if (fb->fd == -1) {
        fprintf(stderr, "Failed to open framebuffer %s: %s\n", device, strerror(errno));
        free(fb);
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) == -1) {
        fprintf(stderr, "FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        close(fb->fd);
        free(fb);
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) == -1) {
        fprintf(stderr, "FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        close(fb->fd);
        free(fb);
        return -1;
    }

    fb->mem_size = fb->fix.smem_len;
    fb->mem = (unsigned char *)mmap(NULL,
                                    fb->mem_size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED,
                                    fb->fd,
                                    0);
    if (fb->mem == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap framebuffer: %s\n", strerror(errno));
        fb->mem = NULL;
        close(fb->fd);
        free(fb);
        return -1;
    }

    if (ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1) {
        fprintf(stderr, "Warning: FBIOBLANK unblank failed: %s\n", strerror(errno));
    }

    *out = fb;
    return 0;
}

void framebuffer_display_close(framebuffer_display_t *fb)
{
    if (fb == NULL) {
        return;
    }
    if (fb->mem != NULL) {
        munmap(fb->mem, fb->mem_size);
    }
    if (fb->fd != -1) {
        close(fb->fd);
    }
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
    free(fb);
}

static unsigned int scale_color(unsigned int value, unsigned int length)
{
    if (length == 0) {
        return 0;
    }
    if (length >= 8) {
        return value << (length - 8);
    }
    return value >> (8 - length);
}

static uint32_t pack_color(const struct fb_var_screeninfo *var,
                           unsigned int r,
                           unsigned int g,
                           unsigned int b)
{
    uint32_t pixel = 0;

    pixel |= scale_color(r, var->red.length) << var->red.offset;
    pixel |= scale_color(g, var->green.length) << var->green.offset;
    pixel |= scale_color(b, var->blue.length) << var->blue.offset;

    if (var->transp.length > 0) {
        pixel |= scale_color(255, var->transp.length) << var->transp.offset;
    }

    return pixel;
}

static void put_pixel(framebuffer_display_t *fb, unsigned int x, unsigned int y, uint32_t color)
{
    unsigned int bytes_per_pixel = fb->var.bits_per_pixel / 8;
    unsigned char *pixel = fb->mem +
                           (size_t)(y + fb->var.yoffset) * fb->fix.line_length +
                           (size_t)(x + fb->var.xoffset) * bytes_per_pixel;

    if (bytes_per_pixel == 2) {
        uint16_t value = (uint16_t)color;
        memcpy(pixel, &value, sizeof(value));
    } else if (bytes_per_pixel == 4) {
        memcpy(pixel, &color, sizeof(color));
    }
}

static void draw_color_bars(framebuffer_display_t *fb)
{
    static const unsigned int colors[][3] = {
        {255, 0, 0},
        {0, 255, 0},
        {0, 0, 255},
        {255, 255, 0},
        {255, 0, 255},
        {0, 255, 255},
        {255, 255, 255},
        {0, 0, 0}
    };
    unsigned int x;
    unsigned int y;
    unsigned int visible_width = fb->var.xres;
    unsigned int visible_height = fb->var.yres;
    unsigned int color_count = sizeof(colors) / sizeof(colors[0]);

    for (y = 0; y < visible_height; ++y) {
        for (x = 0; x < visible_width; ++x) {
            unsigned int bar = x * color_count / visible_width;
            uint32_t pixel = pack_color(&fb->var,
                                        colors[bar][0],
                                        colors[bar][1],
                                        colors[bar][2]);

            put_pixel(fb, x, y, pixel);
        }
    }
}

int framebuffer_display_draw_rgb565(framebuffer_display_t *fb,
                                    const uint16_t *pixels,
                                    unsigned int width,
                                    unsigned int height)
{
    unsigned int x;
    unsigned int y;

    if (fb == NULL || pixels == NULL || width == 0 || height == 0) {
        return -1;
    }
    if (fb->var.bits_per_pixel != 16 && fb->var.bits_per_pixel != 32) {
        fprintf(stderr, "Unsupported framebuffer bpp: %u\n", fb->var.bits_per_pixel);
        return -1;
    }

    for (y = 0; y < fb->var.yres; ++y) {
        unsigned int src_y = y * height / fb->var.yres;

        for (x = 0; x < fb->var.xres; ++x) {
            unsigned int src_x = x * width / fb->var.xres;
            uint16_t rgb565 = pixels[(size_t)src_y * width + src_x];
            unsigned int r = ((rgb565 >> 11) & 0x1f) * 255 / 31;
            unsigned int g = ((rgb565 >> 5) & 0x3f) * 255 / 63;
            unsigned int b = (rgb565 & 0x1f) * 255 / 31;
            uint32_t pixel = pack_color(&fb->var, r, g, b);

            put_pixel(fb, x, y, pixel);
        }
    }

    if (msync(fb->mem, fb->mem_size, MS_SYNC) == -1) {
        fprintf(stderr, "Warning: framebuffer msync failed: %s\n", strerror(errno));
    }

    return 0;
}

int framebuffer_test_pattern(const char *device)
{
    framebuffer_display_t *fb;

    if (framebuffer_display_open(&fb, device) != 0) {
        return -1;
    }

    printf("Framebuffer: %s\n", device);
    printf("Resolution: %ux%u, virtual: %ux%u\n",
           fb->var.xres,
           fb->var.yres,
           fb->var.xres_virtual,
           fb->var.yres_virtual);
    printf("Line length: %u bytes, bpp: %u, memory: %lu bytes\n",
           fb->fix.line_length,
           fb->var.bits_per_pixel,
           (unsigned long)fb->mem_size);
    printf("Color offsets: R%u:%u G%u:%u B%u:%u A%u:%u\n",
           fb->var.red.offset,
           fb->var.red.length,
           fb->var.green.offset,
           fb->var.green.length,
           fb->var.blue.offset,
           fb->var.blue.length,
           fb->var.transp.offset,
           fb->var.transp.length);
    printf("Offset: x=%u, y=%u\n", fb->var.xoffset, fb->var.yoffset);

    memset(fb->mem, 0, fb->mem_size);

    if (fb->var.bits_per_pixel == 16 || fb->var.bits_per_pixel == 32) {
        draw_color_bars(fb);
        if (msync(fb->mem, fb->mem_size, MS_SYNC) == -1) {
            fprintf(stderr, "Warning: framebuffer msync failed: %s\n", strerror(errno));
        }
    } else {
        fprintf(stderr, "Unsupported framebuffer bpp: %u\n", fb->var.bits_per_pixel);
        framebuffer_display_close(fb);
        return -1;
    }

    printf("Framebuffer test pattern written\n");
    framebuffer_display_close(fb);
    return 0;
}
