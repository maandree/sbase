/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdlib.h>

/* a := b */

void
zset(z_t a, z_t b)
{
	size_t i;
	if (a->alloced < b->alloced) {
		a->alloced = b->alloced;
		a->chars = realloc(a->chars, b->alloced * sizeof(*(a->chars)));
	}
	a->sign = b->sign;
	if (a->sign) {
		i = a->used = b->used;
		while (i--)
			a->chars[i] = b->chars[i];
	}
}

void
zseti(z_t a, long long int b)
{
	if (b >= 0) {
		zsetu(a, (unsigned long long int)b);
	} else {
		zsetu(a, (unsigned long long int)-b);
		a->sign = -1;
	}
}

void
zsetu(z_t a, unsigned long long int b)
{
	if (!b) {
		a->sign = 0;
		return;
	}
	if (a->alloced < 2) {
		a->alloced = 2;
		a->chars = realloc(a->chars, 2 * sizeof(*(a->chars)));
	}
	a->sign = 1;
	a->chars[0] = (uint32_t)b;
	b >>= 32;
	if (b)
		a->chars[1] = (uint32_t)b;
	a->used = b ? 2 : 1;
}
