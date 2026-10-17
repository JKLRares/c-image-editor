/* Inject allocation and I/O failures through GNU linker interposition. */
#include "image.h"
#include "commands.h"
#include "netpbm.h"
#include "operations.h"
#include "parse.h"
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_allocation, fail_close, fail_write, fail_rename;
static int fail_temporary, fail_fdopen, fail_flush, fail_sync, fail_mode;
static size_t allocated_bytes;

void *__real_malloc(size_t bytes);
int __real_fclose(FILE *file);
size_t __real_fwrite(const void *data, size_t size, size_t count, FILE *file);
int __real_rename(const char *old, const char *new_name);
int __real_mkstemp(char *name);
FILE *__real_fdopen(int descriptor, const char *mode);
int __real_fflush(FILE *file);
int __real_fsync(int descriptor);
int __real_fchmod(int descriptor, mode_t mode);
void *__wrap_malloc(size_t bytes);
int __wrap_fclose(FILE *file);
size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *file);
int __wrap_rename(const char *old, const char *new_name);
int __wrap_mkstemp(char *name);
FILE *__wrap_fdopen(int descriptor, const char *mode);
int __wrap_fflush(FILE *file);
int __wrap_fsync(int descriptor);
int __wrap_fchmod(int descriptor, mode_t mode);

void *__wrap_malloc(size_t bytes)
{
	allocated_bytes += bytes;
	if (fail_allocation) { fail_allocation = 0; return NULL; }
	return __real_malloc(bytes);
}
int __wrap_fclose(FILE *file)
{
	int result = __real_fclose(file);
	if (fail_close) { fail_close = 0; return EOF; }
	return result;
}
size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *file)
{
	if (fail_write) { fail_write = 0; return 0; }
	return __real_fwrite(data, size, count, file);
}
int __wrap_rename(const char *old, const char *new_name)
{
	if (fail_rename) { fail_rename = 0; return -1; }
	return __real_rename(old, new_name);
}
int __wrap_mkstemp(char *name)
{
	if (fail_temporary) { fail_temporary = 0; return -1; }
	return __real_mkstemp(name);
}
FILE *__wrap_fdopen(int descriptor, const char *mode)
{
	if (fail_fdopen) { fail_fdopen = 0; return NULL; }
	return __real_fdopen(descriptor, mode);
}
int __wrap_fflush(FILE *file)
{
	if (fail_flush) { fail_flush = 0; return EOF; }
	return __real_fflush(file);
}
int __wrap_fsync(int descriptor)
{
	if (fail_sync) { fail_sync = 0; return -1; }
	return __real_fsync(descriptor);
}
int __wrap_fchmod(int descriptor, mode_t mode)
{
	if (fail_mode) { fail_mode = 0; return -1; }
	return __real_fchmod(descriptor, mode);
}

static image fixture(image_format format)
{
	image img = {0};
	assert(image_create(&img, format, 3, 3, 255) == IMAGE_OK);
	size_t pixels, bytes;
	assert(image_sizes(&img, &pixels, &bytes));
	memset(img.data, 7, bytes);
	return img;
}

static void assert_no_temporary(void)
{
	DIR *directory = opendir(".");
	assert(directory);
	struct dirent *entry;
	while ((entry = readdir(directory)))
		assert(strncmp(entry->d_name, ".image-editor-", 14) != 0);
	assert(closedir(directory) == 0);
}

static int descriptor_count(void)
{
	DIR *directory = opendir("/proc/self/fd");
	if (!directory)
		return -1;
	int count = 0;
	while (readdir(directory))
		count++;
	assert(closedir(directory) == 0);
	return count;
}

static void assert_old_destination(void)
{
	char buffer[16] = {0};
	FILE *file = fopen("destination.pnm", "rb");
	assert(file);
	assert(fread(buffer, 1, sizeof(buffer), file) == 8);
	assert(memcmp(buffer, "ORIGINAL", 8) == 0);
	assert(fclose(file) == 0);
	assert_no_temporary();
}

int main(void)
{
	FILE *file = fopen("fixture.pgm", "wb");
	assert(file && fputs("P2\n1 1\n255\n7\n", file) >= 0 && fclose(file) == 0);
	image img = fixture(IMAGE_GRAY);
	void *original = img.data;
	fail_allocation = 1;
	assert(image_create(&img, IMAGE_RGB, 4, 4, 255) == IMAGE_NOMEM);
	assert(!fail_allocation && img.data == original && img.width == 3);
	fail_allocation = 1;
	assert(netpbm_load("fixture.pgm", &img) == IMAGE_NOMEM);
	assert(!fail_allocation && img.data == original);
	fail_close = 1;
	assert(netpbm_load("fixture.pgm", &img) == IMAGE_IO);
	assert(!fail_close && img.data == original);
	file = fopen("truncated.pgm", "wb");
	assert(file && fputs("P5\n2 2\n255\n1", file) >= 0 && fclose(file) == 0);
	assert(netpbm_load("truncated.pgm", &img) == IMAGE_INVALID);
	assert(img.data == original && img.width == 3);
	assert(netpbm_load("fixture.pgm", &img) == IMAGE_OK);
	assert(img.width == 1 && img.height == 1 && *(unsigned char *)img.data == 7);
	image_destroy(&img);

	for (int color = 0; color <= 1; color++) {
		img = fixture(color ? IMAGE_RGB : IMAGE_GRAY);
		selection sel = selection_all(&img);
		original = img.data;
		size_t pixels, bytes;
		assert(image_sizes(&img, &pixels, &bytes));
		unsigned char *snapshot = malloc(bytes);
		assert(snapshot);
		memcpy(snapshot, original, bytes);
		fail_allocation = 1;
		assert(image_crop(&img, &sel) == IMAGE_NOMEM);
		assert(!fail_allocation && img.data == original && img.width == 3 && sel.x2 == 3);
		for (int angle = 90; angle <= 270; angle += 180) {
			fail_allocation = 1;
			assert(image_rotate(&img, &sel, angle) == IMAGE_NOMEM);
			assert(!fail_allocation && img.data == original);
			assert(img.width == 3 && memcmp(snapshot, img.data, bytes) == 0);
		}
		allocated_bytes = 0;
		assert(image_rotate(&img, &sel, 180) == IMAGE_OK);
		assert(allocated_bytes == 0 && img.data == original);
		if (color) {
			fail_allocation = 1;
			assert(image_apply(&img, &sel, FILTER_BLUR) == IMAGE_NOMEM);
			assert(!fail_allocation && img.data == original);
			allocated_bytes = 0;
			assert(image_apply(&img, &sel, FILTER_BLUR) == IMAGE_OK);
			assert(allocated_bytes == sizeof(rgb) && img.data == original);
		}
		assert(memcmp(snapshot, img.data, bytes) == 0);
		selection invalid = {0, 0, 4, 3};
		assert(image_crop(&img, &invalid) == IMAGE_INVALID);
		assert(image_rotate(&img, &invalid, 90) == IMAGE_INVALID);
		assert(img.data == original);
		free(snapshot);
		image_destroy(&img);
		image_destroy(&img);
		assert(!img.data && img.width == 0 && img.height == 0);
	}

	img = fixture(IMAGE_RGB);
	file = fopen("destination.pnm", "wb");
	assert(file && fputs("ORIGINAL", file) >= 0 && fclose(file) == 0);
	int *failures[] = {&fail_allocation, &fail_temporary, &fail_fdopen, &fail_write,
		&fail_flush, &fail_sync, &fail_close, &fail_mode, &fail_rename};
	int descriptors_before = descriptor_count();
	for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
		*failures[i] = 1;
		assert(netpbm_save("destination.pnm", &img, 0) != IMAGE_OK);
		assert(!*failures[i]);
		assert_old_destination();
		*failures[i] = 1;
		image_status status = netpbm_save("new.pnm", &img, 0);
		/* Mode preservation is only needed for existing destinations. */
		if (failures[i] == &fail_mode) {
			assert(status == IMAGE_OK && fail_mode == 1);
			fail_mode = 0;
			assert(unlink("new.pnm") == 0);
		} else {
			assert(status != IMAGE_OK && !*failures[i]);
			assert(access("new.pnm", F_OK) != 0);
		}
		assert_no_temporary();
	}
	assert(descriptor_count() == descriptors_before);
	assert(netpbm_save("destination.pnm", &img, 0) == IMAGE_OK);
	assert_no_temporary();
	image_destroy(&img);

	FILE *commands = tmpfile(), *output = tmpfile();
	assert(commands && output);
	assert(fputs("LOAD fixture.pgm\nEXIT\n", commands) >= 0);
	rewind(commands);
	assert(editor_run(commands, output) == EXIT_SUCCESS);
	/* Borrowed streams must remain open and reusable after the session. */
	assert(fseek(commands, 0, SEEK_SET) == 0);
	assert(fseek(output, 0, SEEK_SET) == 0);
	char message[64] = {0};
	const char expected[] = "Loaded fixture.pgm\n";
	assert(fread(message, 1, sizeof(message), output) == strlen(expected));
	assert(strcmp(message, expected) == 0);
	assert(editor_run(NULL, output) == EXIT_FAILURE);
	assert(fclose(commands) == 0 && fclose(output) == 0);

	size_t pixels, bytes;
	img = (image){IMAGE_RGB, INT_MAX, INT_MAX, 255, NULL};
	assert(!image_sizes(&img, &pixels, &bytes));
	img.width = -1;
	assert(!image_sizes(&img, &pixels, &bytes));
	int number;
	assert(parse_int("-2147483648", &number) && number == INT_MIN);
	assert(parse_int("2147483647", &number) && number == INT_MAX);
	assert(!parse_int("2147483648", &number));
	assert(!parse_int("-2147483649", &number));
	assert(!parse_int("90junk", &number));
	puts("Fault injection assertions passed");
	return 0;
}
