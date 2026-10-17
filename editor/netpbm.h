#ifndef IMAGE_EDITOR_NETPBM_H
#define IMAGE_EDITOR_NETPBM_H
#include "image.h"

/* Initialize *out before loading. Failure leaves it unchanged; the command
 * layer handles the CLI's discard-on-failed-LOAD rule. */
image_status netpbm_load(const char *filename, image *out);
/* Write beside the destination, then rename. Reject existing final symlinks and
 * nonregular files. See README.md for durability, metadata and concurrency limits. */
image_status netpbm_save(const char *filename, const image *img, int ascii);
#endif
