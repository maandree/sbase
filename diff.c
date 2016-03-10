/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>

#include "arg.h"
#include "util.h"

/*
 * Lines that only appear in file1 are marked 1 (file #1 and CSI 31 m is red).
 * Lines that only appear in file2 are marked 2 (file #2 and CSI 32 m is green).
 * Lines that appear in both files are marked 0.
 */

/*
 * Excluded optimisations.
 * 
 * POSIX mandates that the output change set is minimal
 * (of course, this is not always possibly.) It does
 * however not mandate that the total output is minimal,
 * and I see no compelling reason to minimise anything
 * but the change set. There the size of the hunks are
 * not minimalised, which could be done by shifting
 * lumps of changes without the same hunk.
 * 
 * After compressing the data set, it is possible to
 * divide and “conquer” the problem. Find any lump
 * that is at least as large as the sum of all other
 * lumps. Solve the problem recursively on both sides
 * of that lump. Their base case is not solved
 * directly, but independently.
 * 
 * The data set could be further compressed by supporting
 * lumping of chunks that do not appear the same number
 * of times in both files. Implementing this would be
 * too costly to be beneficial, despite running in
 * O(n log n) on average.
 */

#define END_OF_PATH          127
#define NO_LF_MARK           "\n\\ No newline at end of file"
#define COLOURED_NO_LF_MARK  "\n\033[7m\\ No newline at end of file\033[27m"

#define line_eq(a, b)  ((a)->hash == (b)->hash && !strcmp((a)->line, (b)->line))
#define intcmp(a, b)   ((a) < (b) ? -1 : (a) > (b))

enum { FILES_DIFFER = 1, NOT_MINIMAL = 2, BINARIES_DIFFER = 3, FAILURE = 4 };


struct file_data {
	char **lines;
	size_t line_count; /* used as length of `lines[0]` if `is_binary` */
	unsigned lf_terminated : 1;
	unsigned is_binary : 1;
	unsigned is_empty : 1;
	struct stat attr;
	const char *path;
	char *path_quoted;
};

struct trace {
	char f;
	int ch;
	size_t d;
	size_t a_len;
	size_t b_len;
};

struct chunk {
	size_t ai;
	size_t bi;
	unsigned have_a : 1;
	unsigned have_b : 1;
	struct trace *chunk;
};

struct line {
	char *line;
	char *end;
	char old_end_char;
	unsigned uncommon : 1;
	size_t hash;
	size_t compression;
	size_t number;
};


static int bflag = 0;
static int cflag = 0;
static int eflag = 0;
static int fflag = 0;
static int uflag = 0;
static int rflag = 0;
static size_t n_context = 0;

/* Minimal output or cheap comparsion? */
static int bdiff = 0;
static int dflag = 0;
static int cheap_algorithm_used = 0;

static int use_colour = 0;
static int (*cprintf)(const char *, ...);


static void
usage(void)
{
	enprintf(FAILURE, "usage: %s [-c | -C n | -e | -f | -u | -U n] [-bdDr] file1 file2\n", argv0);
}

static int
is_minimal_feasible(size_t a, size_t b)
{
	size_t max = a > b ? a : b;

	if (max > (SIZE_MAX / max) / 2)
		return 0;

	/* Keep in mind, this is not the size of the files, but rather the size of the
	 * set that is compared after reduction and compression. It is important to
	 * remember that the user may want to run diff one a large set of files, not
	 * just one file. */
	return a * b < 1000UL * 1000UL;
}

#ifdef DEBUG
static void
measure_time(const char *label)
{
	static clock_t clocks[10];
	static int i = 0, j = 0;
	clock_t duration;
	double seconds;
	if (!label) {
		clocks[i++] = clock();
	} else {
		duration = clock() - clocks[--i];
		seconds = duration;
		seconds /= CLOCKS_PER_SEC;
		fprintf(stderr, "\033[1;3%imTIME MEASUREMENT [%*s%s]: %lu clocks = %lf seconds\033[m\n",
		        (j ^= 1) ? 1 : 5, 2 * i, "", label, (unsigned long)duration, seconds);
	}
}
#else
# define measure_time(x)  ((void)0)
#endif


/* Functions for diff:ing directories. { */

static char *
join_paths(const char *a, const char *b)
{
	char *rc = enmalloc(FAILURE, strlen(a) + strlen(b) + sizeof("/"));
	sprintf(rc, "%s/%s", a, b);
	return rc;
}

static const char *
classify(struct file_data *f)
{
	return !f ? "directory" : f->is_empty ? "regular empty file" : "regular file";
}

static int
is_incommensurable(mode_t mode)
{
	/* POSIX specifies that if a and b refer to the same special device,
	 * there should be no comparision. This seems unnecessary since it
	 * also specifies that special devices and FIFO:s shall not be compared.
	 * We extend this to not compare sockets either. POSIX says that it
	 * is implementation-specified for other types than special files,
	 * FIFO:s, regular files and directories. */
	return S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) || S_ISSOCK(mode);
}

/* } */


/* Comparsion functions for lines and pointers to lines. { */

static int linepcmp_hash_line_stable(const void *a_, const void *b_)
{
	const struct line *const *ap = a_, *const *bp = b_;
	const struct line *a = *ap, *b = *bp;
	int cmp;
	if ((cmp = intcmp(a->hash, b->hash)))  return cmp;
	if ((cmp = strcmp(a->line, b->line)))  return cmp;
	return intcmp(a->number, b->number);
}

static int linecmp_hash_line_stable(const void *a_, const void *b_)
{
	const struct line *a = a_, *b = b_;
	int cmp;
	if ((cmp = intcmp(a->hash, b->hash)))  return cmp;
	if ((cmp = strcmp(a->line, b->line)))  return cmp;
	return intcmp(a->number, b->number);
}

static int linecmp_hash_line(const void *a_, const void *b_)
{
	const struct line *a = a_, *b = b_;
	int cmp;
	if ((cmp = intcmp(a->hash, b->hash)))  return cmp;
	return strcmp(a->line, b->line);
}

/* } */


/* Functions for supporting -b. { */

static char *
rstrip(char *text, char *removed)
{
	char *end = strchr(text, '\0');
	while ((end != text) && isspace(*--end));
	*removed = *end, *end = '\0';
	return end;
}

static void
rstrip_lines(struct line *lines, size_t n)
{
	size_t i;
	for (i = 0; bflag && i < n; i++)
		lines[i].end = rstrip(lines[i].line, &(lines[i].old_end_char));
}

static void
unrstrip_lines(struct line *lines, size_t n)
{
	size_t i;
	for (i = 0; bflag && i < n; i++)
		*(lines[i].end) = lines[i].old_end_char;
}

/* } */


/* Auxiliary functions used by the input reducers and the differs. { */

static void
hash_lines(struct line *lines, size_t n)
{
	size_t hash;
	char *str;
	while (n--) {
		for (hash = 0, str = lines[n].line; *str; str++)
			hash = 31 * hash + (size_t)*str;
		lines[n].hash = hash;
	}
}

static void
retain_longest_increasing_subsequence(size_t *map, size_t n)
{
	size_t best_length = 0, low, high, mid, p, i;
	ssize_t j;

	/* Map from i to largest j such that j < i, where j
	 * is an included index in the same subsequence. */
	size_t *previous = enmalloc(FAILURE, n * sizeof(*previous));
	/* Map from a length of a subsequence to the last
	 * included index in such subsequence. */
	size_t *end_of = enmalloc(FAILURE, (n + 1) * sizeof(*end_of));

	/* SIZE_MAX marks unset. We do this so that
	 * the last part of the algorithm terminates. */
	memset(end_of, ~0, (n + 1) * sizeof(*end_of));

	for (i = 0; i < n; i++) {
		/* Find longest subsequence that can be extended. */
		low = 1;
		high = best_length;
		while (low <= high) {
			mid = (low + high + 1) / 2; /* Cannot overflow in our case. */
			if (map[end_of[mid]] < map[i])
				low = mid + 1;
			else
				high = mid - 1;
		}

		/* Extend that subsequence. */
		previous[i] = end_of[low - 1];
		end_of[low] = i;

		/* Get the length of the longest, yet known, subsequence. */
		if (low > best_length)
			best_length = low;
	  }

	/* Erase elements that where not selected, so that only
	 * the longest increasing subsequence is present. */
	p = end_of[best_length];
	j = (ssize_t)n - 1;
	while (p < SIZE_MAX) {
		while ((ssize_t)p < j)
			map[j--] = SIZE_MAX;
		j--;
		p = previous[p];
	}
	while (j >= 0)
		map[j--] = SIZE_MAX;

	free(previous);
	free(end_of);
}

/* } */


/* Diff algorithm for large input. { */

/* This algorithm runs in O(n log n) rather than O(n²) time, and
 * O(n) rather than O(n²) space, and is used in the event that O(n²)
 * is deemed too expensive. The output is far from optimal, but
 * it does not matter too much, the program reduces the problem
 * enough that this will probably not be used too often. */
static char *
diff2_cheap(struct line *a_, struct line *b_, size_t an, size_t bn)
{
	/*
	 * The idea behind the algorithm.
	 * 
	 * For lines that appear in both files, create a map from line numbers in the old file to
	 * line numbers in the new file. For duplicate lines that do not appear the same number of
	 * times in both files, do not care too much.
	 * 
	 * The map is not ordered, as find the longest increasing subsequence (there are gaps in
	 * subsequences) and erase the rest from the map.
	 * 
	 * Removes lines from the old files are indicated by missing entries in the map
	 * (marked with SIZE_MAX in this implementation) and added lines can be inferred
	 * from gaps in the map.
	 */

	size_t ai, bi, ri;
	struct line *a = enmalloc(FAILURE, an * sizeof(*a));
	struct line *b = enmalloc(FAILURE, bn * sizeof(*b));
	size_t *map = enmalloc(FAILURE, an * sizeof(*map));
	char *rc;

	memcpy(a, a_, an * sizeof(*a));
	memcpy(b, b_, bn * sizeof(*b));

	/* Remember the original position _and_ make stable sort possible. */
	for (ai = 0; ai < an; ai++)  a[ai].number = ai;
	for (bi = 0; bi < bn; bi++)  b[bi].number = bi;

	/* Perform stable total ordering (not really proper sort) of line content. */
	qsort(a, an, sizeof(*a), linecmp_hash_line_stable);
	qsort(b, bn, sizeof(*b), linecmp_hash_line_stable);

	/* Get new line numbers, and mark removed lines with SIZE_MAX. */
	memset(map, ~0, an * sizeof(*map));
	for (ai = bi = 0; ai < an && bi < bn;) {
		int cmp = linecmp_hash_line(a + ai, b + bi);
		if (!cmp)
			map[a[ai].number] = b[bi].number;
		ai += (cmp <= 0);
		bi += (cmp >= 0);
	}
	free(a);
	free(b);

	/* Find the longest increasing subsequence of map. The map is not monotonic
	 * unless we do this. This is done in O(n log n), just like sorting, so we
	 * get a better result (compare to selecting naïvely) without any significant
	 * performance penalty. */
	retain_longest_increasing_subsequence(map, an);

	/* Create change set from removed lines and gaps in new line numbers. */
	rc = enmalloc(FAILURE, an + bn + 1);
	ri = an + bn;
	for (ai = bi = 0; ai < an; ai++) {
		if (map[ai] == SIZE_MAX) {
			rc[ri--] = 1;
		} else {
			while (map[ai] > bi++)
				rc[ri--] = 2;
			rc[ri--] = 0;
		}
	}
	free(map);
	while (bi++ < bn)
		rc[ri--] = 2;
	rc[ri] = END_OF_PATH;
	memmove(rc, rc + ri, an + bn - ri + 1);

	return rc + an + bn - ri + 1;
}

/* } */


/* Diff algorithm for minimal output. { */

static char *
diff2_minimal(struct line *a, struct line *b, size_t an, size_t bn)
{
	/*
	 * This function calculates the minimal edit set for getting from `a`
	 * to `b`. But rather than using an algorithm that calculates the
	 * smallest changes set, this function uses an algorithm for calculating
	 * the longest common (non-consecutive) subsequence, which is an identical
	 * (with the allowed change operations) except for the value returned
	 * at the end, which is not inspected anyways. This algorithms has the
	 * advantages of requiring simpler initialisation.
	 */

	char (*map)[an + 1][bn + 1] = enmalloc(FAILURE, sizeof(char[an + 1][bn + 1]));
	size_t (*matrix)[2][bn + 1] = encalloc(FAILURE, 1, sizeof(size_t[2][bn + 1]));
	char *rc;
	size_t ai, bi, ri = 0, mi = 0;

	memset((*map)[0], 2, bn + 1);

	a--, b--;
	for (ai = 1; ai <= an; ai++) {
		size_t *last = (*matrix)[mi];
		size_t *this = (*matrix)[mi ^= 1];
		(*map)[ai][0] = 1;
		for (bi = 1; bi <= bn; bi++) {
			size_t u = last[bi];
			size_t d = last[bi - 1] + 1;
			size_t l = this[bi - 1];
			size_t lu = l >= u ? l : u;
			if (line_eq(a + ai, b + bi) && d > lu) {
				/* The > in this comparison makes changes happen as late
				 * as possible. >= would make the changes happen as early
				 * as possible. */
				this[bi] = d;
				(*map)[ai][bi] = 0;
			} else {
				this[bi] = lu;
				(*map)[ai][bi] = 1 + (l >= u);
			}
		}
	}
	free(matrix);

	rc = enmalloc(FAILURE, an + bn + 1);
	rc[ri++] = END_OF_PATH;
	for (ai = an, bi = bn; ai + bi; ri++) {
		rc[ri] = (*map)[ai][bi];
		ai -= rc[ri] != 2;
		bi -= rc[ri] != 1;
	}
	free(map);

	return rc + ri;
}

static char *
diff2_minimal_maybe_transpose(struct line *a, struct line *b, size_t an, size_t bn)
{
	/*
	 * The minimise memory usage in diff2_minimal, swap the files if it helps.
	 */

	int transpose = bn < an;
	char trace, *path, *rc = !transpose ? diff2_minimal(a, b, an, bn) : diff2_minimal(b, a, bn, an);
	for (path = rc; transpose && (trace = *--path) != END_OF_PATH;)
		if (trace)
			*path = 3 - trace;
	return rc;
}

/* } */


/* Result enhancements, better output and more details for the output functions. { */

static struct trace *
enhance_path(char *path)
{
	char *p = path;
	size_t len, a_len = 0, b_len = 0, i = 0, d = 0, a = 0, b = 0, j = 0;
	int have_d = 0, ch = 0, differs = 0;
	struct trace *rc;

	while (*--p != END_OF_PATH);
	len = (size_t)(path - p);
	rc = encalloc(FAILURE, len, sizeof(*rc));

	/* Find distance from edits, and mark exchanges. (left-to-right) */
	for (--len; i < len; i++) {
		rc[i].f = *--path;
		if (rc[i].f) {
			d = 0, have_d = 1;
			ch |= ch ? ch : 3 - rc[i].f;
			if (rc[i].f == ch)
				rc[i].ch = 1;
		} else {
			ch = 0;
			rc[i].d = (have_d ? ++d : SIZE_MAX);
		}
	}
	rc[i].f = END_OF_PATH;

	/* Find distance from edits, mark exchanges, and get chunk lengths. (right-to-left) */
	for (i = len, d = 0, ch = have_d = 0; i-- > 0;) {
		rc[i].a_len = a_len += (rc[i].f != 2);
		rc[i].b_len = b_len += (rc[i].f != 1);
		if (rc[i].f) {
			d = 0, have_d = 1;
			ch |= ch ? ch : (3 - rc[i].f);
			if (rc[i].f == ch)
				rc[i].ch = 1;
		} else {
			ch = 0;
			if (have_d && (d + 1) < rc[i].d)
				rc[i].d = ++d;
			if (rc[i].d > n_context)
				a_len = b_len = 0;
		}
	}

	/* Put removals before additions. */
	for (i = 0; i < len; i++) {
		if (rc[i].f == 0) {
			while (a--)  rc[j++].f = 1;
			while (b--)  rc[j++].f = 2;
			j = i + 1, a = b = 0;
		} else if (rc[i].f == 1) {
			a++, differs = 1;
		} else {
			b++, differs = 1;
		}
	}
	while (a--)  rc[j++].f = 1;
	while (b--)  rc[j++].f = 2;

	free(p);
	if (!differs) {
		free(rc);
		return 0;
	}
	return rc;
}

/* } */


/* When you cannot reduce the time or space complexity, reduce the input. { */

/*
 * Anything that can be executed in O(n log n) time and space is fine.
 * If it is above that, it is probably not a good idea. Anything that
 * is above o(n²) time or space is pointless.
 */

static void
reduce_problem(struct line **ap, struct line **bp, size_t *anp, size_t *bnp, size_t *start, size_t *end)
{
	size_t skip_start = 0, skip_end = 0, an = *anp, bn = *bnp;
	struct line *a = *ap, *b = *bp;

	/* Reduce problem set, by skipping identical head. */
	for (skip_start = 0; skip_start < an && skip_start < bn; skip_start++)
		if (strcmp(a[skip_start].line, b[skip_start].line))
			break;

	a += skip_start, an -= skip_start;
	b += skip_start, bn -= skip_start;

	/* Reduce problem set, by skipping identical tail. */
	for (skip_end = 0; an && bn; an--, bn--, skip_end++)
		if (strcmp(a[an - 1].line, b[bn - 1].line))
			break;

	*ap = a, *anp = an, *start = skip_start;
	*bp = b, *bnp = bn, *end = skip_end;
}

static void
unreduce_problem(char **pathp, struct line **ap, struct line **bp, size_t skip_start, size_t skip_end)
{
	char *path = *pathp;
	size_t path_len;

	if (!skip_start && !skip_end)
		return;

	while (*--path != END_OF_PATH);
	path_len = (size_t)(*pathp - path);
	path = enrealloc(FAILURE, path, skip_end + path_len + skip_start);
	if (skip_end) {
		memmove(path + skip_end + 1, path + 1, path_len - 1);
		memset(path + 1, 0, skip_end);
	}
	memset(path + skip_end + path_len, 0, skip_start);
	*pathp = path + skip_end + path_len + skip_start;

	*ap -= skip_start;
	*bp -= skip_start;
}


static void
compress_problem_1(struct line *a, struct line *b, size_t an, size_t bn,
                   struct line **acp, struct line **bcp, size_t *acnp, size_t *bcnp)
{
	/*
	 * The idea behind the algorithm.
	 * 
	 * For lines that appear in both files the same number of times, create a
	 * map from line numbers in the old file to line numbers in the new file.
	 * 
	 * The map is not ordered, as find the longest increasing subsequence
	 * (there are gaps in subsequences) and erase the rest from the map.
	 * 
	 * Inspect the map for gaps in the old line numbers and the new line numbers.
	 * The gaps can be lumped together, but non-unique lines have to divide the
	 * lumps. Parts between gaps can be lumped together too.
	 */

	size_t ai, bi, ao, bo;
	struct line **a_sorted = enmalloc(FAILURE, an * sizeof(*a_sorted));
	struct line **b_sorted = enmalloc(FAILURE, bn * sizeof(*b_sorted));
	struct line *ac, *bc;
	size_t *map = enmalloc(FAILURE, an * sizeof(*map));

	for (ai = 0; ai < an; ai++)  (a_sorted[ai] = a + ai)->number = ai;
	for (bi = 0; bi < bn; bi++)  (b_sorted[bi] = b + bi)->number = bi;

	qsort(a_sorted, an, sizeof(*a_sorted), linepcmp_hash_line_stable);
	qsort(b_sorted, bn, sizeof(*b_sorted), linepcmp_hash_line_stable);

	memset(map, ~0, an * sizeof(*map));
	for (ai = bi = 0; ai < an && bi < bn;) {
		int cmp = linecmp_hash_line(a_sorted[ai], b_sorted[bi]);
		if (cmp < 0) {
			a_sorted[ai++]->uncommon = 1;
		} else if (cmp > 0) {
			b_sorted[bi++]->uncommon = 1;
		} else {
			ao = ai, bo = bi;
			while (++ai < an && line_eq(a_sorted[ai], a_sorted[ai - 1]));
			while (++bi < bn && line_eq(b_sorted[bi], b_sorted[bi - 1]));
			if (ai - ao == bi - bo)
				while (ao < ai)
					map[a_sorted[ao++]->number] = b_sorted[bo++]->number;
		}
	}
	while (ai < an)  a_sorted[ai++]->uncommon = 1;
	while (bi < bn)  b_sorted[bi++]->uncommon = 1;

	free(a_sorted);
	free(b_sorted);

	retain_longest_increasing_subsequence(map, an);

	for (ai = bi = ao = bo = 0; ai < an; ai++) {
		if (map[ai] == SIZE_MAX) {
			if (ai > 0 && a[ai - 1].uncommon && a[ai].uncommon)
				a[ao].compression += 1 + a[ai].compression;
			else if (a[ai].uncommon)
				ao = ai;
		} else if (ai > 0 && map[ai] > 0 && map[ai - 1] == map[ai] - 1) {
			a[ao].compression += 1 + a[ai].compression;
			b[bo].compression += 1 + b[bi].compression;
			bi++;
		} else {
			for (bo = SIZE_MAX; bi < map[ai]; bi++)
				if (!b[bi].uncommon)
					bo = SIZE_MAX;
				else if (bo == SIZE_MAX)
					bo = bi;
				else
					b[bo].compression += 1 + b[bi].compression;
			ao = ai, bo = bi;
		}
	}
	for (bo = SIZE_MAX; bi < bn; bi++)
		if (!b[bi].uncommon)
			bo = SIZE_MAX;
		else if (bo == SIZE_MAX)
			bo = bi;
		else
			b[bo].compression += 1 + b[bi].compression;

	free(map);

	*acp = ac = enmalloc(FAILURE, an * sizeof(*ac));
	*bcp = bc = enmalloc(FAILURE, bn * sizeof(*bc));

	for (ai = ao = 0; ai < an; ai++, ao++)  ai += (ac[ao] = a[ai]).compression;
	for (bi = bo = 0; bi < bn; bi++, bo++)  bi += (bc[bo] = b[bi]).compression;

	*acnp = ao;
	*bcnp = bo;
}

static void
compress_problem(struct line *a, struct line *b, size_t an, size_t bn,
                 struct line **acp, struct line **bcp, size_t *acnp, size_t *bcnp)
{
	if (bdiff) {
		*acp = a, *acnp = an;
		*bcp = b, *bcnp = bn;
		return;
	}

	compress_problem_1(a, b, an, bn, acp, bcp, acnp, bcnp);
}

static void
decompress_problem(size_t an, size_t bn, struct line *a_compressed, struct line *b_compressed, char **pathp)
{
	char *path = *pathp;
	char *new_path;
	char trace;
	size_t ai = 0, bi = 0, pi = an + bn;

	if (bdiff)
		return;

	new_path = enmalloc(FAILURE, an + bn + 1);

	while ((trace = *--path) != END_OF_PATH) {
		size_t dups = (trace == 1 ? (a_compressed + ai) : (b_compressed + bi))->compression;
		do
			new_path[pi--] = trace;
		while (dups-- > 0);
		ai += trace != 2;
		bi += trace != 1;
	}
	new_path[pi] = END_OF_PATH;
	memmove(new_path, new_path + pi, an + bn - pi + 1);

	*pathp = new_path + an + bn - pi + 1;

	free(path);
	free(a_compressed);
	free(b_compressed);
}

/* } */


/* diff implementation for files. {  */

static struct trace *
diff2(char **a, char **b, size_t an, size_t bn)
{
	size_t skip_start, skip_end, i, an_compressed, bn_compressed;
	char *path;
	struct line *a_compressed, *b_compressed;
	struct line *a_lines = encalloc(FAILURE, an, sizeof(*a_lines));
	struct line *b_lines = encalloc(FAILURE, bn, sizeof(*b_lines));
	char *(*algorithm)(struct line *, struct line *, size_t, size_t);

	measure_time(0);

	for (i = 0; i < an; i++)  a_lines[i].line = a[i];
	for (i = 0; i < bn; i++)  b_lines[i].line = b[i];

	rstrip_lines(a_lines, an);
	rstrip_lines(b_lines, bn);

	reduce_problem(&a_lines, &b_lines, &an, &bn, &skip_start, &skip_end);

	hash_lines(a_lines, an);
	hash_lines(b_lines, bn);

	measure_time(0);
	compress_problem(a_lines, b_lines, an, bn, &a_compressed, &b_compressed, &an_compressed, &bn_compressed);
	measure_time("compress_problem");

	if (bdiff) {
		algorithm = diff2_cheap;
	} else if (dflag || is_minimal_feasible(an_compressed, bn_compressed)) {
		algorithm = diff2_minimal_maybe_transpose;
	} else {
		algorithm = diff2_cheap;
		cheap_algorithm_used = 1;
	}
	measure_time(0);
	path = algorithm(a_compressed, b_compressed, an_compressed, bn_compressed);
	measure_time("diff");

	decompress_problem(an, bn, a_compressed, b_compressed, &path);

	unreduce_problem(&path, &a_lines, &b_lines, skip_start, skip_end);

	unrstrip_lines(a_lines, an);
	unrstrip_lines(b_lines, bn);

	measure_time("diff2");

	free(a_lines);
	free(b_lines);
	return enhance_path(path);
}

/* } */


/* Functions for producing diff output. { */

static char *
get_time_string_unified(const struct stat *attr)
{
	static char buf[sizeof("0000-00-00 00:00:00.000000000 +0000")];
	struct tm *tm;

	tm = localtime(&(attr->st_mtime));
	if (tm == NULL)
		enprintf(FAILURE, "localtime:");

#ifdef st_mtime
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.000000000 %z", tm);
	sprintf(buf + (sizeof("0000-00-00 00:00:00.") - 1), "%09lu", attr->st_mtim.tv_nsec);
	buf[sizeof("0000-00-00 00:00:00.") - 1 + 9] = ' ';
#else
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", tm);
#endif
	return buf;
}

static char *
get_time_string_copied(const struct stat *attr)
{
	static char buf[100];
	struct tm *tm;

	tm = localtime(&(attr->st_mtime));
	if (tm == NULL)
		enprintf(FAILURE, "localtime:");

	strftime(buf, sizeof(buf), "%a %b %e %T %Y", tm);
	return buf;
}

static void
get_diff_chunks(struct trace *path, size_t an, size_t bn, struct chunk **headp, struct chunk **tailp)
{
	struct trace trace;
	size_t ai, bi;
	int suppressed = 1, have_a = 0, have_b = 0;
	struct chunk *head = *headp;

	head = encalloc(FAILURE, an + bn + 1, sizeof(*head));
	*tailp = head++;

	for (ai = bi = 0; (trace = *path++).f != END_OF_PATH;) {
		if (trace.d > n_context) {
			suppressed = 1;
			if (head->chunk) {
				head->have_a = have_a;
				head->have_b = have_b;
				head++;
			}
			have_a = have_b = 0;
			goto next;
		}
		if (suppressed) {
			head->ai = ai;
			head->bi = bi;
			head->chunk = path - 1;
		}
		have_a |= trace.f == 1;
		have_b |= trace.f == 2;
		suppressed = 0;
	next:
		ai += trace.f != 2;
		bi += trace.f != 1;
	}
	if (head->chunk) {
		head->have_a = have_a;
		head->have_b = have_b;
		head++;
	}

	*headp = head;
}

#define OUTPUT_BEGIN\
	struct trace *path;\
	size_t ai, bi;\
	int have_a = 0, have_b = 0;\
	struct trace *chunk;\
	struct trace *chunk_old;\
	struct chunk *head;\
	struct chunk *tail;\
	char **a = old->lines;\
	char **b = new->lines\

#define OUTPUT_DIFF\
	path = diff2(old->lines, new->lines, old->line_count * !old->is_empty, new->line_count * !new->is_empty);\
	if (!path)\
		return 0;\
	if (diff_line)\
		cprintf("\033[1m%s %s %s\033[m\n", diff_line, old->path_quoted, new->path_quoted)

#define OUTPUT_HEAD(A, B, TIMEFUN)\
	cprintf("\033[1m"A" %s\033[21m\t%s\033[0m\n", old->path_quoted, TIMEFUN(&(old->attr)));\
	cprintf("\033[1m"B" %s\033[21m\t%s\033[0m\n", new->path_quoted, TIMEFUN(&(new->attr)))

#define OUTPUT_QUEUE\
	get_diff_chunks(path, old->line_count * !old->is_empty, new->line_count * !new->is_empty, &head, &tail);\
	(void) chunk_old, (void) a, (void) b;\
	for (head = tail;;) {\
		head++;\
		ai = head->ai;\
		bi = head->bi;\
		have_a = head->have_a;\
		have_b = head->have_b;\
		chunk = head->chunk;\
		if (!chunk)\
			break

#define OUTPUT_STACK\
	get_diff_chunks(path, old->line_count * !old->is_empty, new->line_count * !new->is_empty, &head, &tail);\
	(void) chunk_old, (void) a, (void) b;\
	for (;;) {\
		head--;\
		ai = head->ai;\
		bi = head->bi;\
		have_a = head->have_a;\
		have_b = head->have_b;\
		chunk = head->chunk;\
		if (!chunk)\
			break

#define OUTPUT_END\
	}\
	free(tail);\
	free(path);\
	return FILES_DIFFER

static int
output_unified(struct file_data *old, struct file_data *new, const char *diff_line)
{
	struct trace *path;
	struct trace *path_;
	struct trace trace;
	size_t ai, bi;
	char **a;
	char **b;
	int suppressed = 1;

	OUTPUT_DIFF;
	OUTPUT_HEAD("---", "+++", get_time_string_unified);

	a = old->lines, b = new->lines, path_ = path;
	for (ai = bi = 0; (trace = *path++).f != END_OF_PATH;) {
		char f = trace.f;
		if (trace.d > n_context) {
			suppressed = 1;
			goto next;
		}
		if (suppressed) {
			suppressed = 0;
			cprintf("\033[1;34m@@ -%zu", ai + 1 - !trace.a_len);
			if (trace.a_len != 1)
				printf(",%zu", trace.a_len);
			printf(" +%zu", bi + 1 - !trace.b_len);
			if (trace.b_len != 1)
				printf(",%zu", trace.b_len);
			cprintf(" @@\033[m\n");
		}
		if (f == 0)
			printf(" %s\n", a[ai]);
		else
			cprintf("\033[3%im%c%s\033[m\n", (int)f, " -+"[(int)f], f == 1 ? a[ai] : b[bi]);
	next:
		ai += f != 2;
		bi += f != 1;
	}

	free(path_);
	return FILES_DIFFER;
}

static int
output_copied(struct file_data *old, struct file_data *new, const char *diff_line)
{
#define PRINT_PART(L, C, S, A, B)\
	cprintf("\033[1;3"#C"m"A" %zu", L##i + 1);\
	if (chunk->L##_len > 1)\
		printf(",%zu", L##i + chunk->L##_len);\
	cprintf(" "B"\033[m\n");\
	for (; have_##L && chunk->f != END_OF_PATH && chunk->d <= n_context; chunk++) {\
		if (chunk->f == 0)\
			printf("  %s\n", L[L##i]);\
		else if (chunk->f != (3 - C))\
			cprintf("\033[3%im%c %s\033[m\n", chunk->ch ? 3 : C, S"!"[chunk->ch], L[L##i]);\
		L##i += chunk->f != (3 - C);\
	}

	OUTPUT_BEGIN;
	OUTPUT_DIFF;
	OUTPUT_HEAD("***", "---", get_time_string_copied);
	OUTPUT_QUEUE;

	cprintf("\033[1;34m***************\033[m\n");
	chunk_old = chunk;
	PRINT_PART(a, 1, "-", "***", "****");
	chunk = chunk_old;
	PRINT_PART(b, 2, "+", "---", "----");

	OUTPUT_END;
#undef PRINT_PART
}

static int
output_normal(struct file_data *old, struct file_data *new, const char *diff_line)
{
#define PRINT_PART(L, C, S)\
	for (; have_##L && chunk->f != END_OF_PATH && chunk->d <= n_context; chunk++) {\
		if (chunk->f == 0)\
			printf("  %s\n", L[L##i]);\
		else if (chunk->f != (3 - C))\
			cprintf("\033[3"#C"m"S" %s\033[m\n", L[L##i]);\
		L##i += chunk->f != (3 - C);\
	}

	OUTPUT_BEGIN;
	OUTPUT_DIFF;
	OUTPUT_QUEUE;

	cprintf("\033[1;34m%zu", ai + 1 - !have_a);
	if (chunk->a_len > 1)
		printf(",%zu", ai + chunk->a_len);
	printf("%c", " dac"[have_a + 2 * have_b]);
	printf("%zu", bi + 1 - !have_b);
	if (chunk->b_len > 1)
		printf(",%zu", bi + chunk->b_len);
	cprintf("\033[m\n");

	chunk_old = chunk;
	PRINT_PART(a, 1, "<");
	if (have_a && have_b)
		cprintf("\033[34m---\033[m\n");
	chunk = chunk_old;
	PRINT_PART(b, 2, ">");

	OUTPUT_END;
#undef PRINT_PART
}

static int
output_ed(struct file_data *old, struct file_data *new, const char *diff_line)
{
	OUTPUT_BEGIN;
	OUTPUT_DIFF;
	OUTPUT_STACK;
	if (!have_b) {
		printf("%zud\n", ai + 1);
	} else {
		int have_dot = 0;
		printf("%zu", ai + 1 - !have_a);
		if (chunk->a_len > 1)
			printf(",%zu", ai + chunk->a_len);
		printf("%c\n", "ac"[chunk->ch]);
		for (; chunk->f != END_OF_PATH && chunk->d <= n_context; chunk++) {
			if (chunk->f != 1)
				cprintf("\033[3%im%s%s\033[m\n", chunk->ch ? 3 : 2,
				        b[bi][0] == '.' ? "." : "", b[bi]);
			have_dot = (chunk->f == 2 && b[bi][0] == '.');
			if (have_dot) {
				printf(".\ns/.//\n");
				if (chunk[1].f == 2)
					printf("a\n");
			}
			bi += chunk->f != 1;
		}
		if (!have_dot)
			printf(".\n");
	}
	OUTPUT_END;
}

static int
output_ed_alternative(struct file_data *old, struct file_data *new, const char *diff_line)
{
	OUTPUT_BEGIN;
	OUTPUT_DIFF;
	OUTPUT_QUEUE;
	if (!have_b) {
		printf("d%zu\n", ai + 1);
	} else {
		printf("%c%zu", "ac"[chunk->ch], ai + 1 - !have_a);
		if (chunk->a_len > 1)
			printf(" %zu", ai + chunk->a_len);
		printf("\n");
		for (; chunk->f != END_OF_PATH && chunk->d <= n_context; chunk++) {
			if (chunk->f != 1)
				cprintf("\033[3%im%s\033[m\n", chunk->ch ? 3 : 2, b[bi]);
			bi += chunk->f != 1;
		}
		printf(".\n");
	}
	OUTPUT_END;
}

/* } */


static char *
quote(const char *str)
{
	size_t i = 0, len = strlen(str);
	char *rc;

	rc = enmalloc(FAILURE, len * 4 + 3);

	rc[0] = '\"';
	for (i = 1; *str; str++) {
		switch (*str) {
		case '\a':  i += sprintf(rc + i, "\\a");   break;
		case '\b':  i += sprintf(rc + i, "\\b");   break;
		case '\t':  i += sprintf(rc + i, "\\t");   break;
		case '\n':  i += sprintf(rc + i, "\\n");   break;
		case '\v':  i += sprintf(rc + i, "\\v");   break;
		case '\f':  i += sprintf(rc + i, "\\f");   break;
		case '\r':  i += sprintf(rc + i, "\\r");   break;
		case '\"':  i += sprintf(rc + i, "\\\"");  break;
		case ' ':   i += sprintf(rc + i, "\\ ");   break;
		default:
			if ((*str & 0x80) || *str < ' ')
				i += sprintf(rc + i, "\\%03o", (unsigned)(unsigned char)*str);
			else
				rc[i++] = *str;
			break;
		}
	}
	rc[i++] = '\"';

	if (i - 2 == len)
		memmove(rc, rc + 1, i -= 2);

	rc[i] = 0;
	return rc;
}

static struct file_data *
load_lines(const char *pathname)
{
	int fd, bin = 0;
	char *buffer, *p, *end, *quoted;
	size_t ptr, size, n;
	ssize_t m;
	struct file_data* rc;
	struct stat attr;

	p = strrchr(pathname, '/');
	if (p && !p[1])
		return 0;

	fd = strcmp(pathname, "-") ? open(pathname, O_RDONLY) : STDIN_FILENO;
	if (fd == -1) {
		if (errno == EISDIR)
			return 0;
		enprintf(FAILURE, "open %s:", pathname);
	}

	if (fstat(fd, &attr))
		enprintf(FAILURE, "%s:", pathname);
	if (S_ISDIR(attr.st_mode))
		return 0;

	ptr = 0;
	size = attr.st_blksize ? attr.st_blksize : 8096;
	buffer = enmalloc(FAILURE, size + 1);
	for (;;) {
		if (ptr == size)
			buffer = enrealloc(FAILURE, buffer, (size <<= 1) + 1);
		m = read(fd, buffer + ptr, size - ptr);
		if (m < 0)
			enprintf(FAILURE, "read %s:", pathname);
		if (m == 0)
			break;
		ptr += (size_t)m;
	}
	buffer[ptr] = 0;

	for (n = 1, p = buffer;; n += 1) {
		char *lf = strchr(p, '\n');
		if (!lf)
			break;
		p = lf + 1;
	}
	bin = (strchr(p, '\0') != buffer + ptr);

	/* This is a bit ugly, if not done this way, it would require unnecessarily many
	 * malloc:s to create rc and unnecessarily many free:s to destroy it. */
	quoted = quote(pathname);
	rc = enrealloc(FAILURE, buffer,
	               sizeof(*rc) + (n + 1) * sizeof(char *) +
	               (ptr + 1 + sizeof(COLOURED_NO_LF_MARK)) + strlen(quoted) + 1);
	buffer = ((char *)rc) + sizeof(*rc) + (n + 1) * sizeof(char *);
	memmove(buffer, rc, ptr);
	rc->lines = (char **)((char *)rc + sizeof(*rc));
	rc->lf_terminated = ptr && buffer[ptr - 1] == '\n';
	rc->line_count = bin ? ptr : (n -= rc->lf_terminated);
	buffer[ptr - rc->lf_terminated] = 0;
	rc->attr = attr;
	rc->path = pathname;
	rc->path_quoted = buffer + ptr + 1 + sizeof(COLOURED_NO_LF_MARK);
	strcpy(rc->path_quoted, quoted);
	rc->is_binary = bin;
	rc->is_empty = (ptr == 0);
	free(quoted);

	close(fd);

	n = bin ? n : 1;
	rc->lines[n] = 0;
	if (bin) {
		rc->lines[0] = buffer;
	} else {
		for (ptr = 0, p = buffer; p; p = end) {
			end = strchr(p, '\n');
			if (end)
				*end++ = 0;
			rc->lines[ptr++] = p;
		}
	}

	return rc;
}

static int
do_binaries_differ(struct file_data *old, struct file_data *new)
{
	struct file_data *f = old;
	do if (!f->is_binary) {
		char **lines = f->lines;
		size_t len = 0, part_len;
		for (; *lines; lines++) {
			len += 1 + (part_len = strlen(*lines));
			(*lines)[part_len] = '\n';
		}
		f->line_count = len - !f->lf_terminated;
	} while (f == old ? (f = new) : (void*)0);

	if (old->line_count != new->line_count)
		return 1;

	return memcmp(old->lines[0], new->lines[0], old->line_count);
}

static int
compare_files(struct file_data *old, struct file_data *new, const char *diff_line)
{
	int ret;

	if (old->is_binary || new->is_binary) {
		if (do_binaries_differ(old, new)) {
			cprintf("\033[1mBinary files %s and %s differ\033[m\n", old->path, new->path);
			return BINARIES_DIFFER;
		}
		return 0;
	}

	if (old->is_empty && new->is_empty)
		return 0;

	if (!eflag && !fflag) {
		if (!old->lf_terminated && !old->is_empty)
			strcat(old->lines[old->line_count - 1], use_colour ? COLOURED_NO_LF_MARK : NO_LF_MARK);
		if (!new->lf_terminated && !new->is_empty)
			strcat(new->lines[new->line_count - 1], use_colour ? COLOURED_NO_LF_MARK : NO_LF_MARK);
	}

	ret = (uflag ? output_unified :
	       cflag ? output_copied :
	       eflag ? output_ed :
	       fflag ? output_ed_alternative :
	               output_normal)(old, new, diff_line);

	if ((eflag || fflag) && FILES_DIFFER <= ret && ret <= NOT_MINIMAL) {
		if (!old->lf_terminated && !old->is_empty) {
			ret = FAILURE;
			fprintf(stderr, "%s: %s: No newline at end of file\n\n", argv0, old->path);
		}
		if (!new->lf_terminated && !new->is_empty) {
			ret = FAILURE;
			fprintf(stderr, "%s: %s: No newline at end of file\n\n", argv0, new->path);
		}
	}

	return ret;
}

static int
compare_directories(const char *old, const char *new, const char *diff_line)
{
	int ret = 0, r, i = 0, j = 1;
	DIR *dir;
	const char *paths[2] = { old, new };
	struct dirent *file;
	struct file_data *a;
	struct file_data *b;
	char *b_path;
	char *a_path;
	struct stat a_attr;
	struct stat b_attr;

again:
	dir = opendir(paths[i]);
	if (!dir)
		enprintf(FAILURE, "opendir %s:", paths[i]);
	while ((errno = 0, file = readdir(dir))) {
		if (!strcmp(file->d_name, ".") || !strcmp(file->d_name, ".."))
			continue;
		b_path = join_paths(paths[j], file->d_name);
		if (access(b_path, F_OK)) {
			cprintf("\033[1mOnly in %s: %s\033[m\n", paths[i], file->d_name);
			ret = ret > FILES_DIFFER ? ret : FILES_DIFFER;
			goto next;
		} else if (i == 1) {
			goto next;
		}
		a_path = join_paths(paths[i], file->d_name);

		if (stat(a_path, &a_attr))  enprintf(FAILURE, "stat %s:", a_path);
		if (stat(b_path, &b_attr))  enprintf(FAILURE, "stat %s:", b_path);

		if (a_attr.st_dev == b_attr.st_dev && a_attr.st_ino == b_attr.st_ino)
			goto skip;
		if (is_incommensurable(a_attr.st_mode) || is_incommensurable(b_attr.st_mode))
			goto skip;

		a = load_lines(a_path);
		b = load_lines(b_path);

		if (!a ^ !b) {
			cprintf("\033[1mFile %s is a %s while file %s is a %s\033[m\n",
			       a_path, classify(a), b_path, classify(b));
			r = FILES_DIFFER;
		} else if (!a && !b && !rflag) {
			cprintf("\033[1mCommon subdirectories: %s and %s\033[m\n", a_path, b_path);
			r = 0;
		} else if (!a && !b) {
			r = compare_directories(a_path, b_path, diff_line);
		} else {
			r = compare_files(a, b, diff_line);
		}
		ret = ret > r ? ret : r;

		free(a);
		free(b);
	skip:
		free(a_path);
	next:
		free(b_path);
	}
	if (errno)
		enprintf(FAILURE, "readdir %s:", paths[i]);
	closedir(dir);


	if (i)
		return ret;
	i = 1, j = 0;
	goto again;
}

int
main(int argc, char *argv[])
{
	struct file_data *old;
	struct file_data *new;
	char *old_proper = 0;
	char *new_proper = 0;
	int ret;
	char *diff_line = 0;
	char *p;

	/* Construct the 'diff options file1 file2' line used diff:ing directories. */
	if (argc > 2) {
		size_t len = 5;
		int i;
		for (i = 1; i < argc - 2; i++)
			len += strlen(argv[i]) + 1;
		p = diff_line = enmalloc(FAILURE, len + 1);
		p += sprintf(p, "diff ");
		for (i = 1; i < argc - 2; i++)
			p += sprintf(p, "%s ", argv[i]);
		p[-1] = 0;
	}

	ARGBEGIN {
	case 'b':  bflag++;  break;
	case 'c':  cflag++;  n_context = 3;                     break;
	case 'C':  cflag++;  n_context = atol(EARGF(usage()));  break;
	case 'e':  eflag++;  break;
	case 'f':  fflag++;  break;
	case 'u':  uflag++;  n_context = 3;                     break;
	case 'U':  uflag++;  n_context = atol(EARGF(usage()));  break;
	case 'r':  rflag++;  break;
	case 'd':  dflag++;  break;
	case 'D':  use_colour++;  break;
	default:
		usage();
	} ARGEND;
	/* Use of `atol` is intentional, '-U -1' and '-C -1' shall display the entire file.
	 * This is a not specified in POSIX, but appears in other implementations and is
	 * useful whilst removing complexity. */

	if (argc != 2 || (bflag | rflag) > 1 || cflag + eflag + fflag + uflag > 1)
		usage();

	if (!strcmp(argv0, "bdiff")) {
		bdiff = 1;
	} else {
		p = strrchr(argv0, '/');
		if (p && !strcmp(p, "/bdiff"))
			bdiff = 1;
	}

	use_colour = use_colour == 1 ? isatty(STDOUT_FILENO) : use_colour;
	cprintf = use_colour ? printf : ncprintf;

redo:
	old = load_lines(old_proper ? old_proper : argv[0]);
	new = load_lines(new_proper ? new_proper : argv[1]);

	if ((old_proper || new_proper) && (!old || !new)) {
		cprintf("\033[1mFile %s is a %s while file %s is a %s\033[m\n",
		       old_proper ? old_proper : argv[0], classify(old),
		       new_proper ? new_proper : argv[1], classify(new));
		ret = 1;
	} else if (!old && new) {
		old_proper = join_paths(argv[0], basename(argv[1]));
		goto redo;
	} else if (old && !new) {
		new_proper = join_paths(argv[1], basename(argv[0]));
		goto redo;
	} else if (!old && !new) {
		ret = compare_directories(argv[0], argv[1], diff_line);
	} else {
		ret = compare_files(old, new, 0);
	}

	if (fshut(stdout, "<stdout>"))
		ret = FAILURE;

	free(old);
	free(new);
	free(old_proper);
	free(new_proper);
	free(diff_line);

	if (!bdiff && cheap_algorithm_used && ret == FILES_DIFFER)
		ret = NOT_MINIMAL;

	return ret;
}
