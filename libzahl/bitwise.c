/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

void /* a := b & c */
zand(z_t a, z_t b, z_t c)
{
	*a = *b & *c;
}

void /* a := b | c */
zor(z_t a, z_t b, z_t c)
{
	*a = *b | *c;
}

void /* a := b ^ c */
zxor(z_t a, z_t b, z_t c)
{
	*a = *b ^ *c;
}

void /* a := ~b */
znot(z_t a, z_t b)
{
	*a = ~*b;
}

void /* a := b << c */
zlsh(z_t a, z_t b, size_t c)
{
	*a = *b << c;
}

void /* a := b >> c */
zrsh(z_t a, z_t b, size_t c)
{
	*a = *b >> c;
}
