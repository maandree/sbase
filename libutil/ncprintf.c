/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "../util.h"

/* Variant of printf that removes colors from the format string. Only CSI m is
 * recognized, no other appearances of ESC may occour. %i, %u and %c, but not
 * other %-codes, may be used inside the CSI m sequence, but only if they refer
 * to the very first arguments after the format string. Not thread-safe. */
int
ncprintf(const char *format, ...)
{
	static const char *prev_format = 0;
	static size_t skips = 0;
	static char fmt[128];
	size_t r = 0, w = 0;
	va_list args;
	int rc, escape = 0;

	va_start(args, format);

	if (format == prev_format)
		goto print;

	prev_format = format;

	skips = 0;
	for (;; r++) {
		if (escape) {
			escape = (format[r] != 'm');
			skips += (format[r] == '%');
		} else if (format[r] == '\033') {
			escape = 1;
		} else {
			if (!(fmt[w++] = format[r]))
				break;
		}
	}
	fmt[w] = 0;

print:
	for (r = skips; r--;)
		(void) va_arg(args, int);

	rc = vprintf(fmt, args);
	va_end(args);
	return rc;
}
