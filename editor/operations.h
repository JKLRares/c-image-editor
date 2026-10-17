#ifndef IMAGE_EDITOR_OPERATIONS_H
#define IMAGE_EDITOR_OPERATIONS_H
#include "image.h"
typedef enum { FILTER_SHARPEN, FILTER_EDGE, FILTER_BLUR, FILTER_GAUSSIAN } image_filter;

/* Images/selection are borrowed. On failure they remain unchanged.
 * Successful crop/full quarter-turns may replace data and reset selection.
 * Filters and 180-degree rotations reuse the existing image allocation. */
image_status image_crop(image *img, selection *sel);
image_status image_apply(image *img, const selection *sel, image_filter filter);
image_status image_equalize(image *img);
image_status image_histogram(const image *img, int max_stars, int bins, int stars[256]);
image_status image_rotate(image *img, selection *sel, int angle);
#endif
