#include "netpbm.h"
#include "parse.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Consume precisely one token delimiter. In raw formats, bytes following the
 * Maxval delimiter belong to the raster, even if they look like whitespace. */
static int read_token(FILE *file, char *token, size_t capacity, int *delimiter)
{
	int c;
	do {
		c = fgetc(file);
		if (c == '#') {
			while (c != '\n' && c != '\r' && c != EOF)
				c = fgetc(file);
		}
	} while (c != EOF && isspace((unsigned char)c));
	if (c == EOF)
		return 0;

	size_t length = 0;
	while (c != EOF && !isspace((unsigned char)c) && c != '#') {
		if (c == 0 || length + 1 >= capacity)
			return 0;
		token[length++] = (char)c;
		c = fgetc(file);
	}
	token[length] = '\0';
	*delimiter = c;
	if (c == '#') {
		while (c != '\n' && c != '\r' && c != EOF)
			c = fgetc(file);
	}
	return length != 0 && !ferror(file);
}

static int read_number(FILE *file, int *value, int *delimiter)
{
	char token[64];
	return read_token(file, token, sizeof(token), delimiter) &&
		isdigit((unsigned char)token[0]) && parse_int(token, value);
}

static int read_header(FILE *file, image *img, int *raw)
{
	int delimiter;
	char magic[3];
	size_t pixels, bytes;
	if (!read_token(file, magic, sizeof(magic), &delimiter) ||
		(strcmp(magic, "P2") && strcmp(magic, "P3") &&
		 strcmp(magic, "P5") && strcmp(magic, "P6")))
		return 0;
	img->format = (magic[1] == '3' || magic[1] == '6') ? IMAGE_RGB : IMAGE_GRAY;
	if (!read_number(file, &img->width, &delimiter) ||
		!read_number(file, &img->height, &delimiter) ||
		!read_number(file, &img->max_val, &delimiter) ||
		img->max_val < 1 || img->max_val > 255 ||
		!image_sizes(img, &pixels, &bytes))
		return 0;
	*raw = magic[1] == '5' || magic[1] == '6';
	return !*raw ||
		(delimiter != EOF && isspace((unsigned char)delimiter));
}

static int read_data(FILE *file, image *img, int raw)
{
	size_t pixels, bytes;
	if (!image_sizes(img, &pixels, &bytes))
		return 0;
	int color = image_is_color(img);
	unsigned char *gray = img->data;
	rgb *colors = img->data;
	for (size_t i = 0; i < pixels; i++) {
		unsigned char sample[3];
		int channels = color ? 3 : 1;
		for (int channel = 0; channel < channels; channel++) {
			int value, delimiter;
			if (raw) {
				value = fgetc(file);
				if (value == EOF)
					return 0;
			} else if (!read_number(file, &value, &delimiter)) {
				return 0;
			}
			if (value < 0 || value > img->max_val)
				return 0;
			sample[channel] = (unsigned char)value;
		}
		if (color)
			colors[i] = (rgb){sample[0], sample[1], sample[2]};
		else
			gray[i] = sample[0];
	}
	/* Raw PNM may contain subsequent images; this editor loads the first.
	 * Plain PNM contains one image, with only whitespace/comments afterward. */
	if (!raw) {
		int c;
		while ((c = fgetc(file)) != EOF) {
			if (c == '#') {
				while (c != '\n' && c != '\r' && c != EOF)
					c = fgetc(file);
			} else if (!isspace((unsigned char)c)) {
				return 0;
			}
		}
	}
	return !ferror(file);
}

image_status netpbm_load(const char *filename, image *out)
{
	if (!filename || !out)
		return IMAGE_INVALID;
	FILE *file = fopen(filename, "rb");
	if (!file)
		return IMAGE_IO;
	image loaded = {0};
	int raw;
	image_status status = IMAGE_INVALID;
	if (read_header(file, &loaded, &raw)) {
		status = image_create(&loaded, loaded.format, loaded.width,
							  loaded.height, loaded.max_val);
		if (status == IMAGE_OK && !read_data(file, &loaded, raw))
			status = IMAGE_INVALID;
	}
	if (fclose(file) != 0)
		status = IMAGE_IO;
	if (status == IMAGE_OK) {
		image_destroy(out);
		*out = loaded;
	} else {
		image_destroy(&loaded);
	}
	return status;
}

static int write_image(FILE *file, const image *img, int ascii)
{
	int color = image_is_color(img);
	const char *type = color ? (ascii ? "P3" : "P6") : (ascii ? "P2" : "P5");
	size_t pixels, bytes;
	if (!image_sizes(img, &pixels, &bytes) ||
		fprintf(file, "%s\n%d %d\n%d\n", type, img->width,
				img->height, img->max_val) < 0)
		return 0;
	const unsigned char *gray = img->data;
	const rgb *colors = img->data;
	for (size_t i = 0; i < pixels; i++) {
		if (ascii) {
			int ok = color ? fprintf(file, "%u %u %u ", (unsigned)colors[i].r,
						(unsigned)colors[i].g, (unsigned)colors[i].b) >= 0 :
						fprintf(file, "%u ", (unsigned)gray[i]) >= 0;
			if (!ok || ((i + 1) % (size_t)img->width == 0 && fputc('\n', file) == EOF))
				return 0;
		} else if (color) {
			unsigned char samples[3] = {colors[i].r, colors[i].g, colors[i].b};
			if (fwrite(samples, 1, sizeof(samples), file) != sizeof(samples))
				return 0;
		} else if (fputc(gray[i], file) == EOF) {
			return 0;
		}
	}
	return !ferror(file);
}

image_status netpbm_save(const char *filename, const image *img, int ascii)
{
	if (!filename || !*filename || !image_valid(img))
		return IMAGE_INVALID;
	struct stat existing;
	int exists = lstat(filename, &existing) == 0;
	if ((exists && !S_ISREG(existing.st_mode)) || (!exists && errno != ENOENT))
		return IMAGE_IO;
	const char *slash = strrchr(filename, '/');
	size_t prefix = slash ? (size_t)(slash - filename) + 1 : 0;
	const char suffix[] = ".image-editor-XXXXXX";
	if (prefix > SIZE_MAX - sizeof(suffix))
		return IMAGE_INVALID;
	char *temporary = malloc(prefix + sizeof(suffix));
	if (!temporary)
		return IMAGE_NOMEM;
	memcpy(temporary, filename, prefix);
	memcpy(temporary + prefix, suffix, sizeof(suffix));
	int descriptor = mkstemp(temporary);
	if (descriptor < 0) {
		free(temporary);
		return IMAGE_IO;
	}
	FILE *file = fdopen(descriptor, "wb");
	int ok = 0;
	if (file) {
		ok = write_image(file, img, ascii);
		if (ok && fflush(file) != 0)
			ok = 0;
		if (ok && exists && fchmod(descriptor, existing.st_mode & 0777) != 0)
			ok = 0;
		if (ok && fsync(descriptor) != 0)
			ok = 0;
		if (fclose(file) != 0)
			ok = 0;
	} else {
		(void)close(descriptor);
	}
	if (ok && rename(temporary, filename) != 0)
		ok = 0;
	if (!ok)
		(void)unlink(temporary);
	free(temporary);
	return ok ? IMAGE_OK : IMAGE_IO;
}
