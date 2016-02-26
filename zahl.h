/* See LICENSE file for copyright and license details. */

/* Warning: libzahl is not thread-safe. */

#include <stddef.h>
#include <setjmp.h>

typedef long long z_t[1];

enum zprimality { NONPRIME = 0, PROBABLY_PRIME, PRIME };
enum zranddev { FAST_RANDOM = 0, SECURE_RANDOM };
enum zranddist { QUASIUNIFORM = 0, UNIFORM };

void zsetup(jmp_buf);                  /* Prepare libzahl for use. */
void zunsetup(void);                   /* Free resources of libzahl */

void zinit(z_t);                       /* Prepare a for use. */
void zfree(z_t);                       /* Free resources in a. */

void zadd(z_t, z_t, z_t);              /* a := b + c */
void zsub(z_t, z_t, z_t);              /* a := b - c */
void zmul(z_t, z_t, z_t);              /* a := b * c */
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

size_t zbits(z_t);                     /* ⌊log₂ |a|⌋ + 1, 1 if a = 0 */
size_t zlsb(z_t);                      /* Index of first set bit, SIZE_MAX if non are set. */

void zgcd(z_t, z_t, z_t);              /* a := gcd(b, c) */
enum zprimality zptest(z_t, int);      /* 0 if a ∉ ℙ, 1 if a ∈ ℙ with (1 − 4↑−b) certainty, 2 if a ∈ ℙ. */
void zrand(z_t, z_t, enum zranddev, enum zranddist); /* Pick a randomly from [0, b] ∩ ℤ. */

char *zstr(z_t, char *);               /* Write a in decimal onto b. */
int zsets(z_t, const char *);          /* a := b */

/* a := b */
void zset(z_t, z_t);
void zseti(z_t, long long int);
void zsetu(z_t, unsigned long long int);

/* signum (a - b) */
int zcmp(z_t, z_t);
int zcmpi(z_t, long long int);
int zcmpu(z_t, unsigned long long int);
