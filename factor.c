/* See LICENSE file for copyright and license details. */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "util.h"
#include "zahl.h"

enum { POLLARDS_RHO_INITIAL_SEED = 0 };
enum { POLLARDS_RHO_SEED_INCREASEMENT = 500 };

#define LIST_TINY_PRIMES X(2) X(3) X(5) X(7) X(11) X(13) X(17)
static z_t constants[18];

#define LIST_POLLARDS_RHO_SEEDS\
	X(92, 34511) X(89, 17041) X(86, 10711) X(85, 6029) X(84, 6500) X(83, 7000)\
	X(82, 7559) X(78, 8017) X(74, 7559) X(72, 7001) X(71, 6500) X(64, 6029)\
	X(54, 4993) X(16, 503) X(8, 101) X(4, 11) X(0, 2)
static long pollards_rho_seeds_i[93];
static z_t pollards_rho_seeds[93];

static int certainty = 5;

static char *strbuf;
#ifdef DEBUG
static z_t result;
static z_t expected;
#endif

static z_t integer;
static z_t *stack = 0;
static size_t stack_size = 0;
static z_t c, d, x, y, q, r;
static z_t tmp;

#define elementsof(x)     (sizeof(x) / sizeof(*x))
#define is_factorised(x)  (!zcmp(x, constants[1]))
#define to_string(x)      zstr(x, strbuf)
#define prime_test(x)     zptest(x, certainty)

static void
output_prime(z_t prime)
{
	const char *str = to_string(prime);
	printf(" %s", str);
#ifdef DEBUG
	zmul(result, result, prime);
#endif
}

static int
div_test(z_t quotient, z_t numerator, z_t denominator)
{
	zdivmod(q, r, numerator, denominator);
	if (zcmp(r, constants[0]))
		return 0;
	zset(quotient, q);
	return 1;
}

static void
pollards_rho(z_t factor)
{
	/*
	 * Pollard's rho algorithm with Floyd's cycle-finding algorithm and seed.
	 * A special-purpose integer factorisation algorithm used for factoring
	 * integers with small factors.
	 */

#define QUEUE(composite)  (zset(*stack, composite), stack++)
#define DEQUEUE()         (stack--, zset(factor, *stack))

	size_t bits, i, seed_i;
	z_t *stack_bottom;

	bits = zbits(integer);
	if (bits > stack_size) {
		i = stack_size, stack_size = bits;
		stack = erealloc(stack, stack_size * sizeof(*stack));
		for (; i < stack_size; i++) {
			zinit(stack[i]);
		}
	}
	stack_bottom = stack;

	QUEUE(factor);
	zsetu(c, POLLARDS_RHO_INITIAL_SEED);

next:
	if (stack == stack_bottom)
		return;
	DEQUEUE();

start_over:
	bits = zbits(factor);

	if (bits < elementsof(pollards_rho_seeds_i))
		seed_i = pollards_rho_seeds_i[bits];
	else
		seed_i = pollards_rho_seeds_i[elementsof(pollards_rho_seeds_i) - 1];

	zadd(x, c, pollards_rho_seeds[seed_i]);
	zset(y, x);

	for (;;) {
		do {
			zsqr(x, x), zadd(x, x, pollards_rho_seeds[seed_i]);
			zsqr(y, y), zadd(y, y, pollards_rho_seeds[seed_i]);
			zsqr(y, y), zadd(y, y, pollards_rho_seeds[seed_i]);
			zmod(x, x, factor);
			zmod(y, y, factor);

			zsub(d, x, y);
			zabs(d, d);
			zgcd(d, d, factor);
		} while (!zcmp(d, constants[1]));

		if (!zcmp(factor, d)) {
			if (prime_test(factor)) {
				output_prime(factor);
				break;
			} else {
				zsetu(tmp, POLLARDS_RHO_SEED_INCREASEMENT);
				zadd(c, tmp, c);
				goto start_over;
			}
		}

		if (prime_test(d)) {
			output_prime(d);
			zdiv(factor, factor, d);
			if (is_factorised(factor))
				break;
		} else {
			zdiv(factor, factor, d);
			QUEUE(d);
			if (is_factorised(factor))
				break;
		}

		if (prime_test(factor)) {
			output_prime(factor);
			break;
		}
	}

	goto next;
}

static int
factor(char *integer_str)
{
	size_t i;

	if (!*integer_str)
		goto invalid;
	for (i = 0; integer_str[i]; i++)
		if (!isdigit(integer_str[i]))
			goto invalid;

	zsets(integer, integer_str);
#ifdef DEBUG
	zsetu(result, 1);
	zset(expected, integer);
#endif

	strbuf = integer_str;

	while (*integer_str == '0' && *integer_str != 0) integer_str++;
	printf("%s:", integer_str);

	/* Behave like GNU (others too?) factor: print empty set for 0 and 1, and pretend 0 is positive. */
	if (zcmp(integer, constants[1]) <= 0)
		goto done;

	/* Remove factors of tiny primes. */
#define X(factor)\
	if (div_test(integer, integer, constants[factor])) {\
		do\
			output_prime(constants[factor]);\
		while (div_test(integer, integer, constants[factor]));\
		if (is_factorised(integer))\
			goto done;\
	}
	LIST_TINY_PRIMES;
#undef X

	if (prime_test(integer)) {
		output_prime(integer);
		goto done;
	}

	pollards_rho(integer);

#ifdef DEBUG
	if (zcmp(result, expected))
		fprintf(stderr, "\033[1;31mIncorrect factorization of %s\033[m\n", to_string(expected));
#endif

done:
	printf("\n");
	return 0;
invalid:
	weprintf("%s is not a valid non-negative integer\n", integer_str);
	return 1;
}

static void
usage(void)
{
	eprintf("usage: %s [-c N] [number ...]\n", argv0);
}

int
main(int argc, char *argv[])
{
	ssize_t n;
	long temp;
	int ret = 0;

	ARGBEGIN {
	case 'c':
		temp = strtol(EARGF(usage()), NULL, 10);
		if (temp < 1)
			eprintf("value of -c must be a positive integer\n");
		certainty = temp > INT_MAX ? INT_MAX : (int)temp;
		break;
	default:
		usage();
	} ARGEND;

	n = elementsof(pollards_rho_seeds_i);
#define X(i, v)\
	while (--n >= i) pollards_rho_seeds_i[n] = i;\
	zinit(pollards_rho_seeds[i]);\
	zsetu(pollards_rho_seeds[i], v);
	LIST_POLLARDS_RHO_SEEDS;
#undef X

#define X(x)  zinit(constants[x]), zsetu(constants[x], x);
	X(0); X(1); LIST_TINY_PRIMES;
#undef X
	zinit(integer);
	zinit(q);
	zinit(r);
	zinit(c);
	zinit(d);
	zinit(x);
	zinit(y);
	zinit(tmp);
#ifdef DEBUG
	zinit(result);
	zinit(expected);
#endif

	if (*argv) {
		while (*argv)
			ret |= factor(*argv++);
	} else {
		size_t size;
		char *line = 0;
		for (size = 0; (n = getline(&line, &size, stdin)) >= 0;) {
			line[n - (n > 0 && line[n - 1] == '\n')] = 0;
			ret |= *line ? factor(line) : 0;
		}
		free(line);
	}

	return fshut(stdout, "<stdout>") || ret;
}
