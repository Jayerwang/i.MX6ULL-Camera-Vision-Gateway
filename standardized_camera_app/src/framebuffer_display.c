#include "framebuffer_display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    int fd;
    unsigned char *mem;
    size_t mem_size;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
} framebuffer_t;

static int framebuffer_open(framebuffer_t *fb, const char *device)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = open(device, O_RDWR);
    if (fb->fd == -1) {
        fprintf(stderr, "Failed to open framebuffer %s: %s\n", device, strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) == -1) {
        fprintf(stderr, "FBIOGET_FSCREENINFO failed: %s\n", strerror(errno));
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) == -1) {
        fprintf(stderr, "FBIOGET_VSCREENINFO failed: %s\n", strerror(errno));
        close(fb->fd);
        fb->fd = -1;
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
        fb->fd = -1;
        return -1;
    }

    return 0;
}

static void framebuffer_close(framebuffer_t *fb)
{
    if (fb->mem != NULL) {
        munmap(fb->mem, fb->mem_size);
    }
    if (fb->fd != -1) {
        close(fb->fd);
    }
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
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

static void put_pixel(framebuffer_t *fb, unsigned int x, unsigned int y, uint32_t color)
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

static void draw_color_bars(framebuffer_t *fb)
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

int framebuffer_test_pattern(const char *device)
{
    framebuffer_t fb;

    if (framebuffer_open(&fb, device) != 0) {
        return -1;
    }

    printf("Framebuffer: %s\n", device);
    printf("Resolution: %ux%u, virtual: %ux%u\n",
           fb.var.xres,
           fb.var.yres,
           fb.var.xres_virtual,
           fb.var.yres_virtual);
    printf("Line length: %u bytes, bpp: %u, memory: %lu bytes\n",
           fb.fix.line_length,
           fb.var.bits_per_pixel,
           (unsigned long)fb.mem_size);
    printf("Color offsets: R%u:%u G%u:%u B%u:%u A%u:%u\n",
           fb.var.red.offset,
           fb.var.red.length,
           fb.var.green.offset,
           fb.var.green.length,
           fb.var.blue.offset,
           fb.var.blue.length,
           fb.var.transp.offset,
           fb.var.transp.length);
    printf("Offset: x=%u, y=%u\n", fb.var.xoffset, fb.var.yoffset);

    if (ioctl(fb.fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1) {
        fprintf(stderr, "Warning: FBIOBLANK unblank failed: %s\n", strerror(errno));
    }

    memset(fb.mem, 0, fb.mem_size);

    if (fb.var.bits_per_pixel == 16 || fb.var.bits_per_pixel == 32) {
        draw_color_bars(&fb);
        if (msync(fb.mem, fb.mem_size, MS_SYNC) == -1) {
            fprintf(stderr, "Warning: framebuffer msync failed: %s\n", strerror(errno));
        }
    } else {
        fprintf(stderr, "Unsupported framebuffer bpp: %u\n", fb.var.bits_per_pixel);
        framebuffer_close(&fb);
        return -1;
    }

    printf("Framebuffer test pattern written\n");
    framebuffer_close(&fb);
    return 0;
}
