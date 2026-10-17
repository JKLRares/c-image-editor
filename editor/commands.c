#include "commands.h"
#include "image.h"
#include "netpbm.h"
#include "operations.h"
#include "parse.h"
#include <stdlib.h>
#include <string.h>

#define COMMAND_CAPACITY 256

/* Return 1 for a line, -1 for a discarded oversized/binary line, 0 for EOF. */
static int read_command(FILE *input, char command[COMMAND_CAPACITY])
{
	size_t length = 0;
	int c, invalid = 0;
	while ((c = fgetc(input)) != EOF && c != '\n') {
		if (c == 0 || length == COMMAND_CAPACITY - 1)
			invalid = 1;
		if (!invalid)
			command[length++] = (char)c;
	}
	command[length] = '\0';
	if (invalid)
		return -1;
	return c != EOF || length != 0;
}

static size_t tokenize(char *command, char *tokens[6])
{
	size_t count = 0;
	char *token = strtok(command, " \t\r\v\f");
	while (token && count < 6) {
		tokens[count++] = token;
		token = strtok(NULL, " \t\r\v\f");
	}
	return count;
}

static void operation_error(FILE *out, image_status status)
{
	const char *message = "Invalid command";
	switch (status) {
	case IMAGE_NOMEM: message = "Memory allocation failed"; break;
	case IMAGE_NEEDS_RGB: message = "Easy, Charlie Chaplin"; break;
	case IMAGE_NEEDS_GRAY: message = "Black and white image needed"; break;
	case IMAGE_NOT_SQUARE: message = "The selection must be square"; break;
	case IMAGE_BAD_ANGLE: message = "Unsupported rotation angle"; break;
	default: break;
	}
	fprintf(out, "%s\n", message);
}

static int execute_command(char *command, image *img, selection *sel, FILE *out)
{
	char *tokens[6];
	size_t count = tokenize(command, tokens);
	if (!count) {
		fprintf(out, "Invalid command\n");
		return 1;
	}
	const char *name = tokens[0];
	if (strcmp(name, "LOAD") == 0) {
		if (count != 2) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		image_destroy(img);
		*sel = (selection){0};
		if (netpbm_load(tokens[1], img) == IMAGE_OK) {
			*sel = selection_all(img);
			fprintf(out, "Loaded %s\n", tokens[1]);
		} else {
			fprintf(out, "Failed to load %s\n", tokens[1]);
		}
		return 1;
	}
	if (strcmp(name, "EXIT") == 0) {
		if (count != 1) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		if (!img->data)
			fprintf(out, "No image loaded\n");
		return 0;
	}
	if (strcmp(name, "SAVE") && strcmp(name, "SELECT") && strcmp(name, "CROP") &&
		strcmp(name, "APPLY") && strcmp(name, "EQUALIZE") &&
		strcmp(name, "HISTOGRAM") && strcmp(name, "ROTATE")) {
		fprintf(out, "Invalid command\n");
		return 1;
	}
	if (!img->data) {
		fprintf(out, "No image loaded\n");
		return 1;
	}
	if (strcmp(name, "SAVE") == 0) {
		if ((count != 2 && count != 3) ||
			(count == 3 && strcmp(tokens[2], "ascii"))) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		image_status status = netpbm_save(tokens[1], img, count == 3);
		fprintf(out, status == IMAGE_OK ? "Saved %s\n" : "Failed to save %s\n", tokens[1]);
	} else if (strcmp(name, "SELECT") == 0) {
		int x1, y1, x2, y2;
		if (count == 2 && strcmp(tokens[1], "ALL") == 0) {
			*sel = selection_all(img);
			fprintf(out, "Selected ALL\n");
		} else if (count == 5 && parse_int(tokens[1], &x1) &&
				   parse_int(tokens[2], &y1) && parse_int(tokens[3], &x2) &&
				   parse_int(tokens[4], &y2)) {
			if (image_select(img, sel, x1, y1, x2, y2) == IMAGE_OK)
				fprintf(out, "Selected %d %d %d %d\n", sel->x1, sel->y1, sel->x2, sel->y2);
			else
				fprintf(out, "Invalid set of coordinates\n");
		} else {
			fprintf(out, "Invalid command\n");
		}
	} else if (strcmp(name, "CROP") == 0 || strcmp(name, "EQUALIZE") == 0) {
		if (count != 1) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		int crop = strcmp(name, "CROP") == 0;
		image_status status = crop ? image_crop(img, sel) : image_equalize(img);
		if (status == IMAGE_OK)
			fprintf(out, "%s\n", crop ? "Image cropped" : "Equalize done");
		else
			operation_error(out, status);
	} else if (strcmp(name, "APPLY") == 0) {
		static const char *const names[] = {"SHARPEN", "EDGE", "BLUR", "GAUSSIAN_BLUR"};
		if (count != 2) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		int which = 0;
		while (which < 4 && strcmp(tokens[1], names[which]))
			which++;
		if (which == 4) {
			fprintf(out, "APPLY parameter invalid\n");
			return 1;
		}
		image_status status = image_apply(img, sel, (image_filter)which);
		if (status == IMAGE_OK)
			fprintf(out, "APPLY %s done\n", tokens[1]);
		else
			operation_error(out, status);
	} else if (strcmp(name, "HISTOGRAM") == 0) {
		int limit, bins, stars[256];
		if (count != 3 || !parse_int(tokens[1], &limit) || !parse_int(tokens[2], &bins)) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		image_status status = image_histogram(img, limit, bins, stars);
		if (status == IMAGE_INVALID)
			fprintf(out, "Invalid set of parameters\n");
		else if (status != IMAGE_OK)
			operation_error(out, status);
		else {
			for (int i = 0; i < bins; i++) {
				if (fprintf(out, "%d\t|\t", stars[i]) < 0)
					break;
				for (int j = 0; j < stars[i]; j++) {
					if (fputc('*', out) == EOF)
						return 1;
				}
				if (fputc('\n', out) == EOF)
					break;
			}
		}
	} else if (strcmp(name, "ROTATE") == 0) {
		int angle;
		if (count != 2 || !parse_int(tokens[1], &angle)) {
			fprintf(out, "Invalid command\n");
			return 1;
		}
		image_status status = image_rotate(img, sel, angle);
		if (status == IMAGE_OK)
			fprintf(out, "Rotated %d\n", angle);
		else
			operation_error(out, status);
	}
	return 1;
}

int editor_run(FILE *input, FILE *output)
{
	if (!input || !output)
		return EXIT_FAILURE;
	image img = {0};
	selection sel = {0};
	int status;
	char command[COMMAND_CAPACITY];
	while ((status = read_command(input, command)) != 0) {
		if (ferror(input))
			break;
		if (status < 0)
			fprintf(output, "Invalid command\n");
		else if (!execute_command(command, &img, &sel, output))
			break;
		if (ferror(output))
			break;
	}
	image_destroy(&img);
	int flush_status = fflush(output);
	return ferror(input) || ferror(output) || flush_status == EOF ?
		EXIT_FAILURE : EXIT_SUCCESS;
}
