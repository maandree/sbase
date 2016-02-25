/* See LICENSE file for copyright and license details. */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>

#include "util.h"

/*
 * Optimisations that have been excluded.
 * 
 * For composite N, find R, B ∈ ℕ : B↑R = N. If found, continue by
 * multiplying root_order by R and factor B instead of N. This is
 * done by calculating N↑−P for all primes P : P ≤ log₃ N, if the
 * result is an integer, replace N with N↑−P, multiply R with
 * P and try P again. The final N is B. This is useful in some
 * cases, but not overall.
 */

static int certainty = 5;
static int parallel = 1;
static const char *questioned = "";

#if !defined(USE_GMP) && !defined(USE_TOMMATH)
# define USE_GMP /* GMP is significantly fast than libtommath */
#endif

#if defined(USE_GMP)
# include <gmp.h>
# define lowest_bit(x)               mpz_scan1(x, 0)
# define shift_right(r, x, steps)    mpz_tdiv_q_2exp(r, x, steps)
# define prime_test(x)               mpz_probab_prime_p(x, certainty)
# define to_string(x)                mpz_get_str(strbuf, 10, x)
# define div_mod(q, r, n, d)         mpz_tdiv_qr(q, r, n, d)
# define bit_count(x)                mpz_sizeinbase(x, 2)
# define gcd(r, a, b)                mpz_gcd(r, a, b)
# define zabs(r, x)                  mpz_abs(r, x)
# define zmul(r, a, b)               mpz_mul(r, a, b)
# define zmul_i(r, a, b)             mpz_mul_ui(r, a, b)
# define zadd(r, a, b)               mpz_add(r, a, b)
# define zadd_i(r, a, b)             mpz_add_ui(r, a, b)
# define zsub(r, a, b)               mpz_sub(r, a, b)
# define zmod(r, a, b)               mpz_mod(r, a, b)
# define zsqrt(r, x)                 mpz_sqrt(r, x)
# define zsqrt_test(r, x)            mpz_root(r, x, 2)
# define zinit(x)                    mpz_init(x)
# define zfree(x)                    mpz_clear(x)
# define zset(r, x)                  mpz_set(r, x)
# define zset_i(r, x)                mpz_set_ui(r, x)
# define zcmp(a, b)                  mpz_cmp(a, b)
# define zcmp_i(a, b)                mpz_cmp_ui(a, b)
# define zparse(r, s)                mpz_init_set_str(r, s, 10);
typedef mpz_t bigint_t;
#elif defined(USE_TOMMATH)
# include <tommath.h>
# define lowest_bit(x)               mp_cnt_lsb(x)
# define shift_right(r, x, steps)    mp_div_2d(x, steps, r, 0)
static int prime_test(mp_int *x)     { int ret; mp_prime_is_prime(x, certainty, &ret); return ret;}
# define to_string(x)                (mp_todecimal(x, strbuf), strbuf)
# define div_mod(q, r, n, d)         mp_div(n, d, q, r)
# define bit_count(x)                mp_count_bits(x)
# define gcd(r, a, b)                mp_gcd(a, b, r)
# define zabs(r, x)                  mp_abs(x, r)
# define zmul(r, a, b)               mp_mul(a, b, r)
# define zmul_i(r, a, b)             (zset_i(ctx->tmp, b), zmul(r, a, ctx->tmp))
# define zadd(r, a, b)               mp_add(a, b, r)
# define zadd_i(r, a, b)             (zset_i(ctx->tmp, b), zadd(r, a, ctx->tmp))
# define zsub(r, a, b)               mp_sub(a, b, r)
# define zmod(r, a, b)               mp_mod(a, b, r)
# define zsqrt(r, x)                 mp_sqrt(x, r)
static int zsqrt_test(mp_int *r, mp_int *x)  { int ret; mp_is_square(x, &ret); zsqrt(r, x); return ret; }
# define zinit(x)                    mp_init(x)
# define zfree(x)                    mp_clear(x)
/*# define zset(r, x)                  mp_copy(x, r) /\* TODO Is this really correct? */
static void zset(mp_int *r, mp_int *x)  { mp_zero(r); zadd(r, r, x); }
# define zset_i(r, x)                mp_set_int(r, x)
# define zcmp(a, b)                  mp_cmp(a, b)
# define zcmp_i(a, b)                (zset_i(ctx->tmp, b), zcmp(a, ctx->tmp))
# define zparse(r, s)                (zinit(r), mp_read_radix(r, s, 10))
typedef mp_int bigint_t[1];
#endif

#define elementsof(x)               (sizeof(x) / sizeof(*x))
#define is_factorised(x)            (!zcmp_i(x, 1))

enum { NO = 0, MAYBE, YES };

enum { POLLARDS_RHO_INITIAL_SEED = 0 };
enum { POLLARDS_RHO_SEED_INCREASEMENT = 500 };

struct context {
	bigint_t div_n, div_q, div_r, div_d;
	bigint_t *div_stack;
	size_t div_stack_size;
	bigint_t factor, d, x, y, conj_a, conj_b;
#ifdef USE_TOMMATH
	bigint_t tmp;
#endif
};

struct thread_data {
	bigint_t integer;
	size_t root_order;
};

#define LIST_CONSTANTS X(3) X(5) X(7) X(11) X(13) X(17)
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
static void (*output_primes)(bigint_t, int, size_t);
static void (*subfactor)(struct context *, bigint_t, size_t, bigint_t, bigint_t);
static sem_t semaphore;
static pthread_mutex_t print_mutex;
static pthread_mutex_t counter_mutex;
static pthread_cond_t counter_condition;
static int running = 0;
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
		n = ctx->div_stack_size = bit_count(integer);
		ctx->div_stack = emalloc(n * sizeof(bigint_t));
		while (n--)
			zinit(ctx->div_stack[n]);
	}

	zinit(ctx->div_n);
	zinit(ctx->div_q);
	zinit(ctx->div_r);
	zinit(ctx->div_d);

	zinit(ctx->factor);
	zinit(ctx->d);
	zinit(ctx->x);
	zinit(ctx->y);
	zinit(ctx->conj_a);
	zinit(ctx->conj_b);

#ifdef USE_TOMMATH
	zinit(ctx->tmp);
#endif
}

static void
context_reinit(struct context *ctx, bigint_t integer)
{
	size_t i, n = bit_count(integer);

	if (n > ctx->div_stack_size) {
		i = ctx->div_stack_size;
		ctx->div_stack_size = n;
		ctx->div_stack = erealloc(ctx->div_stack, n * sizeof(bigint_t));
		while (i < n)
			zinit(ctx->div_stack[i++]);
	}
}

static void
context_free(struct context *ctx)
{
	size_t n;

	for (n = ctx->div_stack_size; n--;)
		zfree(ctx->div_stack[n]);
	free(ctx->div_stack);

	zfree(ctx->div_n);
	zfree(ctx->div_q);
	zfree(ctx->div_r);
	zfree(ctx->div_d);

	zfree(ctx->factor);
	zfree(ctx->d);
	zfree(ctx->x);
	zfree(ctx->y);
	zfree(ctx->conj_a);
	zfree(ctx->conj_b);

#ifdef USE_TOMMATH
	zfree(ctx->tmp);
#endif
}

static void
parallel_init(void)
{
	if (sem_init(&semaphore, 0, parallel))
		eprintf("sem_init:");
	if ((errno = pthread_mutex_init(&print_mutex, 0)))
		eprintf("pthread_mutex_init:");
	if ((errno = pthread_mutex_init(&counter_mutex, 0)))
		eprintf("pthread_mutex_init:");
	if ((errno = pthread_cond_init(&counter_condition, 0)))
		eprintf("pthread_cond_init:");
}

static void
parallel_term(void)
{
	if (sem_destroy(&semaphore))
		eprintf("sem_destroy:");
	if ((errno = pthread_mutex_destroy(&print_mutex)))
		eprintf("pthread_mutex_destroy:");
	if ((errno = pthread_mutex_destroy(&counter_mutex)))
		eprintf("pthread_mutex_destroy:");
	if ((errno = pthread_cond_destroy(&counter_condition)))
		eprintf("pthread_cond_destroy:");
}

static void
output_primes_nonparallel(bigint_t factor, int is_prime, size_t power)
{
	const char *fstr = to_string(factor);
	const char *qstr = is_prime == MAYBE ? questioned : "";
	while (power--) {
		printf(" %s%s", fstr, qstr);
#ifdef DEBUG
		zmul(result, result, factor);
#endif
	}
}

static void
output_primes_parallel(bigint_t factor, int is_prime, size_t power)
{
	const char *fstr, *qstr = is_prime == MAYBE ? questioned : "";
	if (pthread_mutex_lock(&print_mutex))
		eprintf("pthread_mutex_lock:");
	fstr = to_string(factor);
	while (power--) {
		printf(" %s%s", fstr, qstr);
#ifdef DEBUG
		zmul(result, result, factor);
#endif
	}
	if (pthread_mutex_unlock(&print_mutex))
		eprintf("pthread_mutex_unlock:");
}

static ssize_t
iterated_division(struct context *ctx, bigint_t remainder, bigint_t numerator,
		  bigint_t denominator, size_t root_order, int is_prime)
{
	/*
	 * Just like n↑m by squaring, excepted this is iterated division.
	 */

	const char *dstr = root_order ? to_string(denominator) : 0;
	const char *nstr = is_prime == MAYBE ? questioned : "";
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
						printf(" %s%s", dstr, nstr);
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
subfactor_proper(struct context *ctx, bigint_t factor, bigint_t c, bigint_t next, size_t root_order)
{
	size_t bits, cd;
	bigint_t *d = &ctx->d, *x = &ctx->x, *y = &ctx->y;
	bigint_t *conj_a = &ctx->conj_a, *conj_b = &ctx->conj_b;
	int is_prime;
	long seed;

beginning:
	bits = bit_count(factor);

	if (bits < elementsof(pollards_rho_seeds))
		seed = pollards_rho_seeds[bits];
	else
		seed = pollards_rho_seeds[elementsof(pollards_rho_seeds) - 1];

	zadd_i(*x, c, seed);
	zset(*y, *x);

	for (;;) {
		/*
		 * There may exist a number b = (A = ⌊√n⌋ + 1)² − n such that B = √b is an integer
		 * in which case n = (A − B)(A + B)  [n is(!) odd composite]. If not, the only the
		 * trivial iteration of A (A += 1) seems to be the one not consuming your entire
		 * CPU and it is also guaranteed to find the factors, but it is just so slow.
		 */
		zsqrt(*conj_a, factor);
		zadd_i(*conj_a, *conj_a, 1);
		zmul(*conj_b, *conj_a, *conj_a);
		zsub(*conj_b, *conj_b, factor);

		if (zsqrt_test(*conj_b, *conj_b)) {
			zadd(next, *conj_a, *conj_b);
			zsub(factor, *conj_a, *conj_b);
			subfactor(ctx, next, root_order, 0, 0);
			subfactor(ctx, factor, root_order, c, next);
			break;
		}

		/* Pollard's rho algorithm with Floyd's cycle-finding algorithm and seed.
		 * A special-purpose integer factorisation algorithm used for factoring
		 * integers with small factors. */
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
			if ((is_prime = prime_test(factor))) {
				output_primes(factor, is_prime, root_order);
				break;
			} else {
				zadd_i(c, c, POLLARDS_RHO_SEED_INCREASEMENT);
				goto beginning;
			}
		}

		if ((is_prime = prime_test(factor))) {
			iterated_division(ctx, factor, factor, *d, root_order, is_prime);
			if (is_factorised(factor))
				break;
		} else {
			cd = iterated_division(ctx, factor, factor, *d, 0, 0);
			zset(next, *d);
			subfactor(ctx, next, root_order * cd, 0, 0);
			if (is_factorised(factor))
				break;
		}

		if ((is_prime = prime_test(factor))) {
			output_primes(factor, is_prime, root_order);
			break;
		}
	}
}

static void
subfactor_nonparallel(struct context *ctx, bigint_t integer, size_t root_order,
		      bigint_t reuse_seed, bigint_t reuse_next)
{
	if (reuse_seed) {
		zset_i(reuse_seed, POLLARDS_RHO_INITIAL_SEED);
		subfactor_proper(ctx, integer, reuse_seed, reuse_next, root_order);
	} else {
		bigint_t seed, next;
		zinit(seed);
		zset_i(seed, POLLARDS_RHO_INITIAL_SEED);
		zinit(next);
		subfactor_proper(ctx, integer, seed, next, root_order);
		zfree(seed);
		zfree(next);
	}
}

static void *
subfactor_parallel_threaded(void *data_)
{
	struct thread_data *data = data_;
	struct context ctx;

	if (sem_wait(&semaphore))
		eprintf("sem_wait:");

	output_primes(data->integer, 2, 1);

	context_init(&ctx, data->integer);
	subfactor_nonparallel(&ctx, data->integer, data->root_order, 0, 0);
	context_free(&ctx);
	zfree(data->integer);
	free(data);

	if (sem_post(&semaphore))
		eprintf("sem_post:");

	if ((errno = pthread_mutex_lock(&counter_mutex)))
		eprintf("pthread_mutex_lock:");
	if (--running == 0) {
		if ((errno = pthread_cond_signal(&counter_condition)))
			eprintf("pthread_cond_signal:");
	}
	if ((errno = pthread_mutex_unlock(&counter_mutex)))
		eprintf("pthread_mutex_unlock:");

	return NULL;
}

static void
subfactor_parallel(struct context *ctx, bigint_t integer, size_t root_order,
		   bigint_t reuse_seed, bigint_t reuse_next)
{
	if (reuse_seed) {
		subfactor_nonparallel(ctx, integer, root_order, reuse_seed, reuse_next);
	} else {
		struct thread_data *data = emalloc(sizeof(*data));
		pthread_t thread;
		zinit(data->integer);
		zset(data->integer, integer);
		data->root_order = root_order;

		if ((errno = pthread_mutex_lock(&counter_mutex)))
			eprintf("pthread_mutex_lock:");
		running++;

		if ((errno = pthread_mutex_unlock(&counter_mutex)))
			eprintf("pthread_mutex_unlock:");

		if ((errno = pthread_create(&thread, 0, subfactor_parallel_threaded, data)))
			eprintf("pthread_create:");
		if ((errno = pthread_detach(thread)))
			eprintf("pthread_detach:");
	}
}

static int
factor(struct context *ctx, char *integer_str, bigint_t reusable_seed, bigint_t reusable_next)
{
	bigint_t integer;
	size_t i, power;
	int is_prime;

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

	/* Behave like GNU factor: print empty set for 0 and 1, and pretend 0 is positive. */
	if (zcmp_i(integer, 1) <= 0)
		goto done;

	/* Remove factors of two. */
	power = lowest_bit(integer);
	if (power > 0) {
		shift_right(integer, integer, power);
		while (power--) {
			printf(" 2");
#ifdef DEBUG
			zmul_i(result, result, 2);
#endif
		}
		if (is_factorised(integer))
			goto done;
	}

	context_reinit(ctx, integer);

	/* Remove factors of other tiny primes. */
#ifdef DEBUG
# define print_prime(factor)  printf(" "#factor), zmul_i(result, result, factor);
#else
# define print_prime(factor)  printf(" "#factor);
#endif
#define X(factor)\
	power = iterated_division(ctx, integer, integer, constants[factor], 0, YES);\
	if (power > 0) {\
		while (power--)\
			print_prime(factor);\
		if (is_factorised(integer))\
			goto done;\
	}
	LIST_CONSTANTS;
#undef X

	if ((is_prime = prime_test(integer))) {
		output_primes(integer, is_prime, 1);
		goto done;
	}

	if (parallel < 2) {
		subfactor(ctx, integer, 1, reusable_seed, reusable_next);
	} else {
		if (sem_trywait(&semaphore))
			eprintf("sem_trywait:");

		running++;
		subfactor(ctx, integer, 1, reusable_seed, reusable_next);

		if (sem_post(&semaphore))
			eprintf("sem_post:");

		if ((errno = pthread_mutex_lock(&counter_mutex)))
			eprintf("pthread_mutex_lock:");
		if (--running != 0) {
			if ((errno = pthread_cond_wait(&counter_condition, &counter_mutex)))
				eprintf("pthread_cond_wait:");
		}
		if ((errno = pthread_mutex_unlock(&counter_mutex)))
			eprintf("pthread_mutex_unlock:");
	}

#ifdef DEBUG
	if (zcmp(result, expected))
		fprintf(stderr, "\033[1;31mIncorrect factorisation of %s\033[m\n", to_string(expected));
#endif

done:
	zfree(integer);
	printf("\n");
	return 0;
invalid:
	weprintf("%s is not a valid non-negative integer\n", integer_str);
	return 1;
}

static void
usage(void)
{
	eprintf("usage: %s [-c N] [-p N] [-q] [number ...]\n", argv0);
}

int
main(int argc, char *argv[])
{
	long temp;
	int ret = 0;
	struct context ctx;
	bigint_t reusable_seed, reusable_next;

	ARGBEGIN {
	case 'c':
		temp = strtol(EARGF(usage()), NULL, 10);
		if (temp < 1)
			eprintf("value of -c must be a positive integer\n");
		certainty = temp > INT_MAX ? INT_MAX : (int)temp;
		break;
	case 'p':
		temp = atoi(EARGF(usage()));
		if (temp < 1)
			eprintf("value of -p must be a positive integer\n");
		parallel = temp > INT_MAX ? INT_MAX : (int)temp;
		parallel = temp > SEM_VALUE_MAX ? SEM_VALUE_MAX : (int)temp;
		break;
	case 'q':
		questioned = "?";
		break;
	default:
		usage();
	} ARGEND;

#define X(x)  zinit(constants[x]), zset_i(constants[x], x);
	LIST_CONSTANTS;
#undef X

	if (parallel > 1) {
		output_primes = output_primes_parallel;
		subfactor = subfactor_parallel;
		parallel_init();
	} else {
		output_primes = output_primes_nonparallel;
		subfactor = subfactor_nonparallel;
	}

	context_init(&ctx, 0);
	zinit(reusable_seed);
	zinit(reusable_next);
#ifdef DEBUG
	zinit(result);
	zinit(expected);
#endif

	if (*argv) {
		while (*argv)
			ret |= factor(&ctx, *argv++, reusable_seed, reusable_next);
	} else {
		ssize_t n;
		size_t size = 0;
		char *line = 0;
		while ((n = getline(&line, &size, stdin)) >= 0) {
			if (n > 0 && line[n - 1] == '\n')
				n--;
			line[n] = 0;
			ret |= *line ? factor(&ctx, line, reusable_seed, reusable_next) : 0;
		}
		free(line);
	}

	context_free(&ctx);
	zfree(reusable_seed);
	zfree(reusable_next);
#ifdef DEBUG
	zfree(result);
	zfree(expected);
#endif

	if (parallel > 1)
		parallel_term();

#define X(x)  zfree(constants[x]);
	LIST_CONSTANTS;
#undef X

	return fshut(stdout, "<stdout>") || ret;
}
