#include "compile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <sys/sysinfo.h>
#include <unistd.h>

struct tharg {
	size_t start, cnt;
	struct conf const *conf;
	struct strlist const *srcs, *objs;
	size_t *out_progress;
	struct fmt_spec const *spec;
};

struct fmtdata {
	struct conf const *conf;
	char const *src, *obj;
};

extern bool flag_v;

static void *worker(void *vp_arg);
static void fmt_command(struct string *out_cmd, void *vp_data);
static void fmt_cflags(struct string *out_cmd, void *vp_data);
static void fmt_source(struct string *out_cmd, void *vp_data);
static void fmt_object(struct string *out_cmd, void *vp_data);
static void inc_fmt_include(struct string *out_cmd, void *vp_data);
static void fmt_includes(struct string *out_cmd, void *vp_data);

void
compile(struct conf const *conf, struct strlist const *srcs, struct strlist const *objs)
{
	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'c', fmt_command);
	fmt_spec_add_ent(&spec, 'f', fmt_cflags);
	fmt_spec_add_ent(&spec, 's', fmt_source);
	fmt_spec_add_ent(&spec, 'o', fmt_object);
	fmt_spec_add_ent(&spec, 'i', fmt_includes);

	ssize_t cnt = get_nprocs();
	if (cnt < 1) {
		fputs("no CPU threads available for compilation!\n", stderr);
		exit(1);
	}
	cnt = srcs->size < cnt ? srcs->size : cnt;

	printf("compiling project with %zu worker(s)\n", cnt);
	size_t progress = 0;
	
	struct tharg *thargs = malloc(sizeof(struct tharg) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		thargs[i] = (struct tharg){
			.conf = conf,
			.srcs = srcs,
			.objs = objs,
			.out_progress = &progress,
			.spec = &spec,
		};
	}

	for (size_t i = 0; i < srcs->size; ++i)
		++thargs[i % cnt].cnt;

	pthread_t *ths = malloc(sizeof(pthread_t) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		struct tharg const *prev = i == 0 ? NULL : &thargs[i - 1];
		thargs[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		if (pthread_create(&ths[i], NULL, worker, &thargs[i])) {
			fputs("failed to create worker thread for compilation!\n", stderr);
			exit(1);
		}
	}

	for (size_t i = 0; i < cnt; ++i)
		pthread_join(ths[i], NULL);

	free(ths);
	free(thargs);
	fmt_spec_destroy(&spec);
}

static void *
worker(void *vp_arg)
{
	struct tharg *arg = vp_arg;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		char const *src = arg->srcs->data[i], *obj = arg->objs->data[i];
		
		struct fmtdata data = {
			.conf = arg->conf,
			.src = src,
			.obj = obj,
		};
		
		mkdir_recursive(obj);
		rmdir(obj);

		char *cmd = fmt_str(arg->spec, arg->conf->cc_cmd_fmt, &data);

		if (flag_v) {
			printf("(%zu/%zu)\t%s\t<- %s\n", ++*arg->out_progress,
			       arg->srcs->size, obj, cmd);
		} else {
			printf("(%zu/%zu)\t%s\n", ++*arg->out_progress,
			       arg->srcs->size, obj);
		}
		
		int rc = system(cmd);
		free(cmd);
		
		if (rc != arg->conf->cc_success_rc) {
			fprintf(stderr, "compilation failed on file: '%s'!\n", src);
			exit(1);
		}
	}

	return NULL;
}

static void
fmt_command(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *cc = sanitize_path(data->conf->cc);
	string_push_str(out_cmd, cc);
	free(cc);
}

static void
fmt_cflags(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	string_push_str(out_cmd, data->conf->cflags);
}

static void
fmt_source(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *src = sanitize_path(data->src);
	string_push_str(out_cmd, src);
	free(src);
}

static void
fmt_object(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *obj = sanitize_path(data->obj);
	string_push_str(out_cmd, obj);
	free(obj);
}

static void
inc_fmt_include(struct string *out_cmd, void *vp_data)
{
	char *inc = sanitize_path(vp_data);
	string_push_str(out_cmd, inc);
	free(inc);
}

static void
fmt_includes(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	
	struct strlist incs = strlist_copy(&data->conf->incs);
	strlist_add(&incs, data->conf->inc_dir);

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'i', inc_fmt_include);
	
	for (size_t i = 0; i < incs.size; ++i) {
		fmt_inplace(out_cmd, &spec, data->conf->cc_inc_fmt, incs.data[i]);
		if (i < incs.size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
	strlist_destroy(&incs);
}
