#include "image.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

int image_is_color(const image *img)
{
	return img && img->format == IMAGE_RGB;
}

int image_sizes(const image *img, size_t *pixels, size_t *bytes)
{
	if (!img || !pixels || !bytes || img->width <= 0 || img->height <= 0 ||
		(img->format != IMAGE_GRAY && img->format != IMAGE_RGB))
		return 0;
	size_t width = (size_t)img->width, height = (size_t)img->height;
	size_t pixel_size = image_is_color(img) ? sizeof(rgb) : 1;
	if (width > SIZE_MAX / height)
		return 0;
	*pixels = width * height;
	/* Bound count * int star_limit in uint64_t, including wider-int targets. */
	if (*pixels > INT_MAX || *pixels > UINT64_MAX / (uint64_t)INT_MAX ||
		*pixels > SIZE_MAX / pixel_size)
		return 0;
	*bytes = *pixels * pixel_size;
	return *bytes <= PTRDIFF_MAX;
}

int image_valid(const image *img)
{
	size_t pixels, bytes;
	return img && img->data && img->max_val >= 1 && img->max_val <= 255 &&
		image_sizes(img, &pixels, &bytes);
}

void image_destroy(image *img)
{
	if (img) {
		free(img->data);
		*img = (image){0};
	}
}

image_status image_create(image *img, image_format format, int width, int height,
						  int max_val)
{
	image created = {format, width, height, max_val, NULL};
	size_t pixels, bytes;
	if (!img || max_val < 1 || max_val > 255 ||
		!image_sizes(&created, &pixels, &bytes))
		return IMAGE_INVALID;
	created.data = malloc(bytes);
	if (!created.data)
		return IMAGE_NOMEM;
	image_destroy(img);
	*img = created;
	return IMAGE_OK;
}

selection selection_all(const image *img)
{
	return (selection){0, 0, img->width, img->height};
}

int selection_valid(const image *img, const selection *sel)
{
	return image_valid(img) && sel && sel->x1 >= 0 && sel->y1 >= 0 &&
		sel->x1 < sel->x2 && sel->y1 < sel->y2 &&
		sel->x2 <= img->width && sel->y2 <= img->height;
}

int selection_is_full(const image *img, const selection *sel)
{
	return selection_valid(img, sel) && sel->x1 == 0 && sel->y1 == 0 &&
		sel->x2 == img->width && sel->y2 == img->height;
}

image_status image_select(const image *img, selection *sel,
						  int x1, int y1, int x2, int y2)
{
	if (x1 > x2) {
		int tmp = x1;
		x1 = x2;
		x2 = tmp;
	}
	if (y1 > y2) {
		int tmp = y1;
		y1 = y2;
		y2 = tmp;
	}
	selection chosen = {x1, y1, x2, y2};
	if (!sel || !selection_valid(img, &chosen))
		return IMAGE_INVALID;
	*sel = chosen;
	return IMAGE_OK;
}
