/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

extern z_t zahl_tmp_g, zahl_tmp_h, zahl_tmp_i, zahl_tmp_j;

size_t /* Length of a in radix b, assuming a > 0. */
zstr_length_positive(z_t a, unsigned long long int radix)
{
	size_t size_total = 1, size_temp;
	zset(zahl_tmp_i, a);
	while (zahl_tmp_i->sign > 0) {
		zsetu(zahl_tmp_g, radix);
		zset(zahl_tmp_h, zahl_tmp_g);
		size_temp = 1;
		while (zcmp(zahl_tmp_g, zahl_tmp_i) <= 0) {
			zset(zahl_tmp_h, zahl_tmp_g);
			zsqr(zahl_tmp_g, zahl_tmp_g);
			size_temp <<= 1;
		}
		size_total += size_temp >> 1;
		zdiv(zahl_tmp_i, zahl_tmp_i, zahl_tmp_h);
	}
	return size_total;
}

char * /* Write a in decimal onto b. */
zstr(z_t a, char *b)
{
	size_t n;
	char overridden;
	if (!a->sign) {
		if (!b)
			b = malloc(2);
		b[0] = '0';
		b[1] = 0;
		return b;
	}
	n = zstr_length_positive(a, 10);
	n += a->sign < 0;
	if (!b)
		b = malloc(n + 1);

	zabs(zahl_tmp_g, a);
	zsetu(zahl_tmp_h, 1000000000UL);
	n = n > 9 ? (n - 9) : (a->sign < 0);
	b[0] = '-';
	overridden = 0;

	for (;;) {
		zdivmod(zahl_tmp_j, zahl_tmp_i, zahl_tmp_g, zahl_tmp_h);
		zswap(zahl_tmp_j, zahl_tmp_g); /* TODO this should not be necessary */
		if (zahl_tmp_g->sign) {
			sprintf(b + n, "%09lu", (unsigned long)(zahl_tmp_i->chars[0]));
			b[n + 9] = overridden;
			overridden = b[n + 8];
			n = n > 9 ? (n - 9) : (a->sign < 0);
		} else {
			n += sprintf(b + n, "%lu", (unsigned long)(zahl_tmp_i->chars[0]));
			b[n] = overridden;
			break;
		}
	}

	return b;
}

int /* a := b */
zsets(z_t a, const char *b)
{
	uint32_t temp = 0;
	int neg = (*b == '-');
	const char *b_;

	if (!*b) {
		errno = EINVAL;
		return -1;
	}

	b += neg || (*b == '+');

	if (!*b) {
		errno = EINVAL;
		return -1;
	}
	for (b_ = b; *b_; b_++) {
		if (!isdigit(*b_)) {
			errno = EINVAL;
			return -1;
		}
	}

	a->sign = 0;
	zsetu(zahl_tmp_g, 1000000000UL);

	switch ((b_ - b) % 9) {
		while (*b) {
			temp *= 10, temp += *b++ & 15;
		case 8:
			temp *= 10, temp += *b++ & 15;
		case 7:
			temp *= 10, temp += *b++ & 15;
		case 6:
			temp *= 10, temp += *b++ & 15;
		case 5:
			temp *= 10, temp += *b++ & 15;
		case 4:
			temp *= 10, temp += *b++ & 15;
		case 3:
			temp *= 10, temp += *b++ & 15;
		case 2:
			temp *= 10, temp += *b++ & 15;
		case 1:
			temp *= 10, temp += *b++ & 15;
		case 0:
			zmul(a, a, zahl_tmp_g);
			zsetu(zahl_tmp_h, temp);
			zadd(a, a, zahl_tmp_h);
			temp = 0;
		}
	}

	if (neg)
		a->sign = -a->sign;
	return 0;
}
