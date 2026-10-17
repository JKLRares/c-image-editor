#ifndef IMAGE_EDITOR_PARSE_H
#define IMAGE_EDITOR_PARSE_H
/* Complete signed decimal tokens only; overflow and suffixes are rejected. */
int parse_int(const char *text, int *value);
#endif
