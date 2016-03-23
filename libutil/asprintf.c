/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../util.h"

static int xenvasprintf(int, char **, const char *, va_list);

int
asprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = xenvasprintf(-1, strp, fmt, ap);
	va_end(ap);

	return ret;
}

int
easprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = xenvasprintf(1, strp, fmt, ap);
	va_end(ap);

	return ret;
}

int
enasprintf(int status, char **strp, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = xenvasprintf(status, strp, fmt, ap);
	va_end(ap);

	return ret;
}

int
xenvasprintf(int status, char **strp, const char *fmt, va_list ap)
{
	int ret;
	va_list ap2;

	va_copy(ap2, ap);
	ret = vsnprintf(0, 0, fmt, ap2);
	va_end(ap2);
	if (ret < 0) {
		if (status >= 0)
			enprintf(status, "vsnprintf:");
		*strp = 0;
		return -1;
	}

	*strp = malloc(ret + 1);
	if (!*strp) {
		if (status >= 0)
			enprintf(status, "malloc:");
		return -1;
	}

	vsprintf(*strp, fmt, ap);
	return ret;
}
