/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#define x zahl_tmp_g
#define a zahl_tmp_h
#define d zahl_tmp_i
#define n1 zahl_tmp_j
#define n4 zahl_tmp_k
#define _2 zahl_tmp_l

extern z_t x, a, d, n1, n4, _2;

enum zprimality /* 0 if n ∉ ℙ, 1 if n ∈ ℙ with (1 - 4↑−t) certainty, 2 if n ∈ ℙ. */
zptest(z_t n, int t)
{
	/*
	 * Miller–Rabin primarlity test.
	 */

	size_t i, r;

	if (zcmpu(n, 1) <= 0)  return NONPRIME;
	if (zcmpu(n, 3) <= 0)  return PRIME;
	if (zeven(n))          return NONPRIME;

	zsetu(n1, 1), zsub(n4, n, n1);
	zset(n1, n4); /* TODO zsub[_unsigned](n1, n, n1) does not work. */
	zsetu(n4, 4), zsub(_2, n, n4);
	zset(n4, _2);
	zsetu(_2, 2);

	r = zlsb(n1);
	zrsh(d, n1, r);

	while (t--) {
		zrand(a, n4, FAST_RANDOM, UNIFORM);
		zadd_unsigned(a, a, _2);
		zmodpow(x, a, d, n);

		if (!zcmpu(x, 1) || !zcmp(x, n1))
			continue;

		for (i = 1; i < r; i++) {
			zsqr(x, x);
			zmod(x, x, n);
			if (!zcmpu(x, 1))
				return NONPRIME;
			if (!zcmp(x, n1))
				break;
		}
		if (i == r)
			return NONPRIME;
	}

	return PROBABLY_PRIME;
}
