/* See LICENSE file for copyright and license details. */

/* Warning: libzahl is not thread-safe. */

#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>

typedef struct {
	int sign;
	size_t used;
	size_t alloced;
	uint32_t *chars;
} z_t[1];

enum zprimality { NONPRIME = 0, PROBABLY_PRIME, PRIME };
enum zranddev { FAST_RANDOM = 0, SECURE_RANDOM };
enum zranddist { QUASIUNIFORM = 0, UNIFORM };

void zsetup(jmp_buf);                  /* Prepare libzahl for use. */
void zunsetup(void);                   /* Free resources of libzahl */

void zinit(z_t);                       /* Prepare a for use. */
void zfree(z_t);                       /* Free resources in a. */
void zswap(z_t, z_t);                  /* (a, b) := (b, a) */
size_t zsave(z_t, void *);             /* Store a into b (if !!b), and return number of written bytes. */
size_t zload(z_t, const void *);       /* Restore a from b, and return number of read bytes. */

void zadd_unsigned(z_t, z_t, z_t);     /* a := |b| + |c|, b and c must not be the same reference. */
void zsub_unsigned(z_t, z_t, z_t);     /* a := |b| - |c|, b and c must not be the same reference. */
void zsub_positive(z_t, z_t, z_t);     /* a := |b| - |c|, assumes b ≥ c and that, and b is not c. */

void zadd(z_t, z_t, z_t);              /* a := b + c */
void zsub(z_t, z_t, z_t);              /* a := b - c */
void zmul(z_t, z_t, z_t);              /* a := b * c */
void zmodmul(z_t, z_t, z_t, z_t);      /* a := (b * c) % d */
void zdiv(z_t, z_t, z_t);              /* a := b / c */
void zdivmod(z_t, z_t, z_t, z_t);      /* a := c / d, b = c % d */
void zmod(z_t, z_t, z_t);              /* a := b % c */
void zsqr(z_t, z_t);                   /* a := b² */
void zneg(z_t, z_t);                   /* a := -b */
void zabs(z_t, z_t);                   /* a := |b| */
void zpow(z_t, z_t, z_t);              /* a := b ↑ c */
void zmodpow(z_t, z_t, z_t, z_t);      /* a := (b ↑ c) % d */

void zand(z_t, z_t, z_t);              /* a := b & c */
void zor(z_t, z_t, z_t);               /* a := b | c */
void zxor(z_t, z_t, z_t);              /* a := b ^ c */
void znot(z_t, z_t);                   /* a := ~b */
void zlsh(z_t, z_t, size_t);           /* a := b << c */
void zrsh(z_t, z_t, size_t);           /* a := b >> c */
int zbtest(z_t, size_t);               /* (a >> b) & 1 */
void zsplit(z_t, z_t, z_t, size_t);    /* a := c >> d, b := c - (a << d) */

size_t zbits(z_t);                     /* ⌊log₂ |a|⌋ + 1, 1 if a = 0 */
size_t zlsb(z_t);                      /* Index of first set bit, SIZE_MAX if none are set. */

void zgcd(z_t, z_t, z_t);              /* a := gcd(b, c) */
enum zprimality zptest(z_t, int);      /* 0 if a ∉ ℙ, 1 if a ∈ ℙ with (1 − 4↑−b) certainty, 2 if a ∈ ℙ. */
/* TODO primality test with trail division */
/* TODO primality test that returns composite (and sometimes prime) factor */
void zrand(z_t, z_t, enum zranddev, enum zranddist, ...); /* Pick a randomly from [0, b] ∩ ℤ. */

char *zstr(z_t, char *);               /* Write a in decimal onto b. */
int zsets(z_t, const char *);          /* a := b */
size_t zstr_length_positive(z_t, unsigned long long int);  /* Length of a in radix b, assuming a > 0. */

/* a := b */
void zset(z_t, z_t);
void zseti(z_t, long long int);
void zsetu(z_t, unsigned long long int);

/* signum (a - b) */
int zcmp(z_t, z_t);
int zcmpi(z_t, long long int);
int zcmpu(z_t, unsigned long long int);
/* signum (|a| - |b|) */
int zcmpmag(z_t, z_t);

static inline int /* 1 if a is even, 0 if a is odd */
zeven(z_t a)
{
	return !a->sign || !(a->chars[0] & 1);
}

static inline int /* 0 if a is even, 1 if a is odd */
zodd(z_t a)
{
	return a->sign && (a->chars[0] & 1);
}

static inline int /* 1 if a is even, 0 if a is odd, assumes a is non-zero */
zeven_nonzero(z_t a)
{
	return !(a->chars[0] & 1);
}

static inline int /* 0 if a is even, 1 if a is odd, assumes a is non-zero */
zodd_nonzero(z_t a)
{
	return (a->chars[0] & 1);
}

static inline int /* 1 if a is zero, 0 if a is zero */
zzero(z_t a)
{
	return !a->sign;
}

static inline int /* a/|a|, 0 if a is zero */
zsignum(z_t a)
{
	return a->sign;
}
