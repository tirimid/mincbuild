#include "prune.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <regex.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PRUNE_SINGLE_THREAD
#include <pthread.h>
#include <sys/sysinfo.h>
#endif

// allow GNU as `.include` statements to also easily be supported if needed.
#ifdef PRUNE_SUPPORT_AS
#define INCLUDE_REGEX "[#\\.]\\s*include\\s*[<\"].+[>\"]"
#else
#define INCLUDE_REGEX "#\\s*include\\s*[<\"].+[>\"]"
#endif

struct thread_arg {
	size_t start, cnt;
	struct conf const *conf;
	struct str_list *srcs, *objs;
	struct str_list const *hdrs;
	regex_t const *re;
};

struct rebuild_ck_info {
	struct str_list const *hdrs;
	struct conf const *conf;
	regex_t const *re;
	time_t mt;
};

static void *worker(void *vp_arg);
static bool ck_rebuild(char const *path, struct str_list *ckd_incs, struct rebuild_ck_info const *info);

void
prune(struct conf const *conf, struct str_list *srcs, struct str_list *objs,
      struct str_list const *hdrs)
{
	regex_t re;
	if (regcomp(&re, INCLUDE_REGEX, REG_EXTENDED | REG_NEWLINE)) {
		fputs("failed to compile regex: '" INCLUDE_REGEX "'!\n", stderr);
		exit(1);
	}
	
#ifndef PRUNE_SINGLE_THREAD
	// multithreaded pthread dependent code.
	
	ssize_t cnt = get_nprocs();
	if (cnt < 1) {
		fputs("no CPU threads available for pruning!\n", stderr);
		exit(1);
	}
	cnt = srcs->size < cnt ? srcs->size : cnt;
	
	printf("pruning compilation with %zu worker(s)\n", cnt);
	
	struct thread_arg *th_args = malloc(sizeof(struct thread_arg) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		th_args[i] = (struct thread_arg){
			.conf = conf,
			.srcs = srcs,
			.objs = objs,
			.hdrs = hdrs,
			.re = &re,
		};
	}

	for (size_t i = 0; i < srcs->size; ++i)
		++th_args[i % cnt].cnt;

	// create threads to mark sources / objects for removal in pruning.
	pthread_t *ths = malloc(sizeof(pthread_t) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		struct thread_arg const *prev = i == 0 ? NULL : &th_args[i - 1];
		th_args[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		if (pthread_create(&ths[i], NULL, worker, &th_args[i])) {
			fputs("failed to create worker thread for pruning!\n", stderr);
			exit(1);
		}
	}

	for (size_t i = 0; i < cnt; ++i)
		pthread_join(ths[i], NULL);

	free(ths);
	free(th_args);
#else
	// singlethreaded pthread independent code.
	
	puts("pruning compilation in single thread mode");
	
	struct thread_arg th_arg = {
		.conf = conf,
		.srcs = srcs,
		.objs = objs,
		.hdrs = hdrs,
		.re = &re,
		.start = 0,
		.cnt = srcs->size,
	};
	
	worker(&th_arg);
#endif
	
	regfree(&re);

	// remove marked sources / objects.
	for (size_t i = 0; i < srcs->size; ++i) {
		if (!srcs->data[i]) {
			str_list_rm_no_free(srcs, i);
			str_list_rm_no_free(objs, i);
			--i;
		}
	}
}

static void *
worker(void *vp_arg)
{
	struct thread_arg *arg = vp_arg;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		struct stat s_src;
		stat(arg->srcs->data[i], &s_src);

		struct stat s_obj;
		if (stat(arg->objs->data[i], &s_obj))
			continue;

		time_t mt = s_obj.st_mtime;
		struct str_list ckd_incs = str_list_create();
		
		struct rebuild_ck_info info = {
			.hdrs = arg->hdrs,
			.conf = arg->conf,
			.re = arg->re,
			.mt = mt,
		};
		
		if (ck_rebuild(arg->srcs->data[i], &ckd_incs, &info))
			goto cleanup;

		printf("\t%s\n", arg->srcs->data[i]);

		// mark source/object for removal in pruning.
		free(arg->srcs->data[i]);
		free(arg->objs->data[i]);
		arg->srcs->data[i] = arg->objs->data[i] = NULL;

	cleanup:
		str_list_destroy(&ckd_incs);
	}

	return NULL;
}

static bool
ck_rebuild(char const *path, struct str_list *ckd_incs,
           struct rebuild_ck_info const *info)
{
	// check whether anything even needs to be done.
	struct stat s;
	stat(path, &s);
	bool rebuild = difftime(s.st_mtime, info->mt) > 0.0;
	if (rebuild)
		return true;
	
	// find included headers in the file.
	struct str_list incs = str_list_create();
	
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
		str_list_add(&incs, inc_path);
		free(inc_path);
		
		start += match.rm_eo;
	}

	free(fconts);

	// remove non-project includes from `incs`, and also includes for
	// headers which have already been checked, preventing excess resource
	// usage and hanging with coupled inclusions.
	for (size_t i = 0; i < incs.size; ++i) {
		if (!str_list_contains(info->hdrs, incs.data[i])
		    || str_list_contains(ckd_incs, incs.data[i])) {
			str_list_rm(&incs, i);
			--i;
		}
	}
	
	for (size_t i = 0; i < incs.size && !rebuild; ++i) {
		str_list_add(ckd_incs, incs.data[i]);
		if (ck_rebuild(incs.data[i], ckd_incs, info)) {
			str_list_destroy(&incs);
			return true;
		}
	}

	str_list_destroy(&incs);
	return false;
}
