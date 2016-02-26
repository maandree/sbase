/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

/* signum (a - b) */

int
zcmp(z_t a, z_t b)
{
	return *a < *b ? -1 : *a > *b;
}

int
zcmpi(z_t a, long long int b)
{
	return *a < b ? -1 : *a > b;
}

int
zcmpu(z_t a, unsigned long long int b)
{
	unsigned long long int au = *a;
	return *a < 0 ? -1 : au < b ? -1 : au > b;
}
