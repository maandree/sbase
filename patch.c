/* See LICENSE file for copyright and license details. */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include "text.h"
#include "util.h"

/*
 * Petty POSIX violations:
 * -  Rejected hunks are saved without timestamps.
 * -  Questions are prompted to /dev/tty rather than to /dev/stdout.
 * -  -N skips applied hunks rather than applied patches.
 * -  If -l is used leading whitespace is ignored.
 *    (we interpret that trailing whitespace should be ignored.)
 * -  Files in /dev/ are not considered as existing files. This is
 *    checked before -p and -d are applied.
 * 
 * I have choosen not to support git diff-files.
 * CVE-2010-4651 is recognised as user error, not as a security issue.
 */

#define NO_LF_MARK         "\\ No newline at end of file"

#define PATH(p)            ((p) == stdin_dash ? "/dev/stdin" : (p) == stdout_dash ? "/dev/stdout" : (p))
#define isnulblank(c)      (!(c) || isblank(c))
#define issamefile(a, b)   ((a).st_dev == (b).st_dev && (a).st_ino == (b).st_ino)
#define storefile(d, s)    ((d)->st_dev = (s).st_dev, (d)->st_ino = (s).st_ino)
#define strstart(h, n)     ((h) && strstr(h, n) == (h))
#define linestart(h, n)    (strstart((h).data, n))
#define anychr(cs, c)      ((c) && strchr(cs, c))
#define containsnul(l)     ((size_t)(strchr((l).data, '\0') - (l).data) < (l).len)
#define lineeq(l, s)       ((l).len == sizeof(s) - 1 && !memcmp((l).data, s, sizeof(s) - 1))
#define linecpy2mem(d, s)  (memcpy(d, (s).data, (s).len + 1))
#define missinglf(l)       ((l).len && (l).data[(l).len - 1] != '\n')
#define fwriteline(f, l)   (fwrite((l).data, 1, (l).len, f))
#define enmemdup(f, s, n)  ((n) ? memcpy(enmalloc(f, n), s, n) : 0)

enum { REJECTED = 1, FAILURE = 2 };
enum applicability { APPLICABLE, APPLIED, INAPPLICABLE };
enum format { NORMAL, COPIED, UNIFIED, ED, GARBAGE, EMPTY, GUESS };

struct hunk_content {
	size_t start;
	size_t len;
	struct line *lines;
};

struct parsed_hunk {
	struct hunk_content old;
	struct hunk_content new;
	char *annot;
	/* Symbols for `.annot`:
	 *   ' ' = context
	 *   '-' = removal
	 *   '+' = addition
	 *   '<' = change, removal
	 *   '>' = change, addition */
	char *rannot;
};

struct hunk {
	struct line *head;
	struct line *lines;
	size_t nlines;
};

struct patch {
	char *path;
	enum format format;
	struct hunk *hunks;
	size_t nhunks;
};

struct patchset {
	struct patch *patches;
	size_t npatches;
};

struct line_data {
	char new;
	char orig;
	size_t nold;
	struct line line;
	struct line *old;
};

struct file_data {
	struct line_data *d;
	size_t n;
};

struct patched_file {
	dev_t st_dev;
	ino_t st_ino;
	struct file_data *data;
};

static enum format specified_format = GUESS;
static const char *patchfile = 0;
static char *rejectfile = 0;
static const char *outfile = 0;
static char *apply_patches_to = 0;
static size_t pflag = SIZE_MAX;
static int bflag = 0;
static int fflag = 0;
static int lflag = 0;
static int Rflag = 0;
static int Nflag = 0;
static int Uflag = 0;
static char *dflag = 0;
static int rejected = 0;
static struct patched_file *prevpatch = 0;
static size_t prevpatchn = 0;
static struct patched_file *prevout = 0;
static size_t prevoutn = 0;
static char stdin_dash[sizeof("-")];
static char stdout_dash[sizeof("-")];
static char *ifdef = 0;
static char *ifndef = 0;

static void
usage(void)
{
	enprintf(FAILURE, "usage: %s [-c | -e | -n | -u] [-d dir] [-D define] [-o outfile] [-p num] "
		 "[-r rejectfile] [-bflNRU] (-i patchfile | < patchfile) [file]\n", argv0);
}

static void
load_lines(const char *path, struct file_data *out, int skip_lf, int orig)
{
	FILE *f;
	struct linebuf b = EMPTY_LINEBUF;
	size_t i, n;

	if (!(f = path && path != stdin_dash ? fopen(path, "r") : stdin))
		enprintf(FAILURE, "fopen %s:", path);
	ngetlines(FAILURE, f, &b);
	fshut(f, f == stdin ? "<stdin>" : path);

	out->n = n = b.nlines;
	out->d = encalloc(FAILURE, n + 1, sizeof(*out->d));
	for (i = 0; i < n; i++) {
		out->d[i].line = b.lines[i];
		out->d[i].orig = orig;
	}
	free(b.lines);

	if (b.nolf) {
		n--;
		out->d[n].line.data[--(out->d[n].line.len)] = '\0';
	}
	while (skip_lf && n--)
		out->d[n].line.data[--(out->d[n].line.len)] = '\0';
}

static char *
ask(const char *instruction)
{
	FILE *f;
	char *answer = 0;
	size_t size = 0;
	ssize_t n;

	if (fflag)
		return 0;

	if (!(f = fopen("/dev/tty", "r+")))
		enprintf(FAILURE, "fopen /dev/tty:");

	if (fprintf(f, "%s: %s: %s: ", argv0, patchfile ? patchfile : "-", instruction) < 0)
		enprintf(FAILURE, "printf /dev/tty:");
	fflush(stdout);

	if ((n = getline(&answer, &size, f)) <= 0) {
		answer = 0;
	} else {
		n -= (answer[n - 1] == '\n');
		answer[n] = 0;
		if (!*answer) {
			free(answer);
			answer = 0;
		}
	}

	fclose(f);
	return answer;
}

static char *
adjust_filename(const char *filename)
{
	size_t strips = pflag;
	const char *p = strchr(filename, '\0');
	const char *stripped = filename;
	char *rc;

	if (p == filename || p[-1] == '/')
		return 0;

	for (; strips && (p = strchr(stripped, '/')); strips--)
		for (stripped = p; *stripped == '/'; stripped++);
	if (strips && pflag != SIZE_MAX)
		return 0;

	if (dflag && *stripped != '/')
		enasprintf(FAILURE, &rc, "%s/%s", dflag, stripped);
	else
		rc = enstrdup(FAILURE, stripped);

	return rc;
}

static int
file_exists(const char *filename)
{
	char *adjusted;
	int ret;

	if (strstart(filename, "/dev/"))
		return 0;

	adjusted = adjust_filename(filename);
	ret = adjusted ? !access(adjusted, F_OK) : 0;
	free(adjusted);
	return ret;
}

static int
unquote(char *str)
{
	int quote = 0, escape = 0, value;
	size_t r = 0, w = 0, i;

	for (; str[r]; r++) {
		if (escape) {
			escape = 0;
			switch (str[r++]) {
			case 'a':  str[w++] = '\a';  break;
			case 'b':  str[w++] = '\b';  break;
			case 'e':  str[w++] = '\033';  break;
			case 'f':  str[w++] = '\f';  break;
			case 'n':  str[w++] = '\n';  break;
			case 'r':  str[w++] = '\r';  break;
			case 't':  str[w++] = '\t';  break;
			case 'v':  str[w++] = '\v';  break;
			case 'x':
				for (i = value = 0; i < 2; i++, r++) {
					if (!isxdigit(str[r]))
						return -1;
					value *= 16;
					value += (str[r] & 15) + 9 * !isdigit(str[r]);
				}
				str[w++] = value;
				break;
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7': case 'o':
				r -= str[r - 1] != 'o';
				for (i = value = 0; i < 3; i++, r++) {
					if (!anychr("01234567", str[r]))
						return -1;
					value = value * 8 + (str[r] & 7);
				}
				str[w++] = value;
				break;
			default:
				str[w++] = str[r];
				break;
			}
		} if (str[r] == '\"') {
			quote ^= 1;
		} else if (str[r] == '\\') {
			escape = 1;
		} else {
			str[w++] = str[r];
		}
	}

	str[w] = 0;
	return 0;
}

static int
parse_diff_line(char *str, char **old, char **new)
{
	/*
	 * We will just assume that the two last arguments in `str` are sanely
	 * formatted. All other ways to parse it will surely result in utter madness.
	 * 
	 * The POSIX standard does not actually require or suggest supporting this,
	 * but it is required to figure out the filename automatically for some diff
	 * outputs since diff(1p) does not output the "Index:" string...
	 */

	char *s = strchr(str, '\0');
	int ret = 0;

	*new = 0;
	if (s == str)
		return -1;

again:
	if (*s != '\"') {
		while (--s != str)
			if (*s == ' ')
				goto found;
	} else {
		while (--s != str && s - 1 != str)
			if (s[-1] == ' ' && s[0] == '"')
				goto found;
	}

	free(*new);
	return -1;

found:
	*s++ = '\0';
	if (!*new) {
		*new = enstrdup(FAILURE, s);
		s--;
		goto again;
	} else {
		*old = enstrdup(FAILURE, s);
		s = strchr(s, '\0');
		*s = ' ';
	}

	ret |= unquote(*old);
	ret |= unquote(*new);

	if (ret) {
		free(*old);
		free(*new);
	}

	return ret;
}

static void
ask_for_filename(struct patchset *patchset)
{
	size_t i;
	char *answer;

	for (i = 0; i < patchset->npatches; i++)
		if (!patchset->patches[i].path)
			goto found_unset;
	return;

found_unset:
	if (!(answer = ask("please enter name of file to patch")))
		exit(FAILURE);

	/* Two assumtions are made here. (1) if there are multiple
	 * patches to unnamed failes, they are all to the same file,
	 * (with the specifications for -o to justify this assumption,)
	 * and (2) no developer is going to free the filenames. */

	if (access(answer, F_OK)) {
		free(answer);
		enprintf(FAILURE, "selected file does not exist.\n");
	}

	for (i = 0; i < patchset->npatches; i++)
		if (!patchset->patches[i].path)
			patchset->patches[i].path = answer;
}

static int
ask_for_reverse(void)
{
	char *answer = ask(Rflag ?
	                   "unreversed patch detected, ignore -R? [n]" :
	                   "reversed or previously applied patch detected, assume -R? [n]");
	return answer && strcasecmp(answer, "n") && strcasecmp(answer, "no");
}

static void
reverse_hunk(struct parsed_hunk *hunk)
{
	struct hunk_content cont;
	char *annot;
	cont = hunk->old, hunk->old  = hunk->new, hunk->new = cont;
	annot = hunk->annot, hunk->annot = hunk->rannot, hunk->rannot = annot;
}

static struct file_data *
get_last_patch(const char *path)
{
	struct stat attr;
	struct file_data *data;
	size_t i;

	if (!outfile && !ifdef) {
		data = enmalloc(FAILURE, sizeof(*data));
		load_lines(path, data, 0, 0);
		return data;
	}

	if (stat(PATH(path), &attr))
		enprintf(FAILURE, "stat %s:", path);

	for (i = 0; i < prevpatchn; i++) {
		if (issamefile(prevpatch[i], attr)) {
			if (!prevpatch[i].data) {
				prevpatch[i].data = enmalloc(FAILURE, sizeof(*prevpatch->data));
				data = prevpatch[i].data;
				goto load_data;
			}
			return prevpatch[i].data;
		}
	}

	prevpatch = enrealloc(FAILURE, prevpatch, (prevpatchn + 1) * sizeof(*prevpatch));
	storefile(prevpatch + prevpatchn, attr);
	prevpatch[prevpatchn++].data = data = enmalloc(FAILURE, sizeof(*prevpatch->data));

load_data:
	load_lines(path, data, 0, 1);
	return data;
}

static int
is_first_patch(const char *path)
{
	struct stat attr;
	size_t i;

	if (stat(PATH(path), &attr))
		enprintf(FAILURE, "stat %s:", path);

	for (i = 0; i < prevpatchn; i++)
		if (issamefile(prevpatch[i], attr))
			return 0;

	prevpatch = enrealloc(FAILURE, prevpatch, (prevpatchn + 1) * sizeof(*prevpatch));
	storefile(prevpatch + prevpatchn, attr);
	prevpatch[prevpatchn].data = 0;
	prevpatchn++;

	return 1;
}

static const char *
get_fopen_mode(const char *path)
{
	int iteration = 0, fd;
	struct stat attr;
	struct stat attr_stdout;
	size_t i;

	if (!path)
		return "w";
	if (path == stdout_dash)
		return "a";
	if (!stat(path, &attr)) {
		 if (stat("/dev/stdout", &attr_stdout))
			enprintf(FAILURE, "stat /dev/stdout:");
		 if (issamefile(attr, attr_stdout))
			return "a";
	}

start_over:
	iteration++;

	if (stat(path, &attr)) {
		if (errno == ENOENT && iteration == 1) {
			if ((fd = open(path, O_CREAT | O_WRONLY, 0666)) < 0)
				enprintf(FAILURE, "open %s:", path);
			close(fd);
			goto start_over;
		} else {
			enprintf(FAILURE, "stat %s:", path);
		}
	}

	for (i = 0; i < prevoutn; i++)
		if (issamefile(prevout[i], attr))
			return "a";

	prevout = enrealloc(FAILURE, prevout, (prevoutn + 1) * sizeof(*prevout));
	storefile(prevout + prevoutn, attr);
	prevout[prevoutn].data = 0;
	prevoutn++;

	return "w";
}

static enum format
guess_format(struct line *lines)
{
	char *str = lines[0].data;
	size_t commas = 0;

	if (!str)            return EMPTY;
	if (*str == '*')     return COPIED;
	if (*str == '-')     return UNIFIED;
	if (!isdigit(*str))  return GARBAGE;

	while (isdigit(*str) || *str == ',')
		commas += *str++ == ',';

	if (commas > 2 || !anychr("acdi", *str))
		return GARBAGE;

	if (isdigit(str[1]))               return NORMAL;
	if (strchr("ai", *str) && commas)  return GARBAGE;
	if (!str[1] && commas < 2)         return ED;

	return GARBAGE;
}

static char *
get_filename_from_context_header(char *line)
{
	char *end, old_end, *rc;
	int quote = 0, escape = 0;

	for (line += 4; isblank(*line); line++);

	for (end = line; *end; end++) {
		if (escape || *end == '\\')
			escape ^= 1;
		else if (*end == '"')
			quote ^= 1;
		else if (!quote && isblank(*end))
			break;
	}

	old_end = *end, *end = 0;
	rc = enstrdup(FAILURE, line);
	*end = old_end;

	return unquote(rc) ? 0 : rc;
}

static int
parse_context_header(struct patch *patch, struct line *lines, const char *old_mark, const char *new_mark)
{
	char *candidate, *adjusted;
	int i, found = !!apply_patches_to;
	const char *marks[] = {old_mark, new_mark};

	for (i = 0; i < 2; i++, lines++) {
		if (!lines->data ||
		    containsnul(*lines) ||
		    !strstart(lines->data, marks[i]) ||
		    !isblank(lines->data[strlen(marks[i])]) ||
		    !(candidate = get_filename_from_context_header(lines->data)))
			return -1;
		if (!found &&
		    file_exists(candidate) &&
		    (adjusted = adjust_filename(candidate))) {
			free(patch->path);
			patch->path = adjusted;
			found = 1;
		}
		free(candidate);
	}

	return 0;
}

static char *
parse_range(char *str, size_t *first, size_t *last, int *have_last)
{
	errno = 0;
	*first = strtoul(str, &str, 10);
	if (errno)
		return 0;
	*last = 1;
	if (have_last)
		*have_last = *str == ',';
	if (*str == ',') {
		errno = 0;
		*last = strtoul(str + 1, &str, 10);
		if (errno)
			return 0;
	}
	return str;
}

static size_t
parse_patch_normal(struct patch *patch, struct line *lines)
{
	struct line *lines_orig = lines;
	char *p, action;
	size_t n, a_first, a_last = 0, b_first, b_last = 0, added, deleted;
	int have_a_last, have_b_last;
	struct hunk hunk;

	patch->nhunks = 0;
	patch->hunks = 0;

	while (guess_format(lines) == NORMAL) {
		added = deleted = 0;

		hunk.head = lines;
		if (!(p = parse_range(lines++->data, &a_first, &a_last, &have_a_last)) ||
		    (action = *p++, !anychr("acd", action)) ||
		    !(p = parse_range(p, &b_first, &b_last, &have_b_last)) ||
		    p != hunk.head->data + hunk.head->len ||
		    !(hunk.lines = lines)->data ||
		    (have_a_last && a_first > a_last) ||
		    (have_b_last && b_first > b_last))
			goto out;

		if (have_a_last) {
			for (n = a_last - a_first + 1; n--; lines++, deleted++)
				if (!linestart(*lines, "< "))
					goto out;
		} else {
			if (linestart(*lines, "> "))   goto old_done;
			if (lineeq(*lines, "---"))     goto check_delimited;
			if (!linestart(*lines, "< "))  goto out;
			lines++, deleted++;
		}

		if (!lines->data || !(lines += *lines->data == '\\')->data)
			goto new_done;

	check_delimited:
		if (action == 'c' && !lineeq(*lines, "---"))
			goto out;
		lines += action == 'c';

	old_done:
		if (have_b_last) {
			for (n = b_last - b_first + 1; n--; lines++, added++)
				if (!linestart(*lines, "> "))
					goto out;
			if (lines->data && *lines->data == '\\')
				lines++;
		} else if (linestart(*lines, "> ")) {
			added++;
			if ((++lines)->data && *lines->data == '\\')
				lines++;
		}

	new_done:
		if ((action == 'd' && (added || !deleted)) ||
		    (action == 'a' && (!added || deleted)) ||
		    (action == 'c' && (!added || !deleted)))
			goto out;

		hunk.nlines = (size_t)(lines - hunk.lines);

		patch->hunks = enrealloc(FAILURE, patch->hunks, (patch->nhunks + 1) * sizeof(hunk));
		patch->hunks[patch->nhunks++] = hunk;
	}

	return lines - lines_orig;

out:
	if (patch->nhunks) {
		lines = patch->hunks[patch->nhunks - 1].lines;
		lines += patch->hunks[patch->nhunks - 1].nlines;
		return lines - lines_orig;
	} else {
		return 0;
	}
}

static size_t
parse_patch_copied(struct patch *patch, struct line *lines)
{
	struct line *lines_orig = lines;
	char *p;
	size_t n, first, last = 0;
	int have_last;
	struct hunk hunk;

	patch->nhunks = 0;
	patch->hunks = 0;

	if (parse_context_header(patch, lines, "***", "---"))
		return 0;
	lines += 2;

#define LINE  (lines->data)

	while (linestart(*lines, "***************")) {
		if (!lineeq(*lines, "***************") && !isnulblank(LINE[sizeof("***************") - 1]))
			break;

		hunk.head = lines++;
		hunk.lines = lines;

		if (!linestart(*lines, "*** ") ||
		    !(p = parse_range(LINE + 4, &first, &last, &have_last)) ||
		    strcmp(p, " ****") || (size_t)(p + 5 - LINE) < lines->len ||
		    !(++lines)->data)
			goto out;

		if (linestart(*lines, "--- "))
			goto old_done;
		if (have_last) {
			for (n = last - first + 1; n--; lines++)
				if (!LINE || !anychr(" -!", *LINE) || LINE[1] != ' ')
					goto out;
		} else {
			if (!anychr(" -!", *LINE))
				goto out;
			lines++;
		}

	old_done:
		if (!LINE)
			goto out;
		if (*LINE == '\\')
			lines++;

		if (!linestart(*lines, "--- ") ||
		    !(p = parse_range(LINE + 4, &first, &last, &have_last)) ||
		    strcmp(p, " ----") || (size_t)(p + 5 - LINE) < lines->len)
			goto out;

		if (!(++lines)->data || !anychr(" +!", *LINE) || LINE[1] != ' ')
			goto new_done;
		if (have_last) {
			for (n = last - first + 1; n--; lines++)
				if (!LINE || !anychr(" +!", *LINE) || LINE[1] != ' ')
					goto out;
			if (LINE && *LINE == '\\')
				lines++;
		} else if (anychr(" +!", *LINE) && LINE[1] == ' ') {
			if ((++lines)->data && *LINE == '\\')
				lines++;
		}

	new_done:
		hunk.nlines = (size_t)(lines - hunk.lines);

		patch->hunks = enrealloc(FAILURE, patch->hunks, (patch->nhunks + 1) * sizeof(hunk));
		patch->hunks[patch->nhunks++] = hunk;
	}

#undef LINE

	return lines - lines_orig;

out:
	if (patch->nhunks) {
		lines = patch->hunks[patch->nhunks - 1].lines;
		lines += patch->hunks[patch->nhunks - 1].nlines;
		return lines - lines_orig;
	} else {
		return 0;
	}
}

static size_t
parse_patch_unified(struct patch *patch, struct line *lines)
{
	struct line *lines_orig = lines;
	char *p;
	size_t a_count, b_count, unused;
	struct hunk hunk;

	(void) unused;

	patch->nhunks = 0;
	patch->hunks = 0;

	if (parse_context_header(patch, lines, "---", "+++"))
		return 0;
	lines += 2;

	while (linestart(*lines, "@@ -")) {
		hunk.head = lines++;
		hunk.lines = lines;

		if (!(p = parse_range(hunk.head->data + 4, &unused, &a_count, 0)) ||
		    !strstart(p, " +") ||
		    !(p = parse_range(p + 2, &unused, &b_count, 0)) ||
		    !strstart(p, " @@") || !isnulblank(p[3]))
			goto out;

		for (; a_count || b_count; lines++) {
			if (!lines->data)
				goto out;
			else if (*lines->data == '\\')
				continue;
			else if (a_count && *lines->data == '-')
				a_count--;
			else if (b_count && *lines->data == '+')
				b_count--;
			else if (a_count && b_count && *lines->data == ' ')
				a_count--, b_count--;
			else
				goto out;
		}
		if (lines->data && *lines->data == '\\')
			lines++;

		hunk.nlines = (size_t)(lines - hunk.lines);

		patch->hunks = enrealloc(FAILURE, patch->hunks, (patch->nhunks + 1) * sizeof(hunk));
		patch->hunks[patch->nhunks++] = hunk;
	}

	return lines - lines_orig;

out:
	if (patch->nhunks) {
		lines = patch->hunks[patch->nhunks - 1].lines;
		lines += patch->hunks[patch->nhunks - 1].nlines;
		return lines - lines_orig;
	} else {
		return 0;
	}
}

static size_t
parse_patch_ed(struct patch *patch, struct line *lines)
{
	struct line *lines_orig = lines, *lines_last = lines, *last_addition;

	while (guess_format(lines) == ED && !containsnul(*lines)) {
		if (lines->data[lines->len - 1] == 'd') {
			lines_last = ++lines;
			continue;
		}
		last_addition = 0;
	again:
		while ((++lines)->data && !lineeq(*lines, "."))
			last_addition = lines;
		if (!lines++->data)
			break;
		if (lines->data && *lines->data == 's') {
			if ((!lineeq(*lines, "s/.//")  && !lineeq(*lines, "s/\\.//") &&
			     !lineeq(*lines, "s/^.//") && !lineeq(*lines, "s/^\\.//")) ||
			    !last_addition || !last_addition->len || last_addition->data[0] != '.') {
				/* This is just so parse_ed_script does not have too be overkill. */
				weprintf("suspicious line in ed-script, treating hunk as garbage: ");
				fwriteline(stderr, *lines);
				fprintf(stderr, "\n");
				break;
			}
			if ((++lines)->data && lineeq(*lines, "a")) {
				lines++;
				goto again;
			}
		}
		lines_last = lines;
	}

	if (lines_last == lines_orig)
		return 0;

	patch->nhunks = 1;
	patch->hunks = enmalloc(FAILURE, sizeof(*(patch->hunks)));
	patch->hunks->head = 0;
	patch->hunks->lines = lines_orig;
	patch->hunks->nlines = (size_t)(lines_last - lines_orig);

	return patch->hunks->nlines;
}

static size_t
parse_patch(struct patch *patch, struct line *lines)
{
	switch (patch->format) {
	case NORMAL:   return parse_patch_normal(patch, lines);
	case COPIED:   return parse_patch_copied(patch, lines);
	case UNIFIED:  return parse_patch_unified(patch, lines);
	case ED:       return parse_patch_ed(patch, lines);
	default:
		abort();
		return 0;
	}
}

static void
parse_patchfile(struct patchset *ps, struct line *lines)
{
	int only_garbage = 1;
	size_t i, n, old_garbage, garbage_since_last_patch = 0;
	enum format format;
	char *diff_line;
	char *index_line;
	char *index_line_dup;
	struct patch *patch;
	char *oldfile, *newfile;

	memset(ps, 0, sizeof(*ps));

	if (!lines->data) {
		/* Other implementations accept empty files, probably
		 * because diff(1p) can produce empty patchfiles. */
		enprintf(0, "%s: patchfile is empty, accepted.\n", patchfile ? patchfile : "-");
	}

	for (; lines->data; lines++) {
		format = guess_format(lines);
		if (format == GARBAGE ||
		    (specified_format != GUESS && format != specified_format)) {
			garbage_since_last_patch++;
			continue;
		}

		diff_line = index_line = 0;
		for (i = 1; i <= garbage_since_last_patch; i++) {
			if (!diff_line && linestart(lines[-i], "diff ") && !containsnul(lines[-i]))
				diff_line = lines[-i].data;
			if (!index_line && linestart(lines[-i], "Index:") && !containsnul(lines[-i]))
				index_line = lines[-i].data;
		}
		old_garbage = garbage_since_last_patch;
		garbage_since_last_patch = 0;

		ps->patches = enrealloc(FAILURE, ps->patches, (ps->npatches + 1) * sizeof(*patch));
		patch = ps->patches + ps->npatches++;
		memset(patch, 0, sizeof(*patch));

		patch->path = apply_patches_to;

		if (!patch->path && diff_line &&
		    !parse_diff_line(diff_line, &oldfile, &newfile)) {
			if (file_exists(oldfile))
				patch->path = adjust_filename(oldfile);
			else if (file_exists(newfile))
				patch->path = adjust_filename(newfile);
			free(oldfile);
			free(newfile);
		}
		if (!patch->path && index_line) {
			index_line += sizeof("Index:") - 1;
			if (!isblank(*index_line))
				goto skip_index_line;
			while (isblank(*index_line))
				index_line++;
			index_line_dup = enstrdup(FAILURE, index_line);
			if (unquote(index_line_dup))
				goto skip_index_line;
			if (file_exists(index_line_dup))
				patch->path = adjust_filename(index_line_dup);
			free(index_line_dup);
		}

	skip_index_line:
		patch->format = format;
		n = parse_patch(patch, lines);
		if (!n) {
			garbage_since_last_patch = old_garbage + 1;
			ps->npatches--;
		} else {
			lines += n;
			lines--;
			only_garbage = 0;
		}

		if (Rflag && format == ED)
			enprintf(FAILURE, "-R cannot be used with ed scripts.\n");

		/* TODO Step 4 of "Filename Determination" under EXTENDED DESCRIPTION in patch(1p). */
	}

	if (only_garbage)
		enprintf(FAILURE, "%s: patchfile contains only garbage.\n", patchfile ? patchfile : "-");
}

static void
save_file_cpp(FILE *f, struct file_data *file)
{
	size_t i, j, n;
	char annot = ' ';

	for (i = 0; i <= file->n; i++) {
		if ((n = file->d[i].nold)) {
			fprintf(f, "%s\n", annot == '+' ? "#else" : ifndef);
			for (j = 0; j < n; j++) {
				fwriteline(f, file->d[i].old[j]);
				if (missinglf(file->d[i].old[j]))
					fprintf(f, "\n");
			}
			annot = '-';
		}
		if (i == file->n)
			break;
		if (annot == '-')
			fprintf(f, "%s\n", file->d[i].new ? "#else" : "#endif");
		else if (annot == ' ' && file->d[i].new)
			fprintf(f, "%s\n", ifdef);
		else if (annot == '+' && !file->d[i].new)
			fprintf(f, "#endif\n");
		fwriteline(f, file->d[i].line);
		if ((i + 1 < file->n || file->d[i].new) && missinglf(file->d[i].line))
			fprintf(f, "\n");
		annot = file->d[i].new ? '+' : ' ';
	}
	if (annot != ' ')
		fprintf(f, "#endif\n");
}

static void
save_file(FILE *f, struct file_data *file)
{
	size_t i;
	for (i = 0; i < file->n; i++) {
		fwriteline(f, file->d[i].line);
		if (i + 1 < file->n && missinglf(file->d[i].line))
			fprintf(f, "\n");
	}
}

static void
save_rejected_line(FILE *f, int annot, int copied, struct line *line)
{
	fprintf(f, "%c%s",
	        copied ? (strchr("<>", annot) ? '!' : annot)
	               : (annot == '<' ? '-' : annot == '>' ? '+' : annot),
	        copied ? " " : "");
	fwriteline(f, *line);
	if (line->len && line->data[line->len - 1] != '\n')
		fprintf(f, "\n%s\n", NO_LF_MARK);
}

static void
save_rejected_hunk_copied(FILE *f, struct parsed_hunk *hunk, int with_patch_header)
{
	size_t i, j, a, b;
	int a_annots = 0, b_annots = 0;

	if (with_patch_header)
		fprintf(f, "*** /dev/null\n--- /dev/null\n");
	fprintf(f, "***************\n");

	if (hunk->old.len > 1)
		fprintf(f, "*** %zu,%zu ****\n", hunk->old.start, hunk->old.start + hunk->old.len - 1);
	else
		fprintf(f, "*** %zu ****\n", hunk->old.start);

	for (a = b = j = 0; a < hunk->old.len || b < hunk->new.len; j++) {
		switch (hunk->annot[j]) {
		case '<':
		case '-':
			a_annots = 1;
			a++;
			break;
		case '>':
		case '+':
			b_annots = 1;
			b++;
			break;
		default:
			a++;
			b++;
			break;
		}
	}

	for (i = j = 0; a_annots && i < hunk->old.len; i++) {
		for (; strchr(">+", hunk->annot[j]); j++);
		save_rejected_line(f, hunk->annot[j++], 1, hunk->old.lines + i);
	}

	if (hunk->new.len > 1)
		fprintf(f, "--- %zu,%zu ----\n", hunk->new.start, hunk->new.start + hunk->new.len - 1);
	else
		fprintf(f, "--- %zu ----\n", hunk->new.start);

	for (i = j = 0; b_annots && i < hunk->new.len; i++) {
		for (; strchr("<-", hunk->annot[j]); j++);
		save_rejected_line(f, hunk->annot[j++], 1, hunk->new.lines + i);
	}
}

static void
save_rejected_hunk_unified(FILE *f, struct parsed_hunk *hunk, int with_patch_header)
{
	size_t i = 0, a = 0, b = 0;
	char annot;
	struct line *line;

	if (with_patch_header)
		fprintf(f, "--- /dev/null\n+++ /dev/null\n");

	fprintf(f, "@@ -%zu", hunk->old.start);
	if (hunk->old.len != 1)
		fprintf(f, ",%zu", hunk->old.len);
	fprintf(f, " +%zu", hunk->new.start);
	if (hunk->new.len != 1)
		fprintf(f, ",%zu", hunk->new.len);
	fprintf(f, " @@\n");

	for (; a < hunk->old.len || b < hunk->new.len; i++) {
		annot = hunk->annot[i];
		if (strchr("<-", annot))
			line = hunk->old.lines + a++;
		else if (strchr(">+", annot))
			line = hunk->new.lines + b++;
		else
			line = hunk->old.lines + a++, b++;
		save_rejected_line(f, annot, 0, line);
	}
}

static void
save_rejected_hunk(struct parsed_hunk *hunk, const char *path, int with_patch_header)
{
	FILE *f;

	if (!(f = fopen(PATH(path), get_fopen_mode(path))))
		enprintf(FAILURE, "fopen %s:", path);

	(Uflag ? save_rejected_hunk_unified :
	         save_rejected_hunk_copied)(f, hunk, with_patch_header);

	enfshut(FAILURE, f, path);
}

static void
subline(struct line *dest, struct line *src, ssize_t off)
{
	dest->data = src->data + off;
	dest->len = src->len - off;
}

static void
parse_hunk_normal(struct hunk *hunk, struct parsed_hunk *parsed)
{
	struct hunk_content *old = &parsed->old, *new = &parsed->new;
	size_t i, j;
	char *p, action;

	old->start = strtoul(hunk->head->data, &p, 10);
	old->len = 0;
	p += strspn(p, ",0123456789");
	action = *p++;
	new->start = strtoul(p, &p, 10);
	new->len = 0;
	free(hunk->head->data);

	old->lines = enmalloc(FAILURE, hunk->nlines * sizeof(*old->lines));
	new->lines = enmalloc(FAILURE, hunk->nlines * sizeof(*new->lines));
	parsed->annot = enmalloc(FAILURE, hunk->nlines + 1);

	for (i = j = 0; i < hunk->nlines; i++) {
		if (hunk->lines[i].data[0] == '<') {
			subline(old->lines + old->len++, hunk->lines + i, 2);
			parsed->annot[j++] = action == 'c' ? '<' : '-';
		}
		else if (hunk->lines[i].data[0] == '>') {
			subline(new->lines + new->len++, hunk->lines + i, 2);
			parsed->annot[j++] = action == 'c' ? '>' : '+';
		}
	}
	parsed->annot[j] = 0;
}

static void
parse_hunk_copied(struct hunk *hunk, struct parsed_hunk *parsed)
{
	struct hunk_content *old = &parsed->old, *new = &parsed->new;
	size_t i = 0, a, b;
	char *p;

	free(hunk->head->data);

	old->lines = enmalloc(FAILURE, hunk->nlines * sizeof(*old->lines));
	new->lines = enmalloc(FAILURE, hunk->nlines * sizeof(*new->lines));
	parsed->annot = enmalloc(FAILURE, hunk->nlines + 1);

	p = hunk->lines[i++].data + 4;
	old->start = strtoul(p, &p, 10);
	old->len = 0;

	for (; hunk->lines[i].data[1] == ' '; i++)
		subline(old->lines + old->len++, hunk->lines + i, 2);

	p = hunk->lines[i++].data + 4;
	new->start = strtoul(p, &p, 10);
	new->len = 0;

	if (old->len) {
		for (; i < hunk->nlines; i++)
			subline(new->lines + new->len++, hunk->lines + i, 2);
	} else {
		for (; i < hunk->nlines; i++) {
			subline(new->lines + new->len++, hunk->lines + i, 2);
			if (hunk->lines[i].data[0] != '+')
				subline(old->lines + old->len++, hunk->lines + i, 2);
		}
	}

	if (!new->len)
		for (i = 0; i < old->len; i++)
			if (old->lines[i].data[-2] != '-')
				new->lines[new->len++] = old->lines[i];

#define OLDLINE  a < old->len && old->lines[a].data[-2]
#define NEWLINE  b < new->len && new->lines[b].data[-2]

	for (i = a = b = 0; a < old->len || b < new->len;) {
		if (OLDLINE == '-')  parsed->annot[i++] = '-', a++;
		if (NEWLINE == '+')  parsed->annot[i++] = '+', b++;
		while (OLDLINE == ' ' && NEWLINE == ' ')
			parsed->annot[i++] = ' ', a++, b++;
		while (OLDLINE == '!')  parsed->annot[i++] = '<', a++;
		while (NEWLINE == '!')  parsed->annot[i++] = '>', b++;
	}
	parsed->annot[i] = 0;
}

static void
parse_hunk_unified(struct hunk *hunk, struct parsed_hunk *parsed)
{
	struct hunk_content *old = &parsed->old, *new = &parsed->new;
	size_t i, j;
	char *p, *q;

	p = strchr(hunk->head->data, '-');
	old->start = strtoul(p + 1, &p, 10);
	old->len = *p == ',' ? strtoul(p + 1, &p, 10) : 1;
	p = strchr(p, '+');
	new->start = strtoul(p + 1, &p, 10);
	new->len = *p == ',' ? strtoul(p + 1, &p, 10) : 1;
	free(hunk->head->data);

	old->lines = enmalloc(FAILURE, old->len * sizeof(*old->lines));
	new->lines = enmalloc(FAILURE, new->len * sizeof(*new->lines));
	parsed->annot = enmalloc(FAILURE, hunk->nlines + 1);

	for (i = j = 0; i < hunk->nlines; i++)
		if (hunk->lines[i].data[0] != '+')
			subline(old->lines + j++, hunk->lines + i, 1);

	for (i = j = 0; i < hunk->nlines; i++)
		if (hunk->lines[i].data[0] != '-')
			subline(new->lines + j++, hunk->lines + i, 1);

	for (i = 0; i < hunk->nlines; i++)
		parsed->annot[i] = hunk->lines[i].data[0];
	parsed->annot[i] = 0;
	for (;;) {
		p = strstr(parsed->annot, "-+");
		q = strstr(parsed->annot, "+-");
		if (!p && !q)
			break;
		if (!p || (q && p > q))
			p = q;
		for (; p != parsed->annot && strchr("-+", p[-1]); p--);
		for (; *p == '-' || *p == '+'; p++)
			*p = "<>"[*p == '+'];
	}
}

static void
parse_hunk(struct hunk *hunk, enum format format, struct parsed_hunk *parsed)
{
	size_t i, n;

	for (i = 0; i < hunk->nlines; i++) {
		if (hunk->lines[i].data[0] != '\\') {
			hunk->lines[i].data[hunk->lines[i].len++] = '\n';
			continue;
		}
		hunk->nlines--;
		memmove(hunk->lines + i, hunk->lines + i + 1, (hunk->nlines - i) * sizeof(*hunk->lines));
		i--;
		hunk->lines[i].data[hunk->lines[i].len--] = '\0';
	}

	switch (format) {
	case NORMAL:   parse_hunk_normal(hunk, parsed);   break;
	case COPIED:   parse_hunk_copied(hunk, parsed);   break;
	case UNIFIED:  parse_hunk_unified(hunk, parsed);  break;
	default:       abort();
	}

	n = strlen(parsed->annot);
	parsed->rannot = enmalloc(FAILURE, n + 1);
	for (i = 0; i < n; i++) {
		switch (parsed->annot[i]) {
		case '-':  parsed->rannot[i] = '+';  break;
		case '+':  parsed->rannot[i] = '-';  break;
		case '<':  parsed->rannot[i] = '>';  break;
		case '>':  parsed->rannot[i] = '<';  break;
		default:   parsed->rannot[i] = ' ';  break;
		}
	}
	parsed->rannot[n] = 0;
}

static int
linecmpw(struct line *al, struct line *bl)
{
	const unsigned char *a = al->data, *b = bl->data;
	const unsigned char *ae = a + al->len, *be = b + bl->len;

	for (; a != ae && isspace(*a); a++);
	for (; b != be && isspace(*b); b++);

	while (a != ae && b != be) {
		if (*a != *b)
			return *a > *b ? 1 : -1;
		a++, b++;
		if ((a == ae || isspace(*a)) && (b == be || isspace(*b))) {
			for (; a != ae && isspace(*a); a++);
			for (; b != be && isspace(*b); b++);
		}
	}

	return a == ae ? b == be ? 0 : -1 : 1;
}

static int
does_hunk_match(struct file_data *file, size_t position, struct hunk_content *hunk)
{
	size_t pos, n = hunk->len;
	while (n)
		if (pos = position + --n, pos >= file->n ||
		    (lflag ? linecmpw : linecmp)(&file->d[pos].line, hunk->lines + n))
			return 0;
	return 1;
}

static void
linedup(struct line *restrict dest, const struct line *restrict src)
{
	dest->data = enmalloc(FAILURE, src->len + 1);
	linecpy2mem(dest->data, *src);
	dest->len = src->len;
}

static void
apply_contiguous_edit(struct file_data *f, size_t ln, size_t rm, size_t ad, struct line *newlines, const char *annot)
{
#define LN  (f->d[ln])

	size_t rm_end, ad_end, n, a, b, start, extra, i, j, k;
	struct line_data *orig;

	rm_end = ln + rm;
	ad_end = ln + ad;
	n = f->n;

	orig = enmemdup(FAILURE, f->d + ln, (rm + 1) * sizeof(*f->d));
	memmove(f->d + ln, f->d + rm_end, (n - rm_end + 1) * sizeof(*f->d));

	f->n -= rm;
	if (f->n == 1 && !*(f->d->line.data))
		f->n--, n--;
	f->n += ad;

	f->d = enrealloc(FAILURE, f->d, (f->n + 1) * sizeof(*f->d));
	memmove(f->d + ad_end, f->d + ln, (n - rm_end + 1) * sizeof(*f->d));
	memset(f->d + ln, 0, ad * sizeof(*f->d));

	for (i = a = b = 0; a < rm || b < ad; ln++) {
		for (start = i, extra = 0; a < rm && strchr("<-", annot[i]); a++, i++)
			extra += orig[a].nold;
		if (start < i) {
			n = i - start + extra;
			a -= i - start;
			LN.old = enrealloc(FAILURE, LN.old, (LN.nold + n) * sizeof(*f->d->old));
			memmove(LN.old + n, LN.old, LN.nold * sizeof(*f->d->old));
			for (j = extra = 0; j < n - extra; a++) {
				for (k = 0; k < orig[a].nold; k++)
					linedup(LN.old + j++, orig[a].old + k);
				if (orig[a].orig)
					linedup(LN.old + j++, &orig[a].line);
				else
					extra++;
			}
			memmove(LN.old + n - extra, LN.old + n, LN.nold * sizeof(*f->d->old));
			LN.nold += n - extra;
		}
		if (b == ad)
			continue;
		if (annot[i++] == ' ') {
			LN.line = orig[a].line;
			LN.orig = orig[a].orig;
			LN.new = orig[a].new;
			a++, b++;
		} else {
			LN.new = 1;
			linedup(&LN.line, newlines + b++);
		}
	}

	free(orig);
}

static void
do_apply_hunk(struct file_data *f, struct parsed_hunk *h, size_t ln)
{
	size_t i, ad, rm, start, adoff = 0;
	for (i = 0; h->annot[i];) {
		start = i;
		for (ad = rm = 0; h->annot[i] && h->annot[i] != ' '; i++) {
			ad += !!strchr(">+", h->annot[i]);
			rm += !!strchr("<-", h->annot[i]);
		}
		if (i == start) {
			adoff++;
			i++;
		} else {
			apply_contiguous_edit(f, ln + adoff, rm, ad, h->new.lines + adoff, h->annot + start);
			adoff += ad;
		}
	}
}

static enum applicability
apply_hunk_try(struct file_data *file, struct parsed_hunk *fhunk, ssize_t offset,
               int reverse, int dryrun, ssize_t *fuzz)
{
	struct parsed_hunk hunk;
	ssize_t pos, dist = 0;
	int finbounds, binbounds;

	hunk = *fhunk;
	if (reverse)
		reverse_hunk(&hunk);

	hunk.old.start -= !!hunk.old.len;
	hunk.new.start -= !!hunk.new.len;

	pos = (ssize_t)(hunk.old.start) + offset;
	if (pos < 0)
		pos = 0;
	if ((size_t)pos > file->n)
		pos = file->n;
	for (dist = 0;; dist++) {
		finbounds = pos + dist < file->n;
		binbounds = pos - dist >= 0;
		if (finbounds && does_hunk_match(file, pos + dist, &hunk.old)) {
			pos += *fuzz = +dist;
			goto found;
		} else if (binbounds && does_hunk_match(file, pos - dist, &hunk.old)) {
			pos += *fuzz = -dist;
			goto found;
		} else if (!finbounds && !binbounds) {
			break;
		}
	}

	pos = (ssize_t)(hunk.new.start) + offset;
	for (dist = 0;; dist++) {
		finbounds = pos + dist < file->n;
		binbounds = pos - dist >= 0;
		if ((finbounds && does_hunk_match(file, pos + (*fuzz = +dist), &hunk.new)) ||
		    (binbounds && does_hunk_match(file, pos + (*fuzz = -dist), &hunk.new)))
			return APPLIED;
		else if (!finbounds && !binbounds)
			break;
	}

	return INAPPLICABLE;

found:
	if (!dryrun)
		do_apply_hunk(file, &hunk, pos);
	return APPLICABLE;
}

static enum applicability
apply_hunk(struct file_data *file, struct parsed_hunk *hunk, ssize_t offset, ssize_t *fuzz, int forward)
{
	static int know_direction = 0;
	int rev = forward ? 0 : Rflag;
	enum applicability fstatus, rstatus;

	if (forward || know_direction) {
		if ((fstatus = rstatus = apply_hunk_try(file, hunk, offset, rev, 0, fuzz)) != APPLICABLE)
			goto rejected;
	} else if ((fstatus = apply_hunk_try(file, hunk, offset, Rflag, 0, fuzz)) == APPLICABLE) {
		know_direction = 1;
	} else if ((rstatus = apply_hunk_try(file, hunk, offset, !Rflag, 1, fuzz)) == APPLICABLE) {
		know_direction = 1;
		if (!ask_for_reverse())
			goto rejected;
		apply_hunk_try(file, hunk, offset, !Rflag, 0, fuzz);
		Rflag ^= 1;
	} else {
		goto rejected;
	}

	if (Rflag)
		reverse_hunk(hunk);
	return APPLICABLE;

rejected:
	if (Rflag)
		reverse_hunk(hunk);
	return (Nflag && fstatus == APPLIED && rstatus == fstatus) ? APPLIED : INAPPLICABLE;
}

static int
parse_ed_script(struct patch *patch, struct file_data *file)
{
	/* Not relying on ed(1p) lets us implemented -D for
	 * ed-scripts too without diff(1p):ing afterwards.
	 * This is not significantly more complex than
	 * relying on ed(1p) anyways. */

	struct hunk temp, *hunk, *ed = patch->hunks;
	struct line *line;
	size_t i, j, start, last, count, added, add;
	ssize_t offset = 0;
	int have_last;
	char *p, *q;

	patch->format = UNIFIED;
	patch->nhunks = 0;
	patch->hunks = 0;

	for (i = 0; i < ed->nlines;) {
		patch->nhunks++;
		patch->hunks = enrealloc(FAILURE, patch->hunks, patch->nhunks * sizeof(*patch->hunks));
		hunk = patch->hunks + patch->nhunks - 1;
		p = parse_range(ed->lines[i++].data, &start, &last, &have_last);
		if (!have_last)
			last = start;
		if (last < start)
			return -1;
		count = last - start + 1;
		memset(hunk, 0, sizeof(*hunk));
		added = 0;

		switch (*p) {
		case 'c':
		case 'd':
			if (start + count > file->n + 1)
				return -1;
			hunk->lines = enmalloc(FAILURE, count * sizeof(*hunk->lines));
			hunk->nlines = count;
			for (j = 0; j < count; j++) {
				line = hunk->lines + j;
				line->len = 1 + file->d[start + j - 1].line.len;
				line->data = enmalloc(FAILURE, line->len + 2);
				line->data[0] = '-';
				line->data[line->len + 1] = '\0';
				linecpy2mem(line->data + 1, file->d[start + j - 1].line);
				if (line->data[line->len - 1] == '\n')
					line->data[--(line->len)] = '\0';
			}
			if (*p == 'c')
				goto list_addition;
			break;
		case 'a':
		case 'i':
		  	count = 0;
		list_addition:
			for (add = 0; !lineeq(ed->lines[i + add], "."); add++);
			hunk->lines = enrealloc(FAILURE, hunk->lines,
			                        (hunk->nlines + add) * sizeof(*hunk->lines));
			for (j = 0; j < add; j++) {
				line = hunk->lines + hunk->nlines + j;
				line->len = 1 + ed->lines[i + j].len;
				line->data = enmalloc(FAILURE, line->len + 2);
				line->data[0] = '+';
				line->data[line->len + 1] = '\0';
				linecpy2mem(line->data + 1, ed->lines[i + j]);
			}
			i += add + 1;
			hunk->nlines += add;
			added += add;
			if (i < ed->nlines && ed->lines[i].data[0] == 's') {
				line = hunk->lines + hunk->nlines - 1;
				memmove(line->data + 1, line->data + 2, line->len-- - 1);
				i++;
			}
			if (i < ed->nlines && lineeq(ed->lines[i], "a")) {
				i++;
				goto list_addition;
			}
			if (*p == 'i' && start)
				start--;
			break;
		default:
			abort();
		}

		hunk->head = enmalloc(FAILURE, sizeof(*hunk->head));
		hunk->head->len = enasprintf(FAILURE, &hunk->head->data, "@@ -%zu,%zu +%zu,%zu @@",
		                             start, count, SIZE_MAX, added);
	}

	for (i = 0; i < ed->nlines; i++)
		free(ed->lines[i].data);
	free(ed);

	for (i = 0, j = patch->nhunks - 1; i < j; i++, j--) {
		temp = patch->hunks[i];
		patch->hunks[i] = patch->hunks[j];
		patch->hunks[j] = temp;
	}

	for (i = 0; i < patch->nhunks; i++) {
		hunk = patch->hunks + i;
		start = strtoul(hunk->head->data + 4, &p, 10);
		count = strtoul(p + 1, &p, 10);
		p = strchr(q = p + 2, ',') + 1;
		added = strtoul(p, 0, 10);
		q += sprintf(q, "%zu", start + offset - !added + !count);
		*q++ = ',';
		memmove(q, p, strlen(p) + 1);
		offset -= count;
		offset += added;
		hunk->head->len = strlen(hunk->head->data);
	}

	return 0;
}

static const char *
fuzzstr(ssize_t fuzz)
{
	static char buf[sizeof(" with downward fuzz ") + 3 * sizeof(unsigned long long int)];
	if (!fuzz)
		return "";
	sprintf(buf, " with %sward fuzz %llu",
	        fuzz < 0 ? "up" : "down", (unsigned long long int)llabs(fuzz));
	return buf;
}

static void
apply_patch(struct patch *patch, size_t patch_index)
{
	struct file_data *file;
	struct parsed_hunk hunk;
	char *rejfile, *origfile;
	const char *path = outfile ? outfile : patch->path;
	int firstrej = 1, edscript = patch->format == ED;
	ssize_t offset = 0, fuzz;
	size_t i;
	FILE *f = 0;

	if (!rejectfile)
		enasprintf(FAILURE, &rejfile, "%s.rej", path);
	else
		rejfile = rejectfile;

	if (bflag && file_exists(path) && is_first_patch(path)) {
		enasprintf(FAILURE, &origfile, "%s.orig", path);
		if (!(f = fopen(PATH(origfile), "w")))
			enprintf(FAILURE, "fopen %s:", origfile);
	}

	file = get_last_patch(patch->path);

	if (edscript && parse_ed_script(patch, file))
		enprintf(FAILURE, "ed-script for %s corrupt or out of date, aborting.\n", patch->path);

	if (f) {
		save_file(f, file);
		enfshut(FAILURE, f, origfile);
		free(origfile);
	}

	for (i = 0; i < patch->nhunks; i++) {
		parse_hunk(patch->hunks + i, patch->format, &hunk);
		switch (apply_hunk(file, &hunk, offset, &fuzz, edscript)) {
		case APPLIED:
			weprintf("hunk number %zu of %zu for %s has already been applied%s, skipping.\n",
			         i + 1, patch->nhunks, patch->path, fuzzstr(fuzz));
			break;
		case INAPPLICABLE:
			save_rejected_hunk(&hunk, rejfile, firstrej);
			weprintf("hunk number %zu of %zu for %s has been rejected and saved to %s.\n",
			         i + 1, patch->nhunks, patch->path, rejfile);
			firstrej = 0;
			rejected = 1;
			break;
		default:
			if (fuzz)
				weprintf("hunk number %zu of %zu for %s succeeded%s.\n",
				         i + 1, patch->nhunks, patch->path, fuzzstr(fuzz));
			offset += hunk.new.len;
			offset -= hunk.old.len;
			break;
		}
		while (patch->hunks[i].nlines--)
			free(patch->hunks[i].lines[patch->hunks[i].nlines].data);
		free(hunk.new.lines);
		free(hunk.old.lines);
		free(hunk.annot);
		free(hunk.rannot);
	}
	free(patch->hunks);
	if (!rejectfile)
		free(rejfile);

	if (!(f = fopen(PATH(path), get_fopen_mode(outfile))))
		enprintf(FAILURE, "fopen %s:", path);
	(ifdef ? save_file_cpp : save_file)(f, file);
	enfshut(FAILURE, f, path);

	if (!outfile && !ifdef) {
		for (i = 0; i < file->n; i++)
			free(file->d[i].line.data);
		free(file->d);
		free(file);
	}
}

static void
apply_patchset(struct patchset *ps)
{
	size_t i = 0;
	for (i = 0; i < ps->npatches; i++)
		apply_patch(ps->patches + i, i);
	free(ps->patches);
}

static struct line *
get_lines(struct file_data *file)
{
	size_t n = file->n + 1;
	struct line *lines = enmalloc(FAILURE, n * sizeof(*lines));
	while (n--)
		lines[n] = file->d[n].line;
	return lines;
}

int
main(int argc, char *argv[])
{
	struct patchset patchset;
	struct file_data patchfile_data;
	char *p, *Dflag;

	stdin_dash[0] = stdout_dash[0] = '-';
	stdin_dash[1] = stdout_dash[1] = '\0';

	ARGBEGIN {
	case 'b':
		bflag = 1;
		break;
	case 'c':
		specified_format = COPIED;
		break;
	case 'd':
		dflag = EARGF(usage());
		break;
	case 'D':
		if (Dflag)
			usage();
		Dflag = EARGF(usage());
		break;
	case 'e':
		specified_format = ED;
		break;
	case 'f':
		fflag = 1;
		break;
	case 'i':
		if (patchfile)
			usage();
		patchfile = EARGF(usage());
		if (!strcmp(patchfile, "-"))
			patchfile = stdin_dash;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'n':
		specified_format = NORMAL;
		break;
	case 'N':
		Nflag = 1;
		break;
	case 'o':
		if (outfile)
			usage();
		outfile = EARGF(usage());
		if (!strcmp(outfile, "-"))
			outfile = stdout_dash;
		break;
	case 'p':
		errno = 0;
		pflag = strtoul(EARGF(usage()), &p, 10);
		if (errno || *p)
			usage();
		break;
	case 'r':
		if (rejectfile)
			usage();
		rejectfile = EARGF(usage());
		if (!strcmp(rejectfile, "-"))
			rejectfile = stdout_dash;
		break;
	case 'R':
		Rflag = 1;
		break;
	case 'u':
		specified_format = UNIFIED;
		break;
	case 'U':
		Uflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc > 1)
		usage();
	if (argc > 0) {
		apply_patches_to = *argv;
		if (!strcmp(apply_patches_to, "-"))
			apply_patches_to = stdin_dash;
	}
	if (!rejectfile && outfile && !strcmp(outfile, "/dev/null"))
		rejectfile = "/dev/null";

	if (Dflag) {
		p = Dflag + (*Dflag == '!');
		if (!strcmp(p, "0") || !strcmp(p, "1")) {
			enasprintf(FAILURE, &ifdef, "#if %s", p);
			enasprintf(FAILURE, &ifndef, "#if %i", 1 - (*p - '0'));
		} else {
			enasprintf(FAILURE, &ifdef, "#ifdef %s", p);
			enasprintf(FAILURE, &ifndef, "#ifndef %s", p);
		}
		if (*Dflag == '!')
			p = ifdef, ifdef = ifndef, ifndef = p;
	}

	load_lines(patchfile, &patchfile_data, 1, 0);
	parse_patchfile(&patchset, get_lines(&patchfile_data));
	ask_for_filename(&patchset);
	apply_patchset(&patchset);

	return rejected ? REJECTED : 0;
}
