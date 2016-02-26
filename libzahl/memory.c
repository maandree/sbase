/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

void /* Prepare libzahl for use. */
zsetup(jmp_buf env)
{
	(void) env;
}

void /* Free resources of libzahl */
zunsetup(void)
{
	/* nothing for now */
}

void /* Prepare a for use. */
zinit(z_t a)
{
	(void) a;
}

void /* Free resources in a. */
zfree(z_t a)
{
	(void) a;
}

size_t /* ⌊log₂ |a|⌋ + 1, 1 if a = 0 */
zbits(z_t a)
{
	unsigned long long t = *a;
	size_t r = 0;
	if (t == 0)
		return 1;
	while (t) r++, t >>= 1;
	return r;
}

size_t /* Index of first set bit, SIZE_MAX if non are set. */
zlsb(z_t a)
{
	unsigned long long t = *a;
	size_t r = 0;
	if (t == 0)
		return ~0;
	while (!(t & 1)) r++, t >>= 1;
	return r;
}
