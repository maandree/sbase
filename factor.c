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

static int certainty = 5;

static int prime_test(mp_int *x)     { int ret; mp_prime_is_prime(x, certainty, &ret); return ret;}
#define to_string(x)                (mp_todecimal(x, strbuf), strbuf)
#define div_mod(q, r, n, d)         mp_div(n, d, q, r)
#define gcd(r, a, b)                mp_gcd(a, b, r)
#define zabs(r, x)                  mp_abs(x, r)
#define zmul(r, a, b)               mp_mul(a, b, r)
#define zmul_i(r, a, b)             (zset_i(ctx->tmp, b), zmul(r, a, ctx->tmp))
#define zadd(r, a, b)               mp_add(a, b, r)
#define zadd_i(r, a, b)             (zset_i(ctx->tmp, b), zadd(r, a, ctx->tmp))
#define zsub(r, a, b)               mp_sub(a, b, r)
#define zmod(r, a, b)               mp_mod(a, b, r)
static void zset(mp_int *r, mp_int *x)  { mp_zero(r); zadd(r, r, x); }
#define zset_i(r, x)                mp_set_int(r, x)
#define zcmp(a, b)                  mp_cmp(a, b)
#define zcmp_i(a, b)                (zset_i(ctx->tmp, b), zcmp(a, ctx->tmp))
#define zparse(r, s)                (mp_init(r), mp_read_radix(r, s, 10))
typedef mp_int bigint_t[1];

#define elementsof(x)               (sizeof(x) / sizeof(*x))
#define is_factorised(x)            (!zcmp_i(x, 1))

enum { POLLARDS_RHO_INITIAL_SEED = 0 };
enum { POLLARDS_RHO_SEED_INCREASEMENT = 500 };

struct context {
	bigint_t div_n, div_q, div_r, div_d;
	bigint_t *div_stack;
	size_t div_stack_size;
	bigint_t factor, d, x, y;
	bigint_t tmp;
};

struct thread_data {
	bigint_t integer;
	size_t root_order;
};

#define LIST_CONSTANTS X(2) X(3) X(5) X(7) X(11) X(13) X(17)
static bigint_t constants[18];

#define _5(x) x, x, x, x, x
#define _25(x) _5(_5(x))
#define _50(x) _25(x), _25(x)
static const long pollards_rho_seeds[] = {
	[0] = _50(2),
	[4] = _50(11),
	[8] = _50(101),
	[16] = _50(503),
	[54] = _25(4993),
	[64] = _5(6029),
	[71] = _5(6500),
	[72] = _5(7001),
	[74] = _5(7559),
	[78] = _5(8017),
	[82] = _5(7559),
	[83] = _5(7000),
	[84] = _5(6500),
	[85] = _5(6029),
	[86] = _5(10711),
	[89] = _5(17041),
	[92] = _5(34511)
};

static char *strbuf;
#ifdef DEBUG
static bigint_t result;
static bigint_t expected;
#endif

static void
context_init(struct context *ctx, bigint_t integer)
{
	size_t n;

	if (!integer) {
		ctx->div_stack_size = 0;
		ctx->div_stack = 0;
	} else {
		n = ctx->div_stack_size = mp_count_bits(integer);
		ctx->div_stack = emalloc(n * sizeof(bigint_t));
		while (n--)
			mp_init(ctx->div_stack[n]);
	}

	mp_init(ctx->div_n);
	mp_init(ctx->div_q);
	mp_init(ctx->div_r);
	mp_init(ctx->div_d);

	mp_init(ctx->factor);
	mp_init(ctx->d);
	mp_init(ctx->x);
	mp_init(ctx->y);

	mp_init(ctx->tmp);
}

static void
context_reinit(struct context *ctx, bigint_t integer)
{
	size_t i, n = mp_count_bits(integer);

	if (n > ctx->div_stack_size) {
		i = ctx->div_stack_size;
		ctx->div_stack_size = n;
		ctx->div_stack = erealloc(ctx->div_stack, n * sizeof(bigint_t));
		while (i < n)
			mp_init(ctx->div_stack[i++]);
	}
}

static void
context_free(struct context *ctx)
{
	size_t n;

	for (n = ctx->div_stack_size; n--;)
		mp_clear(ctx->div_stack[n]);
	free(ctx->div_stack);

	mp_clear(ctx->div_n);
	mp_clear(ctx->div_q);
	mp_clear(ctx->div_r);
	mp_clear(ctx->div_d);

	mp_clear(ctx->factor);
	mp_clear(ctx->d);
	mp_clear(ctx->x);
	mp_clear(ctx->y);

	mp_clear(ctx->tmp);
}

static void
output_primes(bigint_t factor, size_t power)
{
	const char *fstr = to_string(factor);
	while (power--) {
		printf(" %s", fstr);
#ifdef DEBUG
		zmul(result, result, factor);
#endif
	}
}

static ssize_t
iterated_division(struct context *ctx, bigint_t remainder, bigint_t numerator, bigint_t denominator, size_t root_order)
{
	/*
	 * Just like n↑m by squaring, excepted this is iterated division.
	 */

	const char *dstr = root_order ? to_string(denominator) : 0;
	size_t partial_times = 1, times = 0, out, i;
	bigint_t *n = &ctx->div_n, *q = &ctx->div_q, *r = &ctx->div_r, *d = &ctx->div_d;
	bigint_t *div_stack = ctx->div_stack;

	zset(*n, numerator);
	zset(*d, denominator);
	zset(*div_stack++, denominator);

	for (;;) {
		zmul(*d, *d, *d);
		if (zcmp(*d, *n) <= 0) {
			zset(*div_stack++, *d);
			partial_times <<= 1;
		} else {
			out = root_order * partial_times;
			for (; partial_times; out >>= 1, partial_times >>= 1) {
				div_mod(*q, *r, *n, *--div_stack);
				if (!zcmp_i(*r, 0)) {
					for (i = 0; i < out; i++)
						printf(" %s", dstr);
					times |= partial_times;
					zset(*n, *q);
				}
			}
			zset(remainder, *n);
			return times;
		}
	}
}

static void
pollards_rho(struct context *ctx, bigint_t factor)
{
	/*
	 * Pollard's rho algorithm with Floyd's cycle-finding algorithm and seed.
	 * A special-purpose integer factorisation algorithm used for factoring
	 * integers with small factors.
	 */

#define QUEUE(composite, order)  (mp_init(*fstack), zset(*fstack, composite), fstack++, *rstack++ = order)
#define DEQUEUE()  (root_order = *--rstack, fstack--, zset(factor, *fstack), mp_clear(*fstack))

	size_t bits, cd, root_order;
	bigint_t *d = &ctx->d, *x = &ctx->x, *y = &ctx->y;
	size_t *rstack = emalloc(mp_count_bits(factor) * sizeof(*rstack));
	bigint_t *fstack = emalloc(mp_count_bits(factor) * sizeof(*fstack));
	bigint_t *fstack_bottom = fstack;
	bigint_t c;
	long seed;

	QUEUE(factor, 1);
	mp_init(c);
	zset_i(c, POLLARDS_RHO_INITIAL_SEED);

next:
	if (fstack == fstack_bottom) {
		free(fstack);
		free(rstack);
		mp_clear(c);
		return;
	}
	DEQUEUE();

start_over:
	bits = mp_count_bits(factor);

	if (bits < elementsof(pollards_rho_seeds))
		seed = pollards_rho_seeds[bits];
	else
		seed = pollards_rho_seeds[elementsof(pollards_rho_seeds) - 1];

	zadd_i(*x, c, seed);
	zset(*y, *x);

	for (;;) {
		do {
			zmul(*x, *x, *x), zadd_i(*x, *x, seed);
			zmul(*y, *y, *y), zadd_i(*y, *y, seed);
			zmul(*y, *y, *y), zadd_i(*y, *y, seed);
			zmod(*x, *x, factor);
			zmod(*y, *y, factor);

			zsub(*d, *x, *y);
			zabs(*d, *d);
			gcd(*d, *d, factor);
		} while (!zcmp_i(*d, 1));

		if (!zcmp(factor, *d)) {
			if (prime_test(factor)) {
				output_primes(factor, root_order);
				break;
			} else {
				zadd_i(c, c, POLLARDS_RHO_SEED_INCREASEMENT);
				goto start_over;
			}
		}

		if (prime_test(factor)) {
			iterated_division(ctx, factor, factor, *d, root_order);
			if (is_factorised(factor))
				break;
		} else {
			cd = iterated_division(ctx, factor, factor, *d, 0);
			QUEUE(*d, root_order * cd);
			if (is_factorised(factor))
				break;
		}

		if (prime_test(factor)) {
			output_primes(factor, root_order);
			break;
		}
	}

	goto next;
}

static int
factor(struct context *ctx, char *integer_str)
{
	bigint_t integer;
	size_t i, power;

	if (!*integer_str)
		goto invalid;
	for (i = 0; integer_str[i]; i++)
		if (!isdigit(integer_str[i]))
			goto invalid;

	zparse(integer, integer_str);
#ifdef DEBUG
	zset_i(result, 1);
	zset(expected, integer);
#endif

	strbuf = integer_str;

	while (*integer_str == '0' && *integer_str != 0) integer_str++;
	printf("%s:", integer_str);

	/* Behave like GNU (others too?) factor: print empty set for 0 and 1, and pretend 0 is positive. */
	if (zcmp_i(integer, 1) <= 0)
		goto done;

	context_reinit(ctx, integer);

	/* Remove factors of tiny primes. */
#ifdef DEBUG
# define print_prime(factor)  printf(" "#factor), zmul_i(result, result, factor);
#else
# define print_prime(factor)  printf(" "#factor);
#endif
#define X(factor)\
	power = iterated_division(ctx, integer, integer, constants[factor], 0);\
	if (power > 0) {\
		while (power--)\
			print_prime(factor);\
		if (is_factorised(integer))\
			goto done;\
	}
	LIST_CONSTANTS;
#undef X

	if (prime_test(integer)) {
		output_primes(integer, 1);
		goto done;
	}

	pollards_rho(ctx, integer);

#ifdef DEBUG
	if (zcmp(result, expected))
		fprintf(stderr, "\033[1;31mIncorrect factorization of %s\033[m\n", to_string(expected));
#endif

done:
	mp_clear(integer);
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
	long temp;
	int ret = 0;
	struct context ctx;

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

#define X(x)  mp_init(constants[x]), zset_i(constants[x], x);
	LIST_CONSTANTS;
#undef X

	context_init(&ctx, 0);
#ifdef DEBUG
	mp_init(result);
	mp_init(expected);
#endif

	if (*argv) {
		while (*argv)
			ret |= factor(&ctx, *argv++);
	} else {
		ssize_t n;
		size_t size = 0;
		char *line = 0;
		while ((n = getline(&line, &size, stdin)) >= 0) {
			if (n > 0 && line[n - 1] == '\n')
				n--;
			line[n] = 0;
			ret |= *line ? factor(&ctx, line) : 0;
		}
		free(line);
	}

	context_free(&ctx);
#ifdef DEBUG
	mp_clear(result);
	mp_clear(expected);
#endif

#define X(x)  mp_clear(constants[x]);
	LIST_CONSTANTS;
#undef X

	return fshut(stdout, "<stdout>") || ret;
}
