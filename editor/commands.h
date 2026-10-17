#ifndef IMAGE_EDITOR_COMMANDS_H
#define IMAGE_EDITOR_COMMANDS_H
#include <stdio.h>
/* Borrowed streams remain open. The loop owns/destroys its image. */
int editor_run(FILE *input, FILE *output);
#endif
