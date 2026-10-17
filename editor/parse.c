#include "parse.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int parse_int(const char *text, int *value)
{
	if (!text || !value || !*text || isspace((unsigned char)*text))
		return 0;
	char *end;
	errno = 0;
	long parsed = strtol(text, &end, 10);
	if (end == text || *end != '\0' || errno == ERANGE ||
		parsed < INT_MIN || parsed > INT_MAX)
		return 0;
	*value = (int)parsed;
	return 1;
}

