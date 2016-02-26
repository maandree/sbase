/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

enum zprimality /* 0 if x ∉ ℙ, 1 if x ∈ ℙ with (1 - 4↑−t) certainty, 2 if x ∈ ℙ. */
zptest(z_t n, int t)
{
	/*
	 * Miller-Rabin primarlity test.
	 */

	long long i, x, a, r, d, n1, n4, _2;

	if (*n <= 1)  return NONPRIME;
	if (*n <= 3)  return PRIME;
	if (~*n & 1)  return NONPRIME;

	n1 = *n - 1, n4 = *n - 4, _2 = 2;

	r = zlsb(&n1);
	zrsh(&d, &n1, r);

	while (t--) {
		zrand(&a, &n4, FAST_RANDOM, QUASIUNIFORM);
		zadd(&a, &a, &_2);
		zmodpow(&x, &a, &d, n);

		if (x == 1 || x == n1)
			continue;

		for (i = 1; i < r; i++) {
			zsqr(&x, &x);
			zmod(&x, &x, n);
			if (x == 1)
				return NONPRIME;
			if (x == n1)
				break;
		}
		if (i == r)
			return NONPRIME;
	}

	return PROBABLY_PRIME;
}
