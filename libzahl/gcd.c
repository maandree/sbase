/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

void /* a := gcd(b, c) */
zgcd(z_t a, z_t b, z_t c)
{
	/*
	 * Binary GCD algorithm.
	 */

	unsigned long long u = *b < 0 ? -*b : *b;
	unsigned long long v = *c < 0 ? -*c : *c;
	size_t shifts = 0;

	if (*b == *c) {
		*a = *b;
		return;
	}
	if (*b == 0) {
		*a = *c;
		return;
	}
	if (*c == 0) {
		*a = *b;
		return;
	}
	if (*b < 0 || *c < 0) {
		zgcd(a, (long long *)&u, (long long *)&v);
		if (*b < 0 && *c < 0)
			zneg(a, a);
		return;
	}

	while (!((u | v) & 1))
		u >>= 1, v >>= 1, shifts++;

	while (!(u & 1))
		u >>= 1;
	do {
		while (!(v & 1))
			v >>= 1;
		if (u > v)
			*a = u, u = v, v = *a;
		v -= u;
	} while (v);

	*a = u << shifts;
}
