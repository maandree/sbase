/* See LICENSE file for copyright and license details. */
#include <sys/stat.h>
#include <sys/types.h>

#include "queue.h"

struct history {
	struct history *prev;
	dev_t dev;
	ino_t ino;
};

struct pendingrecurse {
	char *path;
	void *data;
	int depth;
	TAILQ_ENTRY(pendingrecurse) entry;
};

struct recursor {
	void (*fn)(const char *, struct stat *, void *, struct recursor *);
	struct history *hist;
	int depth;
	int maxdepth;
	int follow;
	int flags;
	TAILQ_HEAD(pending, pendingrecurse) pending;
};

enum {
	SAMEDEV  = 1 << 0,
	DIRFIRST = 1 << 1,
	SILENT   = 1 << 2,
	BFS      = 1 << 3,
};

extern int cp_aflag;
extern int cp_fflag;
extern int cp_pflag;
extern int cp_rflag;
extern int cp_vflag;
extern int cp_follow;
extern int cp_status;

extern int rm_fflag;
extern int rm_rflag;
extern int rm_status;

extern int recurse_status;

void recurse(const char *, void *, struct recursor *);
void recurselater(const char *, void *, struct recursor *);
void recursenow(struct recursor *);

int cp(const char *, const char *, int);
void rm(const char *, struct stat *, void *, struct recursor *);
