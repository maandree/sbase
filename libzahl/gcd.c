/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#define u zahl_tmp_a
#define v zahl_tmp_b

extern z_t u, v;

void /* a := gcd(b, c) */
zgcd(z_t a, z_t b, z_t c)
{
	/*
	 * Binary GCD algorithm.
	 */

	size_t shifts = 0, i = 0;
	uint32_t uv, bit;

	if (!zcmp(b, c)) {
		zset(a, b);
		return;
	}
	if (!b->sign) {
		zset(a, c);
		return;
	}
	if (!c->sign) {
		zset(a, b);
		return;
	}

	zabs(u, b);
	zabs(v, c);

	for (;; i++) {
		uv = (i < u->used ? u->chars[i] : 0)
		   | (i < v->used ? v->chars[i] : 0);
		for (bit = 1; bit; bit <<= 1, shifts++)
			if (uv & bit)
				goto loop_done;
	}
loop_done:
	zrsh(u, u, shifts);
	zrsh(v, v, shifts);

	while (zeven(u))
		zrsh(u, u, 1);
	do {
		while (zeven(v))
			zrsh(v, v, 1);
		if (zcmpmag(u, v) > 0) /* both are non-negative */
			zswap(u, v);
		zsub_unsigned(v, v, u);
	} while (v->sign);

	zlsh(a, u, shifts);
	a->sign = (b->sign < 0 && c->sign < 0) ? -1 : 1;
}
