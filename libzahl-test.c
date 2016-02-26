/* See LICENSE file for copyright and license details. */
#include "zahl.h"

#include <stdio.h>

int
main(void)
{
	jmp_buf env;
	char buf[1000];
	z_t a, b, c, _0, _1;

	zsetup(env);
	zinit(a), zinit(b), zinit(c), zinit(_0), zinit(_1);

	zseti(_0, 0);
	printf("this should be 0: %s\n", zstr(_0, buf));
	zseti(_1, 1);
	printf("this should be 1: %s\n", zstr(_1, buf));

	zsets(a, "123");
	printf("this should be 123: %s\n", zstr(a, buf));
	zsets(a, "123000");
	printf("this should be 123000: %s\n", zstr(a, buf));
	zsets(b, "456");
	printf("this should be 456: %s\n", zstr(b, buf));

	zadd(c, a, b);
	printf("this should be 123456: %s\n", zstr(c, buf));
	zsub(c, a, b);
	printf("this should be 122544: %s\n", zstr(c, buf));
	zmul(c, a, b);
	printf("this should be 56088000: %s\n", zstr(c, buf));
	zdiv(c, c, b);
	printf("this should be 123000: %s\n", zstr(c, buf));
	zneg(c, c);
	printf("this should be -123000: %s\n", zstr(c, buf));
	zneg(c, c);
	printf("this should be 123000: %s\n", zstr(c, buf));
	zabs(b, c);
	printf("this should be 123000: %s\n", zstr(b, buf));
	zneg(c, c);
	zabs(b, c);
	printf("this should be 123000: %s\n", zstr(b, buf));
	zsqr(c, a);
	printf("this should be 15129000000: %s\n", zstr(c, buf));
	zseti(b, 5);
	zseti(c, 6);
	zpow(a, b, c);
	printf("this should be 15625: %s\n", zstr(a, buf));
	zseti(a, 500);
	zmodpow(a, b, c, a);
	printf("this should be 125: %s\n", zstr(a, buf));

	zsets(a, "123");
	printf("this should be 123: %s\n", zstr(a, buf));
	zsets(b, "456");
	printf("this should be 456: %s\n", zstr(b, buf));

	zmod(c, b, a);
	printf("this should be 87: %s\n", zstr(c, buf));
	zdivmod(a, b, b, a);
	printf("this should be 3: %s\n", zstr(a, buf));
	printf("this should be 87: %s\n", zstr(b, buf));

	zseti(a, 123);
	printf("this should be 123: %s\n", zstr(a, buf));
	zseti(a, -123);
	printf("this should be -123: %s\n", zstr(a, buf));
	zsetu(a, 123);
	printf("this should be 123: %s\n", zstr(a, buf));
	zset(c, a);
	printf("this should be 123: %s\n", zstr(c, buf));

	zseti(a, 128);
	zrsh(c, a, 2);
	printf("this should be 32: %s\n", zstr(c, buf));
	zlsh(c, a, 2);
	printf("this should be 512: %s\n", zstr(c, buf));

	zseti(a, 0 | 0 | 4 | 8);
	zseti(b, 0 | 2 | 0 | 8);
	zand(c, a, b);
	printf("this should be 8: %s\n", zstr(c, buf));
	zor(c, a, b);
	printf("this should be 14: %s\n", zstr(c, buf));
	zxor(c, a, b);
	printf("this should be 6: %s\n", zstr(c, buf));
	/*znot(c, a);
	printf("this should be 3: %s\n", zstr(c, buf));*/
	printf("this should be 4: %zu\n", zbits(a));
	printf("this should be 4: %zu\n", zbits(b));
	printf("this should be 2: %zu\n", zlsb(a));
	printf("this should be 1: %zu\n", zlsb(b));

	printf("this should be 12: %s\n", zstr(a, buf));
	printf("this should be 10: %s\n", zstr(b, buf));

	printf("this should be 1: %i\n", zcmp(a, b));
	printf("this should be 0: %i\n", zcmp(a, a));
	printf("this should be -1: %i\n", zcmp(b, a));

	printf("this should be 1: %i\n", zcmpi(a, 11));
	printf("this should be 0: %i\n", zcmpi(a, 12));
	printf("this should be -1: %i\n", zcmpi(a, 13));

	printf("this should be 1: %i\n", zcmpu(a, 11));
	printf("this should be 0: %i\n", zcmpu(a, 12));
	printf("this should be -1: %i\n", zcmpu(a, 13));

	zseti(a, 3 * 5 * 5 * 7 * 11);
	zseti(b, 3 * 5 * 13);
	zgcd(c, a, b);
	printf("this should be 15: %s\n", zstr(c, buf));
	zgcd(c, b, a);
	printf("this should be 15: %s\n", zstr(c, buf));
	zgcd(c, b, b);
	printf("this should be 195: %s\n", zstr(c, buf));
	zgcd(c, b, _0);
	printf("this should be 195: %s\n", zstr(c, buf));
	zgcd(c, a, _0);
	printf("this should be 5775: %s\n", zstr(c, buf));
	zgcd(c, b, _1);
	printf("this should be 1: %s\n", zstr(c, buf));
	zgcd(c, a, _1);
	printf("this should be 1: %s\n", zstr(c, buf));

	zseti(a, -1);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 0);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 1);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 2);
	printf("this should be 2: %i\n", zptest(a, 50));
	zseti(a, 3);
	printf("this should be 2: %i\n", zptest(a, 50));
	zseti(a, 4);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 5);
	printf("this should be 1: %i\n", zptest(a, 50));
	zseti(a, 6);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 7);
	printf("this should be 1: %i\n", zptest(a, 50));
	zseti(a, 8);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 9);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 10);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 11);
	printf("this should be 1: %i\n", zptest(a, 50));
	zseti(a, 12);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 13);
	printf("this should be 1: %i\n", zptest(a, 50));
	zseti(a, 14);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 15);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 16);
	printf("this should be 0: %i\n", zptest(a, 50));
	zseti(a, 17);
	printf("this should be 1: %i\n", zptest(a, 50));

	zfree(a), zfree(b), zfree(c), zfree(_0), zfree(_1);
	zunsetup();
}
