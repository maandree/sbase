/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdlib.h>
#include <string.h>

void /* a := b & c */
zand(z_t a, z_t b, z_t c)
{
	size_t n;
	if (!b->sign || !c->sign) {
		zsetu(a, 0);
		return;
	}
	n = b->used < c->used ? b->used : c->used;
	a->used = 0;
	a->sign = (b->sign > 0 || c->sign > 0) * 2 - 1;

	while (n--) {
		a->chars[n] = b->chars[n] & c->chars[n];
		if (a->chars[n]) {
			a->used = n + 1;
			while (n--)
				a->chars[n] = b->chars[n] & c->chars[n];
			break;
		}
	}
	if (!a->used)
		a->sign = 0;
}

void /* a := b | c */
zor(z_t a, z_t b, z_t c)
{
	size_t n, m;
	if (!b->sign || !c->sign) {
		if (!b->sign && !c->sign) {
			a->sign = 0;
		} else if (!b->sign) {
			if (a != c)
				zset(a, c);
		} else {
			if (a != b)
				zset(a, b);
		}
		return;
	} else if (b == c) {
		if (a != b)
			zset(a, b);
		return;
	}
	n = b->used > c->used ? b->used : c->used;
	m = b->used + c->used - n;
	if (a->alloced < n) {
		a->alloced = n;
		a->chars = realloc(a->chars, a->alloced * sizeof(*(a->chars)));
	}
	memcpy(a->chars + m, n == b->used ? b->chars : c->chars, (m - n) * sizeof(*(a->chars)));
	if (a == b) {
		while (n--)
			a->chars[n] |= c->chars[n];
	} else if (a == c) {
		while (n--)
			a->chars[n] |= b->chars[n];
	} else {
		while (n--)
			a->chars[n] = b->chars[n] | c->chars[n];
	}
	a->sign = b->sign | c->sign;
}

void /* a := b ^ c */
zxor(z_t a, z_t b, z_t c)
{
	size_t n, m;
	if (!b->sign || !c->sign) {
		if (!b->sign && !c->sign) {
			a->sign = 0;
		} else if (!b->sign) {
			zset(a, c);
		} else {
			zset(a, b);
		}
		return;
	} else if (b == c) {
		a->sign = 0;
		return;
	}
	n = b->used > c->used ? b->used : c->used;
	m = b->used + c->used - n;
	if (n == m && !memcmp(b->chars, c->chars, n * sizeof(*(b->chars)))) {
		a->sign = 0;
		return;
	}
	if (a->alloced < n) {
		a->alloced = n;
		a->chars = realloc(a->chars, a->alloced * sizeof(*(a->chars)));
	}
	memcpy(a->chars + m, n == b->used ? b->chars : c->chars, (m - n) * sizeof(*(a->chars)));
	if (a == b) {
		while (n--)
			a->chars[n] ^= c->chars[n];
	} else if (a == c) {
		while (n--)
			a->chars[n] ^= b->chars[n];
	} else {
		while (n--)
			a->chars[n] = b->chars[n] ^ c->chars[n];
	}
	a->sign = 1 - 2 * ((b->sign < 0) ^ (c->sign < 0));
}

void /* a := ~b */
znot(z_t a, z_t b)
{
	size_t bits, n;
	if (!b->sign) {
		a->sign = 0;
		return;
	}
	bits = zbits(b);
	if (a != b)
		zset(a, b);
	a->sign = -b->sign;
	for (n = a->used; n--;) {
		a->chars[n] = ~(a->chars[n]);
	}
	bits &= 31;
	a->chars[a->used - 1] &= ((uint32_t)1 << bits) - 1;
	while (a->used) {
		if (!a->chars[a->used - 1])
			a->used -= 1;
		else
			break;
	}
	if (!a->used)
		a->sign = 0;
}

void /* a := b << c */
zlsh(z_t a, z_t b, size_t c)
{
	size_t i, chars = c >> 5, cc = 31 - (c & 31);
	uint32_t carry[] = {0, 0};
	if (!b->sign) {
		a->sign = 0;
		return;
	}
	if (!c) {
		if (a != b)
			zset(a, b);
		return;
	}
	if (chars && a == b) {
		a->used += chars;
		memmove(a->chars + chars, a->chars, a->used * sizeof(*(a->chars)));
		memset(a->chars, 0, chars * sizeof(*(a->chars)));
	} else {
		a->used = b->used + chars;
		if (a != b) {
			if (a->alloced < a->used) {
				a->alloced = a->used;
				a->chars = realloc(a->chars, a->used * sizeof(*(a->chars)));
			}
			memcpy(a->chars + chars, b->chars, a->used * sizeof(*(a->chars)));
		}
		if (chars)
			memset(a->chars, 0, chars * sizeof(*(a->chars)));
	}
	for (i = chars; i < a->used; i++) {
		carry[~i & 1] = a->chars[i] >> cc;
		a->chars[i] <<= c;
		a->chars[i] |= carry[i & 1];
	}
	if (carry[i & 1]) {
		if (a->alloced == a->used) {
			a->alloced <<= 1;
			a->chars = realloc(a->chars, a->alloced * sizeof(*(a->chars)));
		}
		a->chars[i] = carry[i & 1];
		a->used++;
	}
	a->sign = b->sign;
}

void /* a := b >> c */
zrsh(z_t a, z_t b, size_t c)
{
	size_t i, chars = c >> 5, cc = 31 - (c & 31);
	if (!c) {
		if (a != b)
			zset(a, b);
		return;
	}
	if (!b->sign || chars >= b->used || zbits(b) <= c) {
		a->sign = 0;
		return;
	}
	if (chars && a == b) {
		a->used -= chars;
		memmove(a->chars, a->chars + chars, a->used * sizeof(*(a->chars)));
	} else if (a != b) {
		a->used = b->used - chars;
		if (a->alloced < a->used) {
			a->alloced = a->used;
			a->chars = realloc(a->chars, a->used * sizeof(*(a->chars)));
		}
		memcpy(a->chars, b->chars + chars, a->used * sizeof(*(a->chars)));
	}
	a->chars[0] >>= c;
	for (i = 1; i < a->used; i++) {
		a->chars[i - 1] |= a->chars[i] >> cc;
		a->chars[i] >>= c;
	}
	a->sign = b->sign;
}

int /* (a >> b) & 1 */
zbtest(z_t a, size_t b)
{
	if (!a->sign || (b >> 5) >= a->used)
		return 0;
	return (a->chars[b >> 5] >> (b & 31)) & 1;
}

void /* high := a >> delim, low := a - (high << delim) */
zsplit(z_t high, z_t low, z_t a, size_t delim)
{
	size_t chars = delim >> 5;
	if (!a->sign) {
		high->sign = low->sign = 0;
		return;
	}
	if (high != a) {
		zrsh(high, a, delim);
		if (a->used < chars) {
			low->sign = 0;
		} else {
			if (low != a)
				zset(low, a);
			low->used = chars + 1;
			low->chars[chars] &= (((uint32_t)1 << (delim & 31)) - 1);
		}
	} else {
		if (a->used < chars) {
			low->sign = 0;
		} else {
			zset(low, a);
			low->used = chars + 1;
			low->chars[chars] &= (((uint32_t)1 << (delim & 31)) - 1);
		}
		zrsh(high, a, delim);
	}
}
