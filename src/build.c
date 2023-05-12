#include "build.h"

#define _POSIX_C_SOURCE 200809
#include <stdio.h>
#undef _POSIX_C_SOURCE

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <fts.h>
#include <sys/stat.h>
#include <pthread.h>

#include "util.h"

// the `%s` should be formatted and replaced with an extension when this regex
// is used, so that multiple header extensions; e.g. `.h`, `.hh`, `.hxx` can be
// supported instead of just those defined in the regex.
#define INCLUDE_REGEX "#\\s*include\\s*[<\\\"].*\\.%s[>\\\"]"
#define INCLUDE_REGEX_LEN 28

struct thread_arg {
	size_t start, cnt;
	struct conf const *conf;
	struct build_info *info;
	int *out_rc;
};

struct work_info {
	int cnt;
	int *rcs;
	struct thread_arg *args;
	pthread_t *workers;
};

static size_t files_compiled;

static int
get_worker_cnt(size_t task_cnt)
{
	FILE *fp = popen("nproc", "r");
	if (!fp)
		log_fail("failed to popen() nproc");

	char buf[32] = {0};
	int worker_cnt = atoi(fgets(buf, 32, fp));
	if (worker_cnt == 0)
		log_fail("no workers available for task");
	
	pclose(fp);

	return task_cnt < worker_cnt ? task_cnt : worker_cnt;
}

static struct work_info
work_info_create(struct conf const *conf, struct build_info *info,
                 size_t task_cnt)
{
	int cnt = get_worker_cnt(task_cnt);
	
	struct work_info winfo = {
		.cnt = cnt,
		.rcs = malloc(cnt * sizeof(int)),
		.args = malloc(cnt * sizeof(struct thread_arg)),
		.workers = malloc(cnt * sizeof(pthread_t)),
	};

	for (int i = 0; i < cnt; ++i) {
		winfo.args[i] = (struct thread_arg){
			.conf = conf,
			.info = info,
			.out_rc = &winfo.rcs[i],
		};
	}

	return winfo;
}

static void
work_info_destroy(struct work_info *winfo)
{
	free(winfo->rcs);
	free(winfo->args);
	free(winfo->workers);
}

static struct arraylist
ext_files(char *dir, struct arraylist const *exts)
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

struct build_info
build_info_get(struct conf const *conf)
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

void
build_info_destroy(struct build_info *info)
{
	arraylist_destroy(&info->srcs);
	arraylist_destroy(&info->hdrs);
	arraylist_destroy(&info->objs);
}

static bool
chk_inc_rebuild(struct build_info const *info, char const *path,
                struct conf const *conf, time_t obj_mt)
{
	char *path_san = sanitize_cmd(path);
	struct arraylist incs = arraylist_create();
	
	for (size_t i = 0; i < conf->proj.hdr_exts.size; ++i) {
		char const *ext = conf->proj.hdr_exts.data[i];
		size_t path_len = strlen(path_san), ext_len = strlen(ext);
		char *cmd = malloc(path_len + INCLUDE_REGEX_LEN + ext_len + 9);
		sprintf(cmd, "grep \"" INCLUDE_REGEX "\" %s", ext, path_san);

		FILE *fp = popen(cmd, "r");
		if (!fp)
			log_fail("failed to popen() grep");

		char *inc = NULL, *inc_file;
		size_t inc_len;
		while (getline(&inc, &inc_len, fp) != -1) {
			// isolate filename of include directive, which can then be accessed
			// via the `inc_file` variable.
			for (inc_file = inc; !strchr("<\"", *inc_file); ++inc_file);
			*inc_file++ = 0;
			
			for (inc_len = 0; !strchr(">\"", inc_file[inc_len]); ++inc_len);
			inc_file[inc_len] = 0;

			// then, add found includes to `incs`.
			size_t inc_path_size = strlen(conf->proj.inc_dir) + inc_len + 2;
			char *inc_path = malloc(inc_path_size);
			sprintf(inc_path, "%s/%s", conf->proj.inc_dir, inc_file);
			arraylist_add(&incs, inc_path, inc_path_size);
			
			free(inc_path);
		}
		
		pclose(fp);
		free(inc);
		free(cmd);
	}

	// remove non-project includes from `incs`, since it would make no sense to
	// rebuild based on whether those were modified; checking whether a
	// dependency change requires a rebuild should be on the user.
	for (size_t i = 0; i < incs.size; ++i) {
		bool proj_hdr = false;
		
		for (size_t j = 0; j < info->hdrs.size; ++j) {
			if (!strcmp(incs.data[i], info->hdrs.data[j])) {
				proj_hdr = true;
				break;
			}
		}

		if (!proj_hdr) {
			arraylist_rm(&incs, i);
			--i;
		}
	}

	// determine whether a rebuild is necessary.
	struct stat s;
	stat(path, &s);
	bool rebuild = difftime(s.st_mtime, obj_mt) > 0.0;

	for (size_t i = 0; i < incs.size && !rebuild; ++i)
		rebuild = rebuild || chk_inc_rebuild(info, incs.data[i], conf, obj_mt);

	free(path_san);
	arraylist_destroy(&incs);
	
	return rebuild;
}

static void *
prune_worker(void *vp_arg)
{
	struct thread_arg *arg = vp_arg;
	struct conf const *conf = arg->conf;
	struct build_info *info = arg->info;

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		struct stat s_src;
		stat(info->srcs.data[i], &s_src);

		struct stat s_obj;
		if (stat(info->objs.data[i], &s_obj))
			continue;

		time_t src_mt = s_src.st_mtime, obj_mt = s_obj.st_mtime;
		double dt = difftime(src_mt, obj_mt);
		if (dt > 0.0 || chk_inc_rebuild(info, info->srcs.data[i], conf, obj_mt))
			continue;

		printf("\t%s\n", (char *)info->srcs.data[i]);

		// mark source/object for removal in pruning.
		free(info->srcs.data[i]);
		free(info->objs.data[i]);
		info->srcs.data[i] = info->objs.data[i] = NULL;
	}

	return NULL;
}

void
build_prune(struct conf const *conf, struct build_info *info)
{
	struct work_info winfo = work_info_create(conf, info, info->srcs.size);
	log_info("pruning compilation with %d worker(s)", winfo.cnt);

	for (size_t i = 0; i < info->srcs.size; ++i)
		++winfo.args[i % winfo.cnt].cnt;

	// create threads to mark which sources should be pruned.
	for (size_t i = 0; i < info->srcs.size; ++i) {
		struct thread_arg *args = winfo.args, *prev = &args[i == 0 ? i : i - 1];
		args[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		
		if (pthread_create(&winfo.workers[i], NULL, prune_worker, &args[i]))
			log_fail("failed on pthread_create()");
	}

	for (int i = 0; i < winfo.cnt; ++i)
		pthread_join(winfo.workers[i], NULL);

	work_info_destroy(&winfo);

	// remove sources/objects marked for pruning from arraylists.
	for (size_t i = 0; i < info->srcs.size; ++i) {
		if (!info->srcs.data[i]) {
			arraylist_rm_no_free(&info->srcs, i);
			arraylist_rm_no_free(&info->objs, i);
			--i;
		}
	}
}

static void *
compile_worker(void *vp_arg)
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

		file_mkdir_p(obj);
		file_rmdir(obj);

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

void
build_compile(struct conf const *conf, struct build_info *info)
{
	struct work_info winfo = work_info_create(conf, info, info->srcs.size);
	files_compiled = 0;
	log_info("compiling project with %d worker(s)", winfo.cnt);

	for (size_t i = 0; i < info->objs.size; ++i)
		++winfo.args[i % winfo.cnt].cnt;

	for (int i = 0; i < winfo.cnt; ++i) {
		struct thread_arg *args = winfo.args, *prev = &args[i == 0 ? i : i - 1];
		args[i].start = i == 0 ? 0 : prev->start + prev->cnt;
		
		if (pthread_create(&winfo.workers[i], NULL, compile_worker, &args[i]))
			log_fail("failed on pthread_create()");
	}

	for (int i = 0; i < winfo.cnt; ++i)
		pthread_join(winfo.workers[i], NULL);

	for (int i = 0; i < winfo.cnt; ++i) {
		if (winfo.rcs[i] != conf->tc_info.cc_success_rc)
			log_fail("compilation failed on some module(s)");
	}

	work_info_destroy(&winfo);
}

void
build_link(struct conf const *conf, struct build_info const *info)
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

	file_mkdir_p(conf->proj.output);
	file_rmdir(conf->proj.output);

	int rc = system(cmd_san);
	free(cmd_san);
	
	if (rc != conf->tc_info.ld_success_rc)
		log_fail("linking failed");
}
