#ifndef DIPS_FILTERS_H
#define DIPS_FILTERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum image_filter { FILTER_BLUR = 1, FILTER_SHARPEN = 2, FILTER_SOBEL = 3 };
#define FILTER_MAX_RADIUS 4u

/* BGR input, 3 channels. Sobel produces one channel; other filters produce 3.
 * Radius: 1..4; sharpen intensity: 0..16. channels returns 0 for unknown filters;
 * valid returns a boolean; apply functions return 0 or -1/errno. */
unsigned filters_channels(unsigned filter);
/* Parallel nearest-neighbor resize. Input contains the contiguous source rows
 * [source_first,source_first+source_rows); output contains the requested global
 * destination rows. Global coordinates avoid seams between blocks. */
int filters_resize_rows(const uint8_t *input, uint8_t *output, uint32_t source_width,
                        uint32_t source_height, uint32_t width, uint32_t height,
                        uint32_t source_first, uint32_t source_rows, uint32_t first, uint32_t rows);
int filters_valid(unsigned filter, unsigned radius, unsigned intensity);

/* Initializes borders (input BGR for blur/sharpen, zero for Sobel), then applies
 * the filter. Input/output must be distinct, with valid byte strides/storage. */
int filters_apply(unsigned filter, const uint8_t *input, uint8_t *output, unsigned width,
                  unsigned height, size_t input_stride, size_t output_stride, unsigned radius,
                  unsigned intensity);

/* Applies only [first, first+count) in the flattened interior rectangle.
 * Does not initialize borders; allows the original C++ worker partitioning. */
int filters_apply_range(unsigned filter, const uint8_t *input, uint8_t *output, unsigned width,
                        unsigned height, size_t input_stride, size_t output_stride, unsigned radius,
                        unsigned intensity, size_t first, size_t count);

#ifdef __cplusplus
}
#endif
#endif
