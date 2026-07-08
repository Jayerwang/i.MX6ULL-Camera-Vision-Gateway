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

static void draw_rgb565_bars(framebuffer_t *fb)
{
    static const uint16_t colors[] = {
        0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff, 0xffff, 0x0000
    };
    unsigned int x;
    unsigned int y;
    unsigned int visible_width = fb->var.xres;
    unsigned int visible_height = fb->var.yres;
    unsigned int bytes_per_pixel = fb->var.bits_per_pixel / 8;

    for (y = 0; y < visible_height; ++y) {
        unsigned char *row = fb->mem + (size_t)y * fb->fix.line_length;

        for (x = 0; x < visible_width; ++x) {
            unsigned int bar = x * (sizeof(colors) / sizeof(colors[0])) / visible_width;
            uint16_t *pixel = (uint16_t *)(row + (size_t)x * bytes_per_pixel);

            *pixel = colors[bar];
        }
    }
}

static void draw_xrgb8888_bars(framebuffer_t *fb)
{
    static const uint32_t colors[] = {
        0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00,
        0x00ff00ff, 0x0000ffff, 0x00ffffff, 0x00000000
    };
    unsigned int x;
    unsigned int y;
    unsigned int visible_width = fb->var.xres;
    unsigned int visible_height = fb->var.yres;
    unsigned int bytes_per_pixel = fb->var.bits_per_pixel / 8;

    for (y = 0; y < visible_height; ++y) {
        unsigned char *row = fb->mem + (size_t)y * fb->fix.line_length;

        for (x = 0; x < visible_width; ++x) {
            unsigned int bar = x * (sizeof(colors) / sizeof(colors[0])) / visible_width;
            uint32_t *pixel = (uint32_t *)(row + (size_t)x * bytes_per_pixel);

            *pixel = colors[bar];
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

    memset(fb.mem, 0, fb.mem_size);

    if (fb.var.bits_per_pixel == 16) {
        draw_rgb565_bars(&fb);
    } else if (fb.var.bits_per_pixel == 32) {
        draw_xrgb8888_bars(&fb);
    } else {
        fprintf(stderr, "Unsupported framebuffer bpp: %u\n", fb.var.bits_per_pixel);
        framebuffer_close(&fb);
        return -1;
    }

    printf("Framebuffer test pattern written\n");
    framebuffer_close(&fb);
    return 0;
}
