/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define CHECKED(OP, ANTI_OP)\
	if (*b && *c && LLONG_MAX ANTI_OP *b < *c)\
		fprintf(stderr, "libzahl: integer overflow: %lli "#OP" %lli\n", *b, *c), abort();\
	*a = *b OP *c

void /* a := b + c */
zadd(z_t a, z_t b, z_t c)
{
	CHECKED(+, -);
}

void /* a := b - c */
zsub(z_t a, z_t b, z_t c)
{
	*a = *b - *c;
}

void /* a := b * c */
zmul(z_t a, z_t b, z_t c)
{
	CHECKED(*, /);
}

void /* a := b / c */
zdiv(z_t a, z_t b, z_t c)
{
	if (*c == 0)
		fprintf(stderr, "libzahl: division by zero: %lli / 0\n", *b), abort();
	*a = *b / *c;
}

void /* a := c / d, b = c % d */
zdivmod(z_t a, z_t b, z_t c, z_t d)
{
	z_t t;
	if (*c == 0)
		fprintf(stderr, "libzahl: division by zero: %lli /%% 0\n", *b), abort();
	*t = *c / *d;
	*b = *c % *d;
	*a = *t;
}

void /* a := b % c */
zmod(z_t a, z_t b, z_t c)
{
	if (*c == 0)
		fprintf(stderr, "libzahl: division by zero: %lli %% 0\n", *b), abort();
	*a = *b % *c;
}

void /* a := b² */
zsqr(z_t a, z_t b)
{
	if (*b && LLONG_MAX / *b < *b)
		fprintf(stderr, "libzahl: integer overflow: %lli²\n", *b), abort();
	*a = *b * *b;
}

void /* a := -b */
zneg(z_t a, z_t b)
{
	if (*b == LLONG_MIN)
		fprintf(stderr, "libzahl: integer overflow: -(%lli)²\n", *b), abort();
	*a = -*b;
}

void /* a := |b| */
zabs(z_t a, z_t b)
{
	if (zcmpi(b, 0) < 0)
		zneg(a, b);
	else
		zset(a, b);
}

void /* a := b ↑ c */
zpow(z_t a, z_t b, z_t c)
{
	z_t f, p;

	if (zcmpi(c, 0) < 0) {
		zseti(a, 0);
		return;
	}

	zseti(a, 1);
	zset(f, b);
	zset(p, c);

	while (*p) {
		if (*p & 1)
			zmul(a, a, f);
		zrsh(p, p, 1);
		zsqr(f, f);
	}
}

void /* a := (b ↑ c) % d */
zmodpow(z_t a, z_t b, z_t c, z_t d)
{
	z_t f, p, m;

	if (zcmpi(c, 0) < 0) {
		zseti(a, 0);
		return;
	}

	zseti(a, 1);
	zset(f, b);
	zset(p, c);
	zset(m, d);

	while (*p) {
		if (*p & 1) {
			zmul(a, a, f);
			zmod(a, a, m);
		}
		zrsh(p, p, 1);
		zsqr(f, f);
		zmod(f, f, m);
	}
}
