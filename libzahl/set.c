/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

/* a := b */

void
zset(z_t a, z_t b)
{
	*a = *b;
}

void
zseti(z_t a, long long int b)
{
	*a = b;
}

void
zsetu(z_t a, unsigned long long int b)
{
	*a = b;
}
