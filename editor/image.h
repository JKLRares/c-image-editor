#ifndef IMAGE_EDITOR_IMAGE_H
#define IMAGE_EDITOR_IMAGE_H

#include <stddef.h>

typedef struct { unsigned char r, g, b; } rgb;
typedef enum { IMAGE_GRAY = 1, IMAGE_RGB = 3 } image_format;
typedef enum {
	IMAGE_OK, IMAGE_INVALID, IMAGE_NOMEM, IMAGE_IO,
	IMAGE_NEEDS_RGB, IMAGE_NEEDS_GRAY, IMAGE_NOT_SQUARE, IMAGE_BAD_ANGLE
} image_status;

/* Initialize with {0}. Copying the struct does not clone the owned buffer.
 * image_destroy frees the buffer and resets all fields. */
typedef struct {
	image_format format;
	int width, height, max_val;
	void *data;
} image;
typedef struct { int x1, y1, x2, y2; } selection;

int image_sizes(const image *img, size_t *pixels, size_t *bytes);
int image_valid(const image *img);
int image_is_color(const image *img);
void image_destroy(image *img);
/* Replace an initialized image only on success. */
image_status image_create(image *img, image_format format, int width, int height,
						  int max_val);
selection selection_all(const image *img);
int selection_valid(const image *img, const selection *sel);
int selection_is_full(const image *img, const selection *sel);
/* Half-open coordinates; an invalid selection leaves *sel unchanged. */
image_status image_select(const image *img, selection *sel,
						  int x1, int y1, int x2, int y2);
#endif
