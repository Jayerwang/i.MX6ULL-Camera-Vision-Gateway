#include "jpeg_decoder.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBJPEG
#include <jpeglib.h>

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jump_buffer;
} jpeg_error_handler_t;

typedef struct {
    struct jpeg_source_mgr pub;
    const JOCTET *data;
    size_t size;
    boolean start_of_file;
} memory_source_t;

static void jpeg_error_exit(j_common_ptr cinfo)
{
    jpeg_error_handler_t *handler = (jpeg_error_handler_t *)cinfo->err;

    (*cinfo->err->output_message)(cinfo);
    longjmp(handler->jump_buffer, 1);
}

static void memory_init_source(j_decompress_ptr cinfo)
{
    memory_source_t *src = (memory_source_t *)cinfo->src;

    src->start_of_file = TRUE;
}

static boolean memory_fill_input_buffer(j_decompress_ptr cinfo)
{
    static const JOCTET eoi_buffer[2] = { 0xff, JPEG_EOI };

    cinfo->src->next_input_byte = eoi_buffer;
    cinfo->src->bytes_in_buffer = sizeof(eoi_buffer);
    return TRUE;
}

static void memory_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    if (num_bytes <= 0) {
        return;
    }

    if ((size_t)num_bytes > cinfo->src->bytes_in_buffer) {
        memory_fill_input_buffer(cinfo);
        return;
    }

    cinfo->src->next_input_byte += num_bytes;
    cinfo->src->bytes_in_buffer -= num_bytes;
}

static void memory_term_source(j_decompress_ptr cinfo)
{
    (void)cinfo;
}

static void jpeg_memory_source(j_decompress_ptr cinfo,
                               const unsigned char *data,
                               size_t size)
{
    memory_source_t *src;

    if (cinfo->src == NULL) {
        cinfo->src = (struct jpeg_source_mgr *)
            (*cinfo->mem->alloc_small)((j_common_ptr)cinfo,
                                       JPOOL_PERMANENT,
                                       sizeof(memory_source_t));
    }

    src = (memory_source_t *)cinfo->src;
    src->data = data;
    src->size = size;
    src->pub.init_source = memory_init_source;
    src->pub.fill_input_buffer = memory_fill_input_buffer;
    src->pub.skip_input_data = memory_skip_input_data;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = memory_term_source;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = data;
}

static uint16_t rgb888_to_rgb565(unsigned int r, unsigned int g, unsigned int b)
{
    return (uint16_t)(((r & 0xf8) << 8) |
                      ((g & 0xfc) << 3) |
                      ((b & 0xf8) >> 3));
}

int jpeg_decode_to_rgb565(const unsigned char *jpeg_data,
                          size_t jpeg_size,
                          uint16_t **out_pixels,
                          unsigned int *out_width,
                          unsigned int *out_height)
{
    struct jpeg_decompress_struct cinfo;
    jpeg_error_handler_t error_handler;
    JSAMPARRAY scanline;
    uint16_t *volatile pixels = NULL;
    unsigned int width;
    unsigned int height;
    unsigned int row_stride;

    if (jpeg_data == NULL || jpeg_size == 0 ||
        out_pixels == NULL || out_width == NULL || out_height == NULL) {
        return -1;
    }

    *out_pixels = NULL;
    *out_width = 0;
    *out_height = 0;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&error_handler.pub);
    error_handler.pub.error_exit = jpeg_error_exit;
    if (setjmp(error_handler.jump_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        free((void *)pixels);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_memory_source(&cinfo, jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    width = cinfo.output_width;
    height = cinfo.output_height;
    row_stride = width * cinfo.output_components;

    pixels = (uint16_t *)malloc((size_t)width * height * sizeof(*pixels));
    if (pixels == NULL) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    scanline = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                          JPOOL_IMAGE,
                                          row_stride,
                                          1);

    while (cinfo.output_scanline < height) {
        unsigned int y = cinfo.output_scanline;
        unsigned int x;

        jpeg_read_scanlines(&cinfo, scanline, 1);
        for (x = 0; x < width; ++x) {
            unsigned char *rgb = &scanline[0][x * 3];

            ((uint16_t *)pixels)[(size_t)y * width + x] =
                rgb888_to_rgb565(rgb[0], rgb[1], rgb[2]);
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    *out_pixels = (uint16_t *)pixels;
    *out_width = width;
    *out_height = height;
    return 0;
}

#else

int jpeg_decode_to_rgb565(const unsigned char *jpeg_data,
                          size_t jpeg_size,
                          uint16_t **out_pixels,
                          unsigned int *out_width,
                          unsigned int *out_height)
{
    (void)jpeg_data;
    (void)jpeg_size;
    (void)out_pixels;
    (void)out_width;
    (void)out_height;

    fprintf(stderr, "JPEG decoder is not enabled. Rebuild with USE_LIBJPEG=1\n");
    return -1;
}

#endif
