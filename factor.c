/* See LICENSE file for copyright and license details. */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <tommath.h>

#include "util.h"

typedef mp_int bigint_t[1];

enum { POLLARDS_RHO_INITIAL_SEED = 0 };
enum { POLLARDS_RHO_SEED_INCREASEMENT = 500 };

#define LIST_TINY_PRIMES X(2) X(3) X(5) X(7) X(11) X(13) X(17)
static bigint_t constants[18];

#define LIST_POLLARDS_RHO_SEEDS\
	X(92, 34511) X(89, 17041) X(86, 10711) X(85, 6029) X(84, 6500) X(83, 7000)\
	X(82, 7559) X(78, 8017) X(74, 7559) X(72, 7001) X(71, 6500) X(64, 6029)\
	X(54, 4993) X(16, 503) X(8, 101) X(4, 11) X(0, 2)
static long pollards_rho_seeds_i[93];
static bigint_t pollards_rho_seeds[93];

static int certainty = 5;

static char *strbuf;
#ifdef DEBUG
static bigint_t result;
static bigint_t expected;
#endif

static bigint_t integer;
static bigint_t *stack = 0;
static size_t stack_size = 0;
static bigint_t c, d, x, y, q, r;
static bigint_t tmp;

#define elementsof(x)     (sizeof(x) / sizeof(*x))
#define is_factorised(x)  (!mp_cmp(x, constants[1]))
#define to_string(x)      (mp_todecimal(x, strbuf), strbuf)

static int prime_test(mp_int *n)          { int ret; mp_prime_is_prime(n, certainty, &ret); return ret;}
static void zset(mp_int *out, mp_int *n)  { mp_add(constants[0], n, out); }

static void
output_prime(bigint_t prime)
{
	const char *str = to_string(prime);
	printf(" %s", str);
#ifdef DEBUG
	mp_mul(result, prime, result);
#endif
}

static int
div_test(mp_int *numerator, mp_int *denominator, mp_int *quotient)
{
	mp_div(numerator, denominator, q, r);
	if (mp_cmp(r, constants[0]))
		return 0;
	zset(quotient, q);
	return 1;
}

static void
pollards_rho(bigint_t factor)
{
	/*
	 * Pollard's rho algorithm with Floyd's cycle-finding algorithm and seed.
	 * A special-purpose integer factorisation algorithm used for factoring
	 * integers with small factors.
	 */

#define QUEUE(composite)  (zset(*stack, composite), stack++)
#define DEQUEUE()         (stack--, zset(factor, *stack))

	size_t bits, i, seed_i;
	bigint_t *stack_bottom;

	bits = mp_count_bits(integer);
	if (bits > stack_size) {
		i = stack_size, stack_size = bits;
		stack = erealloc(stack, stack_size * sizeof(*stack));
		for (; i < stack_size; i++) {
			mp_init(stack[i]);
		}
	}
	stack_bottom = stack;

	QUEUE(factor);
	mp_set_int(c, POLLARDS_RHO_INITIAL_SEED);

next:
	if (stack == stack_bottom)
		return;
	DEQUEUE();

start_over:
	bits = mp_count_bits(factor);

	if (bits < elementsof(pollards_rho_seeds_i))
		seed_i = pollards_rho_seeds_i[bits];
	else
		seed_i = pollards_rho_seeds_i[elementsof(pollards_rho_seeds_i) - 1];

	mp_add(c, pollards_rho_seeds[seed_i], x); /* c + seed → x */
	zset(y, x);

	for (;;) {
		do {
			/* (The last parameters are the output parameters.) */

			mp_mul(x, x, x), mp_add(x, pollards_rho_seeds[seed_i], x);
			mp_mul(y, y, y), mp_add(y, pollards_rho_seeds[seed_i], y);
			mp_mul(y, y, y), mp_add(y, pollards_rho_seeds[seed_i], y);
			mp_mod(x, factor, x);
			mp_mod(y, factor, y);

			mp_sub(x, y, d);
			mp_abs(d, d);
			mp_gcd(d, factor, d);
		} while (!mp_cmp(d, constants[1]));

		if (!mp_cmp(factor, d)) {
			if (prime_test(factor)) {
				output_prime(factor);
				break;
			} else {
				mp_set_int(tmp, POLLARDS_RHO_SEED_INCREASEMENT);
				mp_add(c, tmp, c);
				goto start_over;
			}
		}

		if (prime_test(factor)) {
			output_prime(factor);
			mp_div(factor, d, factor, 0);
			if (is_factorised(factor))
				break;
		} else {
			mp_div(factor, d, factor, 0);
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

	mp_read_radix(integer, integer_str, 10);
#ifdef DEBUG
	mp_set_int(result, 1);
	zset(expected, integer);
#endif

	strbuf = integer_str;

	while (*integer_str == '0' && *integer_str != 0) integer_str++;
	printf("%s:", integer_str);

	/* Behave like GNU (others too?) factor: print empty set for 0 and 1, and pretend 0 is positive. */
	if (mp_cmp(integer, constants[1]) <= 0)
		goto done;

	/* Remove factors of tiny primes. */
#define X(factor)\
	if (div_test(integer, constants[factor], integer)) {\
		do\
			output_prime(constants[factor]);\
		while (div_test(integer, constants[factor], integer));\
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
	if (mp_cmp(result, expected))
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
	mp_init(pollards_rho_seeds[i]);\
	mp_set_int(pollards_rho_seeds[i], v);
	LIST_POLLARDS_RHO_SEEDS;
#undef X

#define X(x)  mp_init(constants[x]), mp_set_int(constants[x], x);
	X(0); X(1); LIST_TINY_PRIMES;
#undef X
	mp_init(integer);
	mp_init(q);
	mp_init(r);
	mp_init(c);
	mp_init(d);
	mp_init(x);
	mp_init(y);
	mp_init(tmp);
#ifdef DEBUG
	mp_init(result);
	mp_init(expected);
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
