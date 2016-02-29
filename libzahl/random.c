/* See LICENSE file for copyright and license details. */
#include "../zahl.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>


#ifndef FAST_RANDOM_PATHNAME
# define FAST_RANDOM_PATHNAME  "/dev/urandom"
#endif

#ifndef SECURE_RANDOM_PATHNAME
# define SECURE_RANDOM_PATHNAME  "/dev/random"
#endif


extern z_t zahl_tmp_f;

static void
zrand_get_random_bits(z_t r, size_t bits, int fd)
{
	ssize_t read_just;
	size_t read_total, n, chars = (bits + 31) >> 5;
	if (!bits) {
		r->sign = 0;
		return;
	}
	if (r->alloced < chars) {
		r->alloced = chars;
		r->chars = realloc(r->chars, r->alloced * sizeof(*(r->chars)));
	}
	for (n = chars; n--;) {
		for (read_total = 0; read_total < sizeof(*(r->chars)); read_total += read_just) {
			read_just = read(fd,
			                 (char *)(r->chars + n) + read_total,
			                 sizeof(*(r->chars)) - read_total);
			if (read_just < 0)
				abort();
		}
	}
	r->chars[chars - 1] &= ((uint32_t)1 << (bits & 31)) - 1;
	r->used = chars;
	r->sign = 0;
	for (n = chars; n--;) {
		if (r->chars[n]) {
			r->sign = 1;
			break;
		}
	}
}

void /* Pick r uniformly random from [0, n] ∩ ℤ. */
zrand(z_t r, z_t n, enum zranddev dev, enum zranddist dist, ...)
{
	const char *pathname = 0;
	size_t bits;
	int fd;

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

	if (!n->sign) {
		r->sign = 0;
		return;
	}

	fd = open(pathname, O_RDONLY);

	switch (dist) {
	case QUASIUNIFORM:
		bits = zbits(n);
		zrand_get_random_bits(r, bits, fd);
		zsetu(zahl_tmp_f, 1);
		zadd(r, r, zahl_tmp_f);
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
