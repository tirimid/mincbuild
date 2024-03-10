#include "compile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>

#ifndef COMPILE_SINGLE_THREAD
#include <pthread.h>
#include <sys/sysinfo.h>
#endif

struct thread_arg {
	size_t start, cnt;
	struct conf const *conf;
	struct str_list const *srcs, *objs;
	size_t *out_progress;
	struct fmt_spec const *spec;
};

struct fmt_data {
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
compile(struct conf const *conf, struct str_list const *srcs,
        struct str_list const *objs)
{
	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'c', fmt_command);
	fmt_spec_add_ent(&spec, 'f', fmt_cflags);
	fmt_spec_add_ent(&spec, 's', fmt_source);
	fmt_spec_add_ent(&spec, 'o', fmt_object);
	fmt_spec_add_ent(&spec, 'i', fmt_includes);
	
#ifndef COMPILE_SINGLE_THREAD
	// multithreaded pthread dependent code.
	
	ssize_t cnt = get_nprocs();
	if (cnt < 1) {
		fputs("no CPU threads available for compilation!\n", stderr);
		exit(1);
	}
	cnt = srcs->size < cnt ? srcs->size : cnt;

	printf("compiling project with %zu worker(s)\n", cnt);
	size_t progress = 0;
	
	struct thread_arg *th_args = malloc(sizeof(struct thread_arg) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		th_args[i] = (struct thread_arg){
			.conf = conf,
			.srcs = srcs,
			.objs = objs,
			.out_progress = &progress,
			.spec = &spec,
		};
	}

	for (size_t i = 0; i < srcs->size; ++i)
		++th_args[i % cnt].cnt;

	pthread_t *ths = malloc(sizeof(pthread_t) * cnt);
	for (size_t i = 0; i < cnt; ++i) {
		struct thread_arg const *prev = i == 0 ? NULL : &th_args[i - 1];
		th_args[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		if (pthread_create(&ths[i], NULL, worker, &th_args[i])) {
			fputs("failed to create worker thread for compilation!\n", stderr);
			exit(1);
		}
	}

	for (size_t i = 0; i < cnt; ++i)
		pthread_join(ths[i], NULL);

	free(ths);
	free(th_args);
#else
	// singlethreaded pthread independent code.
	
	puts("compiling project in single thread mode");
	size_t progress = 0;
	
	struct thread_arg th_arg = {
		.conf = conf,
		.srcs = srcs,
		.objs = objs,
		.out_progress = &progress,
		.spec = &spec,
		.start = 0,
		.cnt = srcs->size,
	};
	
	worker(&th_arg);
#endif
	
	fmt_spec_destroy(&spec);
}

static void *
worker(void *vp_arg)
{
#ifndef COMPILE_SINGLE_THREAD
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
	
	struct thread_arg *arg = vp_arg;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		char const *src = arg->srcs->data[i], *obj = arg->objs->data[i];
		
		struct fmt_data data = {
			.conf = arg->conf,
			.src = src,
			.obj = obj,
		};
		
		mkdir_recursive(obj);
		rmdir(obj);

		char *cmd = fmt_str(arg->spec, arg->conf->cc_cmd_fmt, &data);
		
#ifndef COMPILE_SINGLE_THREAD
		pthread_mutex_lock(&mutex);
#endif
		
		++*arg->out_progress;
		printf("(%zu/%zu)\t%s", *arg->out_progress, arg->srcs->size, obj);
		if (flag_v)
			printf("\t<- %s\n", cmd);
		puts("");
		
#ifndef COMPILE_SINGLE_THREAD
		pthread_mutex_unlock(&mutex);
#endif
		
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
	struct fmt_data const *data = vp_data;
	char *cc = sanitize_path(data->conf->cc);
	string_push_str(out_cmd, cc);
	free(cc);
}

static void
fmt_cflags(struct string *out_cmd, void *vp_data)
{
	struct fmt_data const *data = vp_data;
	string_push_str(out_cmd, data->conf->cflags);
}

static void
fmt_source(struct string *out_cmd, void *vp_data)
{
	struct fmt_data const *data = vp_data;
	char *src = sanitize_path(data->src);
	string_push_str(out_cmd, src);
	free(src);
}

static void
fmt_object(struct string *out_cmd, void *vp_data)
{
	struct fmt_data const *data = vp_data;
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
	struct fmt_data const *data = vp_data;
	
	struct str_list incs = str_list_copy(&data->conf->incs);
	str_list_add(&incs, data->conf->inc_dir);

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'i', inc_fmt_include);
	
	for (size_t i = 0; i < incs.size; ++i) {
		fmt_inplace(out_cmd, &spec, data->conf->cc_inc_fmt,
		            incs.data[i]);
		
		if (i < incs.size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
	str_list_destroy(&incs);
}
