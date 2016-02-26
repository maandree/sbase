/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>


#ifndef FAST_RANDOM_PATHNAME
# define FAST_RANDOM_PATHNAME  "/dev/urandom"
#endif

#ifndef SECURE_RANDOM_PATHNAME
# define SECURE_RANDOM_PATHNAME  "/dev/random"
#endif


static void
zrand_get_random_bits(z_t r, size_t bits, int fd)
{
	ssize_t read_just;
	size_t read_total = 0;
	for (; read_total < sizeof(*r); read_total += read_just)
		read_just = read(fd, (char *)r + read_total, sizeof(*r) - read_total);
	*r &= (1 << bits) - 1;
}

void /* Pick r uniformly random from [0, n] ∩ ℤ. */
zrand(z_t r, z_t n, enum zranddev dev, enum zranddist dist)
{
	const char *pathname = 0;
	int fd;
	size_t bits;
	long long int _1 = 1;

	switch (dev) {
	case FAST_RANDOM:
		pathname = FAST_RANDOM_PATHNAME;
		break;
	case SECURE_RANDOM:
		pathname = SECURE_RANDOM_PATHNAME;
		break;
	default:
		abort();
	}

	if (*n == 0) {
		*r = 0;
		return;
	}

	fd = open(pathname, O_RDONLY);

	switch (dist) {
	case QUASIUNIFORM:
		bits = zbits(n);
		zrand_get_random_bits(r, bits, fd);
		zadd(r, r, &_1);
		zmul(r, r, n);
		zrsh(r, r, bits);
		break;

	case UNIFORM:
		bits = zbits(n);
		do
			zrand_get_random_bits(r, bits, fd);
		while (zcmp(r, n) > 0);
		break;

	default:
		abort();
	}

	close(fd);
}

