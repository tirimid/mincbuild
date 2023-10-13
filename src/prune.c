#include "prune.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#define INCLUDE_REGEX "#\\s*include\\s*[<\"].+[>\"]"

struct tharg {
	size_t start, cnt;
	struct conf const *conf;
	struct strlist *srcs, *objs;
	struct strlist const *hdrs;
	regex_t const *re;
};

struct rebuildck_info {
	struct strlist const *hdrs;
	struct conf const *conf;
	regex_t const *re;
	time_t mt;
};

static void *worker(void *vp_arg);
static bool ckrebuild(char const *path, struct strlist *ckdincs, struct rebuildck_info const *info);

void
prune(struct conf const *conf, struct strlist *srcs, struct strlist *objs, struct strlist const *hdrs)
{
	regex_t re;
	if (regcomp(&re, INCLUDE_REGEX, REG_EXTENDED | REG_NEWLINE)) {
		fputs("failed to compile regex: '" INCLUDE_REGEX "'!\n", stderr);
		exit(1);
	}

	ssize_t cnt = get_nprocs();
	if (cnt < 1) {
		fputs("no CPU threads available for pruning!\n", stderr);
		exit(1);
	}
	cnt = srcs->size < cnt ? srcs->size : cnt;

	printf("pruning compilation with %zu worker(s)\n", cnt);
	
	struct tharg *thargs = malloc(sizeof(struct tharg) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		thargs[i] = (struct tharg){
			.conf = conf,
			.srcs = srcs,
			.objs = objs,
			.hdrs = hdrs,
			.re = &re,
		};
	}

	for (size_t i = 0; i < srcs->size; ++i)
		++thargs[i % cnt].cnt;

	// create threads to mark sources/objects for removal in pruning.
	pthread_t *ths = malloc(sizeof(pthread_t) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		struct tharg const *prev = i == 0 ? NULL : &thargs[i - 1];
		thargs[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		if (pthread_create(&ths[i], NULL, worker, &thargs[i])) {
			fputs("failed to create worker thread for pruning!\n", stderr);
			exit(1);
		}
	}

	for (size_t i = 0; i < cnt; ++i)
		pthread_join(ths[i], NULL);

	free(ths);
	free(thargs);
	regfree(&re);

	// remove marked sources/objects.
	for (size_t i = 0; i < srcs->size; ++i) {
		if (!srcs->data[i]) {
			strlist_rm_no_free(srcs, i);
			strlist_rm_no_free(objs, i);
			--i;
		}
	}
}

static void *
worker(void *vp_arg)
{
	struct tharg *arg = vp_arg;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		struct stat s_src;
		stat(arg->srcs->data[i], &s_src);

		struct stat s_obj;
		if (stat(arg->objs->data[i], &s_obj))
			continue;

		time_t mt = s_obj.st_mtime;
		struct strlist ckdincs = strlist_create();
		
		struct rebuildck_info info = {
			.hdrs = arg->hdrs,
			.conf = arg->conf,
			.re = arg->re,
			.mt = mt,
		};
		
		if (ckrebuild(arg->srcs->data[i], &ckdincs, &info))
			goto cleanup;

		printf("\t%s\n", arg->srcs->data[i]);

		// mark source/object for removal in pruning.
		free(arg->srcs->data[i]);
		free(arg->objs->data[i]);
		arg->srcs->data[i] = arg->objs->data[i] = NULL;

	cleanup:
		strlist_destroy(&ckdincs);
	}

	return NULL;
}

static bool
ckrebuild(char const *path, struct strlist *ckdincs, struct rebuildck_info const *info)
{
	// check whether anything even needs to be done.
	struct stat s;
	stat(path, &s);
	bool rebuild = difftime(s.st_mtime, info->mt) > 0.0;
	if (rebuild)
		return true;
	
	// find included headers in the file.
	struct strlist incs = strlist_create();
	
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "cannot open file for inclusion checks: '%s'!\n", path);
		exit(1);
	}

	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *fconts = malloc(fsize + 1);
	fread(fconts, 1, fsize, fp);
	fconts[fsize] = 0;

	fclose(fp);

	regoff_t start = 0;
	regmatch_t match;
	while (!regexec(info->re, fconts + start, 1, &match, 0)) {
		fconts[start + match.rm_eo - 1] = 0;

		char *inc = fconts + start + match.rm_so;
		while (!strchr("<\"", *inc))
			++inc;
		++inc;

		char *inc_path = malloc(strlen(info->conf->inc_dir) + strlen(inc) + 2);
		sprintf(inc_path, "%s/%s", info->conf->inc_dir, inc);
		strlist_add(&incs, inc_path);
		free(inc_path);
		
		start += match.rm_eo;
	}

	free(fconts);

	// remove non-project includes from `incs`, and also includes for
	// headers which have already been checked, preventing excess resource
	// usage and hanging with coupled inclusions.
	for (size_t i = 0; i < incs.size; ++i) {
		if (!strlist_contains(info->hdrs, incs.data[i])
		    || strlist_contains(ckdincs, incs.data[i])) {
			strlist_rm(&incs, i);
			--i;
		}
	}
	
	for (size_t i = 0; i < incs.size && !rebuild; ++i) {
		strlist_add(ckdincs, incs.data[i]);
		if (ckrebuild(incs.data[i], ckdincs, info)) {
			strlist_destroy(&incs);
			return true;
		}
	}

	strlist_destroy(&incs);
	return false;
}
