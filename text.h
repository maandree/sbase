/* See LICENSE file for copyright and license details. */

struct line {
	char *data;
	size_t len;
};

struct linebuf {
	struct line *lines;
	size_t nlines;
	size_t capacity;
	int nolf;
};
#define EMPTY_LINEBUF {NULL, 0, 0, 0}
void getlines(FILE *, struct linebuf *);
void ngetlines(int, FILE *, struct linebuf *);

void concat(FILE *, const char *, FILE *, const char *);
int linecmp(struct line *, struct line *);
