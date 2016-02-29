/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

/* signum (a - b) */

extern z_t zahl_tmp_a;

int
zcmp(z_t a, z_t b)
{
	if (a->sign != b->sign)
		return a->sign < b->sign ? -1 : a->sign > b->sign;
	return a->sign * zcmpmag(a, b);
}

int
zcmpi(z_t a, long long int b)
{
	if (!b)
		return a->sign;
	if (!a->sign)
		return b > 0 ? -1 : b < 0;
	zseti(zahl_tmp_a, b);
	return zcmp(a, zahl_tmp_a);
}

int
zcmpu(z_t a, unsigned long long int b)
{
	if (!b)
		return a->sign;
	if (a->sign <= 0)
		return -1;
	zsetu(zahl_tmp_a, b);
	return zcmp(a, zahl_tmp_a);
}

int /* signum (|a| - |b|) */
zcmpmag(z_t a, z_t b)
{
	size_t i, j;
	if ((a->sign & b->sign) == 0)
		return a->sign < b->sign ? -1 : a->sign > b->sign;
	i = a->used - 1;
	j = b->used - 1;
	for (; i > j; i--) {
		if (a->chars[i])
			return +1;
		a->used--;
	}
	for (; j > i; j--) {
		if (b->chars[j])
			return -1;
		b->used--;
	}
	for (; i; i--)
		if (a->chars[i] != b->chars[i])
			return (a->chars[i] > b->chars[i]) * 2 - 1;
	return a->chars[0] < b->chars[0] ? -1 : a->chars[0] > b->chars[0];
}
