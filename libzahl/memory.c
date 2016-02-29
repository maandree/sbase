/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdlib.h>
#include <string.h>

#define LIST_TEMPS  X(a) X(b) X(c) X(d) X(e) X(f) X(g) X(h) X(i) X(j) X(k) X(l)
#define X(x)  z_t zahl_tmp_##x;
LIST_TEMPS
#undef X

void /* Prepare libzahl for use. */
zsetup(jmp_buf env)
{
	(void) env;
#define X(x)  zinit(zahl_tmp_##x);
LIST_TEMPS
#undef X
}

void /* Free resources of libzahl */
zunsetup(void)
{
#define X(x)  zfree(zahl_tmp_##x);
LIST_TEMPS
#undef X
}

void /* Prepare a for use. */
zinit(z_t a)
{
	a->alloced = 0;
	a->chars = 0;
}

void /* Free resources in a. */
zfree(z_t a)
{
	free(a->chars);
	a->alloced = 0;
	a->chars = 0;
}

void /* (a, b) := (b, a) */
zswap(z_t a, z_t b)
{
	z_t t;
	*t = *a;
	*a = *b;
	*b = *t;
}

size_t /* Store a into buffer (if !!buffer), and return number of written bytes. */
zsave(z_t a, void *buffer)
{
	char *buf = buffer;
	if (buffer) {
		*((int *)buf)    = a->sign,    buf += sizeof(int);
		*((size_t *)buf) = a->used,    buf += sizeof(size_t);
		*((size_t *)buf) = a->alloced, buf += sizeof(size_t);
		if (a->sign)
			memcpy(buf, a->chars, a->used * sizeof(*(a->chars)));
	}
	return sizeof(int) + 2 * sizeof(size_t) + (a->sign ? a->used * sizeof(*(a->chars)) : 0);
}

size_t /* Restore a from buffer, and return number of read bytes. */
zload(z_t a, const void *buffer)
{
	char *buf = buffer;
	a->sign    = *((int *)buf),    buf += sizeof(int);
	a->used    = *((size_t *)buf), buf += sizeof(size_t);
	a->alloced = *((size_t *)buf), buf += sizeof(size_t);
	a->chars = realloc(a->chars, a->alloced * sizeof(*(a->chars)));
	if (a->sign)
		memcpy(a->chars, buf, a->used * sizeof(*(a->chars)));
	return sizeof(int) + 2 * sizeof(size_t) + (a->sign ? a->used * sizeof(*(a->chars)) : 0);
}

size_t /* ⌊log₂ |a|⌋ + 1, 1 if a = 0 */
zbits(z_t a)
{
	size_t i = a->used - 1;
	uint32_t x;
	if (!a->sign)
		return 1;
	for (;; i--) {
		if ((x = a->chars[i])) {
			a->used = i + 1;
			for (i *= 32; x; x >>= 1, i++);
			return i;
		}
	}
}

size_t /* Index of first set bit, SIZE_MAX if non are set. */
zlsb(z_t a)
{
	size_t i = 0;
	uint32_t x;
	if (!a->sign)
		return SIZE_MAX;
	for (; i < a->used; i++) {
		if ((x = a->chars[i])) {
			for (i *= 32; !(x & 1); x >>= 1, i++);
			return i;
		}
	}
	abort();
	a->sign = 0;
	return SIZE_MAX;
}
