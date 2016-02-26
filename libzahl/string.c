/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

char * /* Write a in decimal onto b. */
zstr(z_t a, char *b)
{
	if (!b)
		b = malloc(3 * sizeof(*a) + 2);
	sprintf(b, "%lli", *a);
	return b;
}

int /* a := b */
zsets(z_t a, const char *b)
{
	if (!*b || !strchr("-+0123456789", *b)) {
		errno = EINVAL;
		return -1;
	}
	errno = 0;
	*a = strtoll(b, NULL, 10);
	return -!!errno;
}
