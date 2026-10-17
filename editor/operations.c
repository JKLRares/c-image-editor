#include "operations.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned char clamp(int value, int max_val)
{
	if (value < 0)
		return 0;
	if (value > max_val)
		return (unsigned char)max_val;
	return (unsigned char)value;
}

static image_status rotate_selection(image *img, selection *sel, int rotations)
{
	int width = sel->x2 - sel->x1;
	if (width != sel->y2 - sel->y1) {
		return IMAGE_NOT_SQUARE;
	}
	size_t pixel_size = image_is_color(img) ?
						sizeof(rgb) : sizeof(unsigned char);
	unsigned char *data = img->data;
	/* Rotate each square layer through four-way pixel swaps. */
	for (int turn = 0; turn < rotations; turn++) {
		for (int first = 0; first < width / 2; first++) {
			int last = width - 1 - first;
			for (int k = first; k < last; k++) {
				int diff = k - first;
				size_t top = ((size_t)(sel->y1 + first) * (size_t)img->width +
							  (size_t)(sel->x1 + k)) * pixel_size;
				size_t right = ((size_t)(sel->y1 + k) * (size_t)img->width +
								(size_t)(sel->x1 + last)) * pixel_size;
				size_t bottom = ((size_t)(sel->y1 + last) * (size_t)img->width +
								 (size_t)(sel->x1 + last - diff)) * pixel_size;
				size_t left = ((size_t)(sel->y1 + last - diff) * (size_t)img->width +
							   (size_t)(sel->x1 + first)) * pixel_size;
				unsigned char temporary[sizeof(rgb)];
				memcpy(temporary, data + top, pixel_size);
				memcpy(data + top, data + left, pixel_size);
				memcpy(data + left, data + bottom, pixel_size);
				memcpy(data + bottom, data + right, pixel_size);
				memcpy(data + right, temporary, pixel_size);
			}
		}
	}
	return IMAGE_OK;
}

static image_status rotate_full_image_1_3(image *img, selection *sel, int rotations,
						   int og_width, int og_height)
{
	size_t pixels, bytes;
	if (!image_sizes(img, &pixels, &bytes))
		return IMAGE_INVALID;
	void *rotated_img = malloc(bytes);
	if (!rotated_img) {
		return IMAGE_NOMEM;
	}
	int color = image_is_color(img);
	for (int y = 0; y < og_height; y++) {
		for (int x = 0; x < og_width; x++) {
			int dest_x, dest_y;

			if (rotations == 1) {
				dest_x = og_height - 1 - y;
				dest_y = x;
			} else {
				dest_x = y;
				dest_y = og_width - 1 - x;
			}

			size_t source = (size_t)y * (size_t)og_width + (size_t)x;
			size_t target = (size_t)dest_y * (size_t)og_height + (size_t)dest_x;
			if (color)
				((rgb *)rotated_img)[target] = ((rgb *)img->data)[source];
			else
				((unsigned char *)rotated_img)[target] =
					((unsigned char *)img->data)[source];
		}
	}

	image result = {img->format, og_height, og_width, img->max_val, rotated_img};
	image_destroy(img);
	*img = result;

	sel->x1 = 0;
	sel->y1 = 0;
	sel->x2 = img->width;
	sel->y2 = img->height;
	return IMAGE_OK;
}

image_status image_crop(image *img, selection *sel)
{
	if (!selection_valid(img, sel))
		return IMAGE_INVALID;
	image cropped = {0};
	int width = sel->x2 - sel->x1, height = sel->y2 - sel->y1;
	image_status status = image_create(&cropped, img->format, width, height, img->max_val);
	if (status != IMAGE_OK)
		return status;
	size_t pixel_size = image_is_color(img) ? sizeof(rgb) : 1;
	for (int y = 0; y < height; y++) {
		size_t source = ((size_t)(sel->y1 + y) * (size_t)img->width +
						 (size_t)sel->x1) * pixel_size;
		size_t target = (size_t)y * (size_t)width * pixel_size;
		memcpy((unsigned char *)cropped.data + target,
			   (unsigned char *)img->data + source, (size_t)width * pixel_size);
	}
	image_destroy(img);
	*img = cropped;
	*sel = selection_all(img);
	return IMAGE_OK;
}

image_status image_apply(image *img, const selection *sel, image_filter filter)
{
	static const int kernels[4][3][3] = {
		{{0, -1, 0}, {-1, 5, -1}, {0, -1, 0}},
		{{-1, -1, -1}, {-1, 8, -1}, {-1, -1, -1}},
		{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}},
		{{1, 2, 1}, {2, 4, 2}, {1, 2, 1}}
	};
	if (!selection_valid(img, sel) || filter < FILTER_SHARPEN || filter > FILTER_GAUSSIAN)
		return IMAGE_INVALID;
	if (!image_is_color(img))
		return IMAGE_NEEDS_RGB;
	int divisor = filter == FILTER_BLUR ? 9 : filter == FILTER_GAUSSIAN ? 16 : 1;
	int x1 = sel->x1 > 0 ? sel->x1 : 1, y1 = sel->y1 > 0 ? sel->y1 : 1;
	int x2 = sel->x2 < img->width ? sel->x2 : img->width - 1;
	int y2 = sel->y2 < img->height ? sel->y2 : img->height - 1;
	if (x1 >= x2 || y1 >= y2)
		return IMAGE_OK;
	size_t width = (size_t)(x2 - x1), height = (size_t)(y2 - y1);
	/* This rectangle is inside a validated image, so the product fits. */
	rgb *scratch = malloc(width * height * sizeof(rgb));
	if (!scratch)
		return IMAGE_NOMEM;
	const rgb *original = img->data;
	for (int y = y1; y < y2; y++) {
		for (int x = x1; x < x2; x++) {
			int r_sum = 0, g_sum = 0, b_sum = 0;
			for (int ky = -1; ky <= 1; ky++) {
				for (int kx = -1; kx <= 1; kx++) {
					size_t index = (size_t)(y + ky) * (size_t)img->width +
								   (size_t)(x + kx);
					rgb neighbor = original[index];
					int weight = kernels[filter][ky + 1][kx + 1];
					r_sum += neighbor.r * weight;
					g_sum += neighbor.g * weight;
					b_sum += neighbor.b * weight;
				}
			}
			size_t index = (size_t)(y - y1) * width + (size_t)(x - x1);
			scratch[index].r = clamp((r_sum + divisor / 2) / divisor, img->max_val);
			scratch[index].g = clamp((g_sum + divisor / 2) / divisor, img->max_val);
			scratch[index].b = clamp((b_sum + divisor / 2) / divisor, img->max_val);
		}
	}
	/* Commit after computing all pixels, so convolution never reads edits. */
	for (int y = y1; y < y2; y++)
		memcpy((rgb *)img->data + (size_t)y * (size_t)img->width + (size_t)x1,
			   scratch + (size_t)(y - y1) * width, width * sizeof(rgb));
	free(scratch);
	return IMAGE_OK;
}

image_status image_equalize(image *img)
{
	if (!image_valid(img))
		return IMAGE_INVALID;
	if (image_is_color(img))
		return IMAGE_NEEDS_GRAY;
	size_t pixels, bytes;
	(void)image_sizes(img, &pixels, &bytes);
	unsigned char *data = img->data;
	uint64_t frequency[256] = {0};
	unsigned char lookup[256];
	for (size_t i = 0; i < pixels; i++) {
		if (data[i] > img->max_val)
			return IMAGE_INVALID;
		frequency[data[i]]++;
	}
	uint64_t cumulative = 0;
	for (int i = 0; i < 256; i++) {
		cumulative += frequency[i];
		lookup[i] = (unsigned char)((cumulative * (uint64_t)img->max_val +
									(uint64_t)pixels / 2) / (uint64_t)pixels);
	}
	for (size_t i = 0; i < pixels; i++)
		data[i] = lookup[data[i]];
	return IMAGE_OK;
}

image_status image_histogram(const image *img, int max_stars, int bins, int stars[256])
{
	if (!image_valid(img) || !stars)
		return IMAGE_INVALID;
	if (image_is_color(img))
		return IMAGE_NEEDS_GRAY;
	if (max_stars < 0 || bins < 2 || bins > 256 || (bins & (bins - 1)))
		return IMAGE_INVALID;
	size_t pixels, bytes;
	(void)image_sizes(img, &pixels, &bytes);
	const unsigned char *data = img->data;
	uint64_t frequency[256] = {0}, max_frequency = 0;
	for (size_t i = 0; i < pixels; i++) {
		if (data[i] > img->max_val)
			return IMAGE_INVALID;
		unsigned int bin = (unsigned int)data[i] * (unsigned int)bins /
						   ((unsigned int)img->max_val + 1);
		frequency[bin]++;
	}
	for (int i = 0; i < bins; i++) {
		if (frequency[i] > max_frequency)
			max_frequency = frequency[i];
	}
	for (int i = 0; i < bins; i++)
		stars[i] = (int)(frequency[i] * (uint64_t)max_stars / max_frequency);
	return IMAGE_OK;
}

image_status image_rotate(image *img, selection *sel, int angle)
{
	if (!selection_valid(img, sel))
		return IMAGE_INVALID;
	int normalized = (angle % 360 + 360) % 360;
	if (normalized % 90 != 0)
		return IMAGE_BAD_ANGLE;
	if (normalized == 0)
		return IMAGE_OK;
	int turns = normalized / 90;
	if (!selection_is_full(img, sel))
		return rotate_selection(img, sel, turns);
	if (turns == 1 || turns == 3)
		return rotate_full_image_1_3(img, sel, turns, img->width, img->height);
	/* Row-major reversal is a 180-degree rotation, including nonsquare images. */
	size_t pixels, bytes;
	(void)image_sizes(img, &pixels, &bytes);
	if (image_is_color(img)) {
		rgb *data = img->data;
		for (size_t i = 0; i < pixels / 2; i++) {
			rgb temporary = data[i];
			data[i] = data[pixels - 1 - i];
			data[pixels - 1 - i] = temporary;
		}
	} else {
		unsigned char *data = img->data;
		for (size_t i = 0; i < pixels / 2; i++) {
			unsigned char temporary = data[i];
			data[i] = data[pixels - 1 - i];
			data[pixels - 1 - i] = temporary;
		}
	}
	return IMAGE_OK;
}
