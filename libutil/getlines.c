/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../text.h"
#include "../util.h"

void
ngetlines(int status, FILE *fp, struct linebuf *b)
{
	char *line = NULL;
	size_t size = 0, linelen = 0;
	ssize_t len;

	while ((len = getline(&line, &size, fp)) > 0) {
		if (++b->nlines > b->capacity) {
			b->capacity += 512;
			b->lines = enrealloc(status, b->lines, b->capacity * sizeof(*b->lines));
		}
		linelen = len;
		b->lines[b->nlines - 1].data = memcpy(enmalloc(status, linelen + 1), line, linelen + 1);
		b->lines[b->nlines - 1].len = linelen;
	}
	free(line);
	b->nolf = b->lines && b->nlines && linelen && b->lines[b->nlines - 1].data[linelen - 1] != '\n';
	if (b->nolf) {
		b->lines[b->nlines - 1].data = enrealloc(status, b->lines[b->nlines - 1].data, linelen + 2);
		b->lines[b->nlines - 1].data[linelen] = '\n';
		b->lines[b->nlines - 1].data[linelen + 1] = '\0';
		b->lines[b->nlines - 1].len++;
		b->nolf = 1;
	}
}

void
getlines(FILE *fp, struct linebuf *b)
{
	ngetlines(1, fp, b);
}
