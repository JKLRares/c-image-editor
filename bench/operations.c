/* Time deterministic in-memory operations; hash output outside the timed region.
 * BENCH_P0 includes the baseline p0.c supplied through an include directory. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef BENCH_P0
static int quiet_printf(const char *format, ...)
{
	(void)format;
	return 0;
}
static int quiet_puts(const char *text)
{
	(void)text;
	return 0;
}
#define printf quiet_printf
#define puts quiet_puts
#define main p0_editor_main
#include "p0.c"
#undef main
#undef puts
#undef printf
#else
#include "image.h"
#include "operations.h"
#endif

static double cpu_time(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) != 0)
		exit(EXIT_FAILURE);
	return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static uint64_t fingerprint(const image *img)
{
	const rgb *data = img->data;
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t count = (size_t)img->width * (size_t)img->height;
	for (size_t i = 0; i < count; i++) {
		hash = (hash ^ data[i].r) * UINT64_C(1099511628211);
		hash = (hash ^ data[i].g) * UINT64_C(1099511628211);
		hash = (hash ^ data[i].b) * UINT64_C(1099511628211);
	}
	return hash;
}

int main(int argc, char **argv)
{
	if (argc != 4)
		return EXIT_FAILURE;
	int size = atoi(argv[1]), iterations = atoi(argv[2]), operation = atoi(argv[3]);
	if (size < 256 || size > 8192 || iterations < 1 || iterations > 99 ||
		operation < 0 || operation > 2)
		return EXIT_FAILURE;
#ifdef BENCH_P0
	image img = {{'P', '3', '\0'}, size, size, 255, NULL};
	img.data = malloc((size_t)size * (size_t)size * sizeof(rgb));
	if (!img.data)
		return EXIT_FAILURE;
#else
	image img = {0};
	if (image_create(&img, IMAGE_RGB, size, size, 255) != IMAGE_OK)
		return EXIT_FAILURE;
#endif
	rgb *data = img.data;
	for (size_t i = 0; i < (size_t)size * (size_t)size; i++)
		data[i] = (rgb){(unsigned char)(i * 17), (unsigned char)(i * 31),
					   (unsigned char)(i * 47)};
	selection sel = {0, 0, size, size};
	if (operation == 1)
		sel = (selection){1, 1, 129, 129};
	double start = cpu_time();
	for (int i = 0; i < iterations; i++) {
#ifdef BENCH_P0
		if (operation == 0)
			(void)rotate_full_image_2(&img, size, size);
		else
			apply("BLUR", &img, &sel);
#else
		image_status status = operation == 0 ? image_rotate(&img, &sel, 180) :
			image_apply(&img, &sel, FILTER_BLUR);
		if (status != IMAGE_OK)
			return EXIT_FAILURE;
#endif
	}
	double elapsed = cpu_time() - start;
	printf("%.9f %llu\n", elapsed / iterations, (unsigned long long)fingerprint(&img));
#ifdef BENCH_P0
	free_image(&img);
#else
	image_destroy(&img);
#endif
	return EXIT_SUCCESS;
}
