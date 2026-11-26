#include "filters/filters.h"
#include "common/image_job.h"

#include <errno.h>
#include <math.h>
#include <string.h>

unsigned filters_channels(unsigned filter) {
    if (filter == FILTER_SOBEL)
        return 1;
    if (filter == FILTER_BLUR || filter == FILTER_SHARPEN)
        return 3;
    return 0;
}

int filters_resize_rows(const uint8_t *input, uint8_t *output, uint32_t sw, uint32_t sh, uint32_t w,
                        uint32_t h, uint32_t top, uint32_t source_rows, uint32_t first,
                        uint32_t rows) {
    if (!input || !output || input == output || !image_dimensions_valid(sw, sh) ||
        !image_dimensions_valid(w, h) || first >= h || !rows || rows > h - first || top >= sh ||
        !source_rows || source_rows > sh - top) {
        errno = EINVAL;
        return -1;
    }
    for (uint32_t y = 0; y < rows; ++y) {
        uint32_t sy = (uint32_t)((uint64_t)(first + y) * sh / h);
        if (sy < top || sy - top >= source_rows) {
            errno = EINVAL;
            return -1;
        }
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t sx = (uint32_t)((uint64_t)x * sw / w);
            memcpy(output + ((size_t)y * w + x) * 3, input + ((size_t)(sy - top) * sw + sx) * 3, 3);
        }
    }
    return 0;
}

int filters_valid(unsigned filter, unsigned radius, unsigned intensity) {
    return filters_channels(filter) != 0 && radius >= 1 && radius <= FILTER_MAX_RADIUS &&
           intensity <= 16;
}

static int validate(unsigned filter, const uint8_t *in, uint8_t *out, unsigned w, unsigned h,
                    size_t is, size_t os, unsigned r, unsigned intensity) {
    unsigned channels = filters_channels(filter);
    if (!filters_valid(filter, r, intensity) || !in || !out || in == out || w == 0 || h == 0 ||
        SIZE_MAX / w < 3 || is < (size_t)w * 3 || os < (size_t)w * channels || is > SIZE_MAX / h ||
        os > SIZE_MAX / h) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* Same Pascal rows and derivative construction as the original Sobel code. */
static void pascal(int *row, unsigned length) {
    memset(row, 0, length * sizeof(*row));
    row[0] = 1;
    for (unsigned n = 1; n < length; ++n)
        for (unsigned i = n; i != 0; --i)
            row[i] += row[i - 1];
}

int filters_apply_range(unsigned filter, const uint8_t *input, uint8_t *output, unsigned w,
                        unsigned h, size_t is, size_t os, unsigned r, unsigned intensity,
                        size_t first, size_t count) {
    if (validate(filter, input, output, w, h, is, os, r, intensity) < 0)
        return -1;
    unsigned iw = w > 2 * r ? w - 2 * r : 0;
    unsigned ih = h > 2 * r ? h - 2 * r : 0;
    size_t area = (size_t)iw * ih;
    if (first > area || count > area - first) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0)
        return 0;
    unsigned k = 2 * r + 1;
    int smooth[2 * FILTER_MAX_RADIUS + 1] = {0};
    int binomial[2 * FILTER_MAX_RADIUS] = {0};
    int derivative[2 * FILTER_MAX_RADIUS + 1] = {0};
    if (filter == FILTER_SOBEL) {
        pascal(smooth, k);
        pascal(binomial, k - 1);
        for (unsigned i = 0; i < k; ++i)
            derivative[i] = (i == 0 ? 0 : binomial[i - 1]) - (i == k - 1 ? 0 : binomial[i]);
    }
    for (size_t p = first; p < first + count; ++p) {
        unsigned x = r + (unsigned)(p % iw);
        unsigned y = r + (unsigned)(p / iw);
        int sum[3] = {0};
        double gx = 0, gy = 0;
        for (unsigned dy = 0; dy < k; ++dy) {
            for (unsigned dx = 0; dx < k; ++dx) {
                const uint8_t *pixel = input + (size_t)(y + dy - r) * is + (size_t)(x + dx - r) * 3;
                if (filter == FILTER_SOBEL) {
                    int gray = (int)(0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2]);
                    gx += gray * derivative[dx] * smooth[dy];
                    gy += gray * smooth[dx] * derivative[dy];
                } else {
                    for (unsigned c = 0; c < 3; ++c)
                        sum[c] += pixel[c];
                }
            }
        }
        if (filter == FILTER_SOBEL) {
            double magnitude = sqrt(gx * gx + gy * gy);
            output[(size_t)y * os + x] = (uint8_t)(magnitude > 255 ? 255 : magnitude);
        } else {
            for (unsigned c = 0; c < 3; ++c) {
                int value = sum[c] / (int)(k * k);
                if (filter == FILTER_SHARPEN) {
                    int original = input[(size_t)y * is + (size_t)x * 3 + c];
                    value = original + (int)intensity * (original - value);
                }
                output[(size_t)y * os + (size_t)x * 3 + c] = (uint8_t)(value < 0     ? 0
                                                                       : value > 255 ? 255
                                                                                     : value);
            }
        }
    }
    return 0;
}

int filters_apply(unsigned filter, const uint8_t *input, uint8_t *output, unsigned w, unsigned h,
                  size_t is, size_t os, unsigned r, unsigned intensity) {
    if (validate(filter, input, output, w, h, is, os, r, intensity) < 0)
        return -1;
    unsigned channels = filters_channels(filter);
    for (unsigned y = 0; y < h; ++y) {
        if (channels == 3)
            memcpy(output + (size_t)y * os, input + (size_t)y * is, (size_t)w * 3);
        else
            memset(output + (size_t)y * os, 0, w);
    }
    size_t iw = w > 2 * r ? w - 2 * r : 0;
    size_t ih = h > 2 * r ? h - 2 * r : 0;
    return filters_apply_range(filter, input, output, w, h, is, os, r, intensity, 0, iw * ih);
}
