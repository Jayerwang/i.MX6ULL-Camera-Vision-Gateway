#ifndef JPEG_DECODER_H
#define JPEG_DECODER_H

#include <stddef.h>
#include <stdint.h>

int jpeg_decode_to_rgb565(const unsigned char *jpeg_data,
                          size_t jpeg_size,
                          uint16_t **out_pixels,
                          unsigned int *out_width,
                          unsigned int *out_height);

#endif
