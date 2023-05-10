#include "build.h"

#define _POSIX_C_SOURCE 2
#include <stdio.h>
#undef _POSIX_C_SOURCE

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <fts.h>
#include <sys/stat.h>
#include <pthread.h>

#include "util.h"

struct thread_arg {
	size_t start, cnt;
	struct conf const *conf;
	struct build_info const *info;
	int *out_rc;
};

static size_t files_compiled;

static struct arraylist ext_files(char *dir, struct arraylist const *exts)
{
	unsigned long fts_opts = FTS_LOGICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
	char *const fts_dirs[] = {dir, NULL};
	FTS *fts_p = fts_open(fts_dirs, fts_opts, NULL);

	if (!fts_p)
		log_fail("error on fts_open()");

	struct arraylist files = arraylist_create();

	if (!fts_children(fts_p, 0))
		return files;

	FTSENT *fts_ent;
	while (fts_ent = fts_read(fts_p)) {
		if (fts_ent->fts_info != FTS_F)
			continue;

		char const *path = fts_ent->fts_path, *ext = file_ext(path);
		size_t path_len = fts_ent->fts_pathlen + 1;
		
		for (size_t i = 0; i < exts->size; ++i) {
			if (!strcmp(exts->data[i], ext)) {
				arraylist_add(&files, path, path_len);
				break;
			}
		}
	}

	fts_close(fts_p);
	return files;
}

struct build_info build_info_get(struct conf const *conf)
{
	struct build_info info = {
		.srcs = ext_files(conf->proj.src_dir, &conf->proj.src_exts),
		.hdrs = ext_files(conf->proj.inc_dir, &conf->proj.hdr_exts),
		.objs = arraylist_create(),
	};

	size_t src_dir_len = strlen(conf->proj.src_dir);
	size_t lib_dir_len = strlen(conf->proj.lib_dir);
	
	for (size_t i = 0; i < info.srcs.size; ++i) {
		char const *src = (char *)info.srcs.data[i] + src_dir_len;
		src += *src == '/' ? 1 : 0;

		char *obj = malloc(lib_dir_len + strlen(src) + 4);
		sprintf(obj, "%s/%s.o", conf->proj.lib_dir, src);
		arraylist_add(&info.objs, obj, strlen(obj) + 1);
		free(obj);
	}
	
	return info;
}

void build_info_destroy(struct build_info *info)
{
	arraylist_destroy(&info->srcs);
	arraylist_destroy(&info->hdrs);
	arraylist_destroy(&info->objs);
}

void build_prune(struct build_info *info)
{
	// TODO: add pruning to remove unnecessary compilation.
}

static void *compile_worker(void *vp_arg)
{
	struct thread_arg *arg = vp_arg;
	struct conf const *conf = arg->conf;
	struct build_info const *info = arg->info;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		char const *src = info->srcs.data[i], *obj = info->objs.data[i];
		printf("(%zu/%zu)\t%s\n", ++files_compiled, info->srcs.size, obj);

		struct string cmd = string_create();

		// build base command.
		string_push_c_str_n(&cmd, conf->tc.cc, " ", conf->tc.cflags, " ",
		                    conf->tc_info.cc_cobj_flag, " ", obj, " ",
		                    conf->tc_info.cc_conly_flag, " ", src, " ",
		                    conf->tc_info.cc_inc_flag, " ", conf->proj.inc_dir,
		                    " ", NULL);

		// add inclusion dependencies.
		for (size_t j = 0; j < conf->deps.incs.size; ++j) {
			string_push_c_str_n(&cmd, conf->tc_info.cc_inc_flag, " ",
			                    conf->deps.incs.data[j], " ", NULL);
		}

		char *cmd_unsan = string_to_c_str(&cmd);
		string_destroy(&cmd);
		char *cmd_san = sanitize_cmd(cmd_unsan);
		free(cmd_unsan);

		int rc = system(cmd_san);
		free(cmd_san);
		
		if (rc != conf->tc_info.cc_success_rc) {
			*arg->out_rc = rc;
			return NULL;
		}
	}

	*arg->out_rc = conf->tc_info.cc_success_rc;
	return NULL;
}

void build_compile(struct conf const *conf, struct build_info const *info)
{
	FILE *fp = popen("nproc", "r");
	if (!fp)
		log_fail("failed to popen() of nproc");

	// we can (hopefully) assume the user doesn't have more than ~2 billion
	// cores to compile with, as that would be a little strange.
	char buf[32] = {0};
	int worker_cnt = atoi(fgets(buf, 32, fp));
	if (worker_cnt == 0)
		log_fail("no workers available for compilation");
	
	pclose(fp);

	files_compiled = 0;
	log_info("compiling project with %d workers", worker_cnt);
	
	pthread_t *workers = malloc(sizeof(*workers) * worker_cnt);
	int *worker_rcs = malloc(sizeof(*worker_rcs) * worker_cnt);
	struct thread_arg *worker_args = malloc(sizeof(*worker_args) * worker_cnt);
	
	for (int i = 0; i < worker_cnt; ++i) {
		size_t norm_cnt = info->objs.size / worker_cnt;
		size_t last_cnt = info->objs.size - i * norm_cnt;
		
		worker_args[i] = (struct thread_arg){
			.start = norm_cnt * i,
			.cnt = i == worker_cnt - 1 ? last_cnt : norm_cnt,
			.conf = conf,
			.info = info,
			.out_rc = &worker_rcs[i],
		};

		if (pthread_create(&workers[i], NULL, compile_worker, &worker_args[i]))
			log_fail("failed on pthread_create()");
	}

	for (int i = 0; i < worker_cnt; ++i)
		pthread_join(workers[i], NULL);

	for (int i = 0; i < worker_cnt; ++i) {
		if (worker_rcs[i] != conf->tc_info.cc_success_rc)
			log_fail("compilation failed on some module(s)");
	}

	free(workers);
	free(worker_rcs);
	free(worker_args);
}

void build_link(struct conf const *conf, struct build_info const *info)
{
	log_info("linking project");

	struct string cmd = string_create();

	// build base command.
	string_push_c_str_n(&cmd, conf->tc.ld, " ", conf->tc.ldflags, " ",
	                    conf->tc_info.ld_lbin_flag, " ", conf->proj.output, " ",
	                    NULL);

	// add all project object files, including those omitted during compilation.
	struct arraylist obj_exts = arraylist_create();
	arraylist_add(&obj_exts, "o", 2);
	struct arraylist all_objs = ext_files(conf->proj.lib_dir, &obj_exts);

	for (size_t i = 0; i < all_objs.size; ++i)
		string_push_c_str_n(&cmd, all_objs.data[i], " ", NULL);

	arraylist_destroy(&obj_exts);
	arraylist_destroy(&all_objs);

	// add library dependencies.
	for (size_t i = 0; i < conf->deps.libs.size; ++i) {
		string_push_c_str_n(&cmd, conf->tc_info.ld_lib_flag, " ",
		                    conf->deps.libs.data[i], " ", NULL);
	}

	char *cmd_unsan = string_to_c_str(&cmd);
	string_destroy(&cmd);
	char *cmd_san = sanitize_cmd(cmd_unsan);
	free(cmd_unsan);

	int rc = system(cmd_san);
	free(cmd_san);
	
	if (rc != conf->tc_info.ld_success_rc)
		log_fail("linking failed");
}
