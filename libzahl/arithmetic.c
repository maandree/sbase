/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern z_t zahl_tmp_a, zahl_tmp_b, zahl_tmp_c, zahl_tmp_d, zahl_tmp_e, zahl_tmp_f;

void /* a := |b| + |c|, b and c must not be the same reference. */
zadd_unsigned(z_t a, z_t b, z_t c)
{
	size_t i, size, n;
	uint32_t carry[] = {0, 0};
	if (a == c) {
		zadd_unsigned(a, c, b);
		return;
	}
	size = (b->used > c->used ? b->used : c->used);
	if (a->alloced < size + 1) {
		a->alloced = size + 1;
		a->chars = realloc(a->chars, c->alloced * sizeof(*(a->chars)));
	}
	n = b->used + c->used - size;
	if (a == b) {
		if (a->used < c->used) {
			n = c->used;
			memset(a->chars + a->used, 0, n - a->used);
		}
		for (i = 0; i < n; i++) {
			carry[~i & 1] = (a->chars[i] & c->chars[i]) >> 31;
			a->chars[i] += c->chars[i] + carry[i & 1];
		}
	} else if (a == c) {
		if (a->used < b->used) {
			n = b->used;
			memset(a->chars + a->used, 0, n - a->used);
		}
		for (i = 0; i < n; i++) {
			carry[~i & 1] = (a->chars[i] & b->chars[i]) >> 31;
			a->chars[i] += b->chars[i] + carry[i & 1];
		}
	} else {
		if (b->used > c->used) {
			memcpy(a->chars + c->used, b->chars + c->used, b->used - c->used);
			a->used = b->used;
		} else if (b->used < c->used) {
			memcpy(a->chars + b->used, b->chars + b->used, c->used - b->used);
			a->used = c->used;
		}
		for (i = 0; i < n; i++) {
			carry[~i & 1] = (b->chars[i] & c->chars[i]) >> 31;
			a->chars[i] = b->chars[i] + c->chars[i] + carry[i & 1];
		}
	}
	while (carry[~i & 1]) {
		carry[i & 1] = a->chars[i] == 0xFFFFFFFFUL;
		a->chars[i++] += 1;
	}
	if (a->used < i)
		a->used = i;
	a->sign = !!b->sign | !!c->sign;
}

static void
zsub_positive_assign(z_t a, z_t b)
{
	size_t i, n = b->used < a->used ? b->used : a->used;
	uint32_t carry = 0;
	for (i = 0; i < n; i++) {
		if (a->chars[i] >= b->chars[i]) {
			a->chars[i] -= b->chars[i];
			if (carry) {
				a->chars[i] += ~0;
				carry = !a->chars[i];
			}
		} else {
			a->chars[i] = -(b->chars[i] - a->chars[i]);
			a->chars[i] -= carry;
			carry = 1;
		}
	}
	if (carry) {
		while (!a->chars[i])
			a->chars[i++] = ~0;
		a->chars[i] -= 1;
	}
}

void /* a := |b| - |c|, b and c must not be the same reference. */
zsub_unsigned(z_t a, z_t b, z_t c)
{
	int magcmp;
	if (b == c) {
		a->sign = 0;
		return;
	}
	magcmp = zcmpmag(b, c);
	if (magcmp <= 0) {
		if (magcmp == 0) {
			a->sign = 0;
			return;
		}
		if (a != b)
			zset(a, c);
		zsub_positive_assign(a, b);
		a->sign = -1;
	} else {
		if (a != b)
			zset(a, b);
		zsub_positive_assign(a, c);
		a->sign = 1;
	}
}

void /* a := |b| - |c|, assumes b ≥ c and that, and b is not c. */
zsub_positive(z_t a, z_t b, z_t c)
{
	if (a == c) {
		zsub_unsigned(a, c, b);
		a->sign = -a->sign;
		return;
	}
	if (a != b) {
		zabs(a, b);
	}
	zsub_positive_assign(a, c);
}

void /* a := b + c */
zadd(z_t a, z_t b, z_t c)
{
	if (!b->sign) {
		zset(a, c);
	} else if (!c->sign) {
		zset(a, b);
	} else if (b == c) {
		zlsh(a, b, 1);
	} else if ((b->sign | c->sign) < 0) {
		if (b->sign < 0) {
			if (c->sign < 0) {
				zadd_unsigned(a, b, c);
				a->sign = -a->sign;
			} else {
				zsub_unsigned(a, c, b);
			}
		} else {
			zsub_unsigned(a, b, c);
		}
	} else {
		zadd_unsigned(a, b, c);
	}
}

void /* a := b - c */
zsub(z_t a, z_t b, z_t c)
{
	if (b == c) {
		a->sign = 0;
	} else if (!b->sign) {
		zneg(a, c);
	} else if (!c->sign) {
		if (a != b)
			zset(a, b);
	} else if ((b->sign | c->sign) < 0) {
		if (b->sign < 0) {
			if (c->sign < 0) {
				zsub_unsigned(a, c, b);
			} else {
				zadd_unsigned(a, b, c);
				a->sign = -a->sign;
			}
		} else {
			zadd_unsigned(a, b, c);
		}
	} else {
		zsub_unsigned(a, b, c);
	}
}

void /* a := b * c */
zmul(z_t a, z_t b, z_t c)
{
	/*
	 * Karatsuba algorithm
	 */

	size_t m, m2;
	z_t z0, z1, z2, b_high, b_low, c_high, c_low;
	int b_sign, c_sign;

	if (!b->sign || !c->sign) {
		a->sign = 0;
		return;
	}

	m = zbits(b);
	m2 = b == c ? m : zbits(c);

	b_sign = b->sign;
	c_sign = c->sign;

	if (m < 16 && m2 < 16) {
		zsetu(a, b->chars[0] * c->chars[0]);
		a->sign = b_sign * c_sign;
		return;
	}

	b->sign = 1;
	c->sign = 1;

	m = m > m2 ? m : m2;
	m2 = m >> 1;

	zinit(z0);
	zinit(z1);
	zinit(z2);
	zinit(b_high);
	zinit(b_low);
	zinit(c_high);
	zinit(c_low);

	zsplit(b_high, b_low, b, m2);
	zsplit(c_high, c_low, c, m2);

#if 0
	zmul(z0, b_low, c_low);
	zmul(z2, b_high, c_high);
	zadd(b_low, b_low, b_high);
	zadd(c_low, c_low, c_high);
	zmul(z1, b_low, c_low);

	zsub(z1, z1, z0);
	zsub(z1, z1, z2);

	zlsh(z2, z2, m2);
	m2 <<= 1;
	zlsh(z1, z1, m2);

	zadd(a, z2, z1);
	zadd(a, a, z0);
#else
	zmul(z0, b_low, c_low);
	zmul(z2, b_high, c_high);
	zsub(b_low, b_high, b_low);
	zsub(c_low, c_high, c_low);
	zmul(z1, b_low, c_low);

	zlsh(z0, z0, m2 + 1);
	zlsh(z1, z1, m2);
	zlsh(a, z2, m2);
	m2 <<= 1;
	zlsh(z2, z2, m2);
	zadd(z2, z2, a);

	zsub(a, z2, z1);
	zadd(a, a, z0);
#endif

	zfree(z0);
	zfree(z1);
	zfree(z2);
	zfree(b_high);
	zfree(b_low);
	zfree(c_high);
	zfree(c_low);

	b->sign = b_sign;
	c->sign = c_sign;
	a->sign = b->sign * c->sign;
}

void /* a := (b * c) % d */
zmodmul(z_t a, z_t b, z_t c, z_t d)
{
	/* TODO Montgomery modular multiplication */
	if (a == d) {
		zset(zahl_tmp_f, d);
		zmul(a, b, c);
		zmod(a, a, zahl_tmp_f);
	} else {
		zmul(a, b, c);
		zmod(a, a, d);
	}
}

void /* a := b / c */
zdiv(z_t a, z_t b, z_t c)
{
	zdivmod(a, zahl_tmp_f, b, c);
}

void /* a := c / d, b = c % d */
zdivmod(z_t a, z_t b, z_t c, z_t d)
{
	size_t c_bits, d_bits, shift;
	int sign;

	if (zcmpmag(c, d) < 0) {
		a->sign = 0;
		if (b != c)
			zset(b, c);
		b->sign = c->sign * d->sign;
		return;
	}

	c_bits = zbits(c);
	d_bits = zbits(d);

	sign = c->sign * d->sign;

	shift = c_bits - d_bits;
	zlsh(zahl_tmp_d, d, shift);
	zahl_tmp_d->sign = 1;
	if (zcmpmag(zahl_tmp_d, c) > 0) {
		zrsh(zahl_tmp_d, zahl_tmp_d, 1);
		shift -= 1;
	}

	zsetu(zahl_tmp_e, 1);
	zlsh(zahl_tmp_e, zahl_tmp_e, shift);
	zahl_tmp_a->sign = 0;
	zset(zahl_tmp_b, c);
	zahl_tmp_b->sign = 1;

	while (zahl_tmp_e->sign) {
		if (zcmpmag(zahl_tmp_d, zahl_tmp_b) <= 0) {
			zsub(zahl_tmp_b, zahl_tmp_b, zahl_tmp_d);
			zor(zahl_tmp_a, zahl_tmp_a, zahl_tmp_e);
		}
		zrsh(zahl_tmp_e, zahl_tmp_e, 1);
		zrsh(zahl_tmp_d, zahl_tmp_d, 1);
	}

	zset(a, zahl_tmp_a);
	zset(b, zahl_tmp_b);
	a->sign = sign;
}

void /* a := b % c */
zmod(z_t a, z_t b, z_t c)
{
	zdivmod(zahl_tmp_f, a, b, c);
}

void /* a := b² */
zsqr(z_t a, z_t b)
{
	/*
	 * Karatsuba algorithm, optimised for equal factors.
	 */

	size_t m2;
	z_t z0, z1, z2, high, low;
	int b_sign;

	if (!b->sign) {
		a->sign = 0;
		return;
	}

	m2 = zbits(b);

	if (m2 < 16) {
		zsetu(a, b->chars[0] * b->chars[0]);
		a->sign = 1;
		return;
	}

	m2 >>= 1;
	b_sign = b->sign;
	b->sign = 1;

	zinit(z0);
	zinit(z1);
	zinit(z2);
	zinit(high);
	zinit(low);

	zsplit(high, low, b, m2);

#if 0
	zsqr(z0, low);
	zsqr(z2, high);
	zmul(z1, low, high);

	zlsh(z2, z2, m2);
	m2 = (m2 << 1) | 1;
	zlsh(z1, z1, m2);

	zadd(a, z2, z1);
	zadd(a, a, z0);
#else
	zsqr(z0, low);
	zsqr(z2, high);
	zmul(z1, low, high);

	zlsh(z0, z0, m2 + 1);
	zlsh(z1, z1, m2 + 1);
	zlsh(a, z2, m2);
	m2 <<= 1;
	zlsh(z2, z2, m2);
	zadd(z2, z2, a);

	zadd(a, z2, z1);
	zadd(a, a, z0);
#endif

	zfree(z0);
	zfree(z1);
	zfree(z2);
	zfree(high);
	zfree(low);

	b->sign = b_sign;
	a->sign = !!b->sign;
}

void /* a := -b */
zneg(z_t a, z_t b)
{
	if (a != b)
		zset(a, b);
	a->sign = -a->sign;
}

void /* a := |b| */
zabs(z_t a, z_t b)
{
	if (a != b)
		zset(a, b);
	if (a->sign < 0)
		a->sign = -a->sign;
}

void /* a := b ↑ c */
zpow(z_t a, z_t b, z_t c)
{
	if (c->sign < 0) {
		a->sign = 0;
		return;
	}

	zset(zahl_tmp_b, b);
	zset(zahl_tmp_c, c);
	zsetu(a, 1);

	while (zahl_tmp_c->sign) {
		if (zodd(zahl_tmp_c)) {
			zmul(a, a, zahl_tmp_b);
		}
		zrsh(zahl_tmp_c, zahl_tmp_c, 1);
		zsqr(zahl_tmp_b, zahl_tmp_b);
	}
}

void /* a := (b ↑ c) % d */
zmodpow(z_t a, z_t b, z_t c, z_t d)
{
	if (c->sign < 0) {
		a->sign = 0;
		return;
	}

	zmod(zahl_tmp_b, b, d);
	zset(zahl_tmp_c, c);
	zset(zahl_tmp_d, d);
	zsetu(a, 1);

	while (zahl_tmp_c->sign) {
		if (zodd(zahl_tmp_c)) {
			zmul(a, a, zahl_tmp_b);
			zmod(a, a, zahl_tmp_d);
		}
		zrsh(zahl_tmp_c, zahl_tmp_c, 1);
		zsqr(zahl_tmp_b, zahl_tmp_b);
		zmod(zahl_tmp_b, zahl_tmp_b, zahl_tmp_d);
	}
}
