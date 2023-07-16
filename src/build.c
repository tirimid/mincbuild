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
#include <tmcul/ds/string.h>
#include <tmcul/file.h>
#include <tmcul/log.h>
#include <tmcul/fmt.h>

#include "util.h"

#define INCLUDE_REGEX "#\\s*include\\s*[<\\\"].*[>\\\"]"
#define FMT_SPEC_CH '%'

struct thread_arg {
	size_t start, cnt;
	struct conf const *conf;
	struct build_info *info;
	int *out_rc;
	size_t *out_progress;
};

struct work_info {
	int cnt;
	int *rcs;
	struct thread_arg *args;
	pthread_t *workers;
	size_t progress;
};

struct comp_fmt_data {
	struct conf const *conf;
	char const *src, *obj;
};

struct link_fmt_data {
	struct conf const *conf;
	struct arraylist const *objs;
};

static int
get_worker_cnt(size_t task_cnt)
{
	FILE *fp = popen(CMD_NPROC, "r");
	if (!fp)
		log_fail("failed to popen() " CMD_NPROC);

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
		.progress = 0,
	};

	for (int i = 0; i < cnt; ++i) {
		winfo.args[i] = (struct thread_arg){
			.conf = conf,
			.info = info,
			.out_rc = &winfo.rcs[i],
			.out_progress = &winfo.progress,
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

		if (arraylist_contains(exts, ext, strlen(ext) + 1))
			arraylist_add(&files, path, path_len);
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
chk_inc_rebuild(struct build_info const *info, char const *path, time_t obj_mt,
                struct conf const *conf, struct arraylist *chkd_incs)
{
	char *path_san = sanitize_cmd(path);
	struct arraylist incs = arraylist_create();

	size_t grep_len = strlen(CMD_GREP), inc_regex_len = strlen(INCLUDE_REGEX);

	// find all includes in file, and add them to `incs`.
	char *cmd = malloc(grep_len + strlen(path_san) + inc_regex_len + 5);
	sprintf(cmd, CMD_GREP " \"" INCLUDE_REGEX "\" %s", path_san);

	FILE *fp = popen(cmd, "r");
	if (!fp)
		log_fail("failed to popen() " CMD_GREP);

	free(cmd);
	free(path_san);

	char *inc = NULL;
	size_t inc_len;
	while (getline(&inc, &inc_len, fp) != -1) {
		char *inc_file;
		
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

	// remove non-project includes from `incs`, and also includes for headers
	// which have already been checked, preventing excess resource usage and
	// hanging with coupled inclusions.
	for (size_t i = 0; i < incs.size; ++i) {
		size_t idlen = strlen(incs.data[i]);
		
		if (!arraylist_contains(&info->hdrs, incs.data[i], idlen)
		    || arraylist_contains(chkd_incs, incs.data[i], idlen)) {
			arraylist_rm(&incs, i);
			--i;
		}
	}
	
	// determine whether a rebuild is necessary based on gathered information.
	struct stat s;
	stat(path, &s);
	bool rebuild = difftime(s.st_mtime, obj_mt) > 0.0;

	for (size_t i = 0; i < incs.size && !rebuild; ++i) {
		arraylist_add(chkd_incs, incs.data[i], incs.data_sizes[i]);
		rebuild = rebuild || chk_inc_rebuild(info, incs.data[i], obj_mt, conf,
		                                     chkd_incs);
	}

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
		if (dt > 0.0)
			continue;

		struct arraylist chkd_incs = arraylist_create();
		if (chk_inc_rebuild(info, info->srcs.data[i], obj_mt, conf, &chkd_incs))
			goto cleanup;

		printf("\t%s\n", (char *)info->srcs.data[i]);

		// mark source/object for removal in pruning.
		free(info->srcs.data[i]);
		free(info->objs.data[i]);
		info->srcs.data[i] = info->objs.data[i] = NULL;

	cleanup:
		arraylist_destroy(&chkd_incs);
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
	for (int i = 0; i < winfo.cnt; ++i) {
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

static void
comp_fmt_command(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->conf->tc.cc);
}

static void
comp_fmt_cflags(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->conf->tc.cflags);
}

static void
comp_fmt_source(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->src);
}

static void
comp_fmt_object(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->obj);
}

static void
comp_inc_fmt_escape(struct string *out_cmd, void *vp_data)
{
	string_push_ch(out_cmd, FMT_SPEC_CH);
}

static void
comp_inc_fmt_include(struct string *out_cmd, void *vp_data)
{
	string_push_c_str(out_cmd, vp_data);
}

static void
comp_fmt_includes(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	
	struct arraylist incs = arraylist_copy(&conf->deps.incs);
	arraylist_add(&incs, conf->proj.inc_dir, strlen(conf->proj.inc_dir) + 1);

	struct fmt_spec spec = fmt_spec_create(FMT_SPEC_CH);
	fmt_spec_add_ent(&spec, 0, comp_inc_fmt_escape);
	fmt_spec_add_ent(&spec, FMT_SPEC_CH, comp_inc_fmt_escape);
	fmt_spec_add_ent(&spec, 'i', comp_inc_fmt_include);
	
	for (size_t i = 0; i < incs.size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.cc_inc_fmt, incs.data[i]);
		if (i < incs.size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
	arraylist_destroy(&incs);
}

static void
comp_fmt_escape(struct string *out_cmd, void *vp_data)
{
	string_push_ch(out_cmd, FMT_SPEC_CH);
}

static void *
compile_worker(void *vp_arg)
{
	struct thread_arg *arg = vp_arg;
	struct conf const *conf = arg->conf;
	struct build_info const *info = arg->info;

	struct fmt_spec spec = fmt_spec_create(FMT_SPEC_CH);
	fmt_spec_add_ent(&spec, 0, comp_fmt_escape);
	fmt_spec_add_ent(&spec, FMT_SPEC_CH, comp_fmt_escape);
	fmt_spec_add_ent(&spec, 'c', comp_fmt_command);
	fmt_spec_add_ent(&spec, 'f', comp_fmt_cflags);
	fmt_spec_add_ent(&spec, 's', comp_fmt_source);
	fmt_spec_add_ent(&spec, 'o', comp_fmt_object);
	fmt_spec_add_ent(&spec, 'i', comp_fmt_includes);

	for (size_t i = arg->start; i < arg->start + arg->cnt; ++i) {
		char const *src = info->srcs.data[i], *obj = info->objs.data[i];
		printf("(%zu/%zu)\t%s\n", ++*arg->out_progress, info->srcs.size, obj);

		struct comp_fmt_data data = {
			.conf = conf,
			.src = src,
			.obj = obj,
		};
		
		char *cmd_unsan = fmt_c_str(&spec, conf->tc_info.cc_cmd_fmt, &data);
		char *cmd_san = sanitize_cmd(cmd_unsan);
		free(cmd_unsan);

		cmd_mkdir_p(obj);
		cmd_rmdir(obj);

		int rc = system(cmd_san);
		free(cmd_san);
		
		if (rc != conf->tc_info.cc_success_rc) {
			*arg->out_rc = rc;
			return NULL;
		}
	}

	fmt_spec_destroy(&spec);

	*arg->out_rc = conf->tc_info.cc_success_rc;
	return NULL;
}

void
build_compile(struct conf const *conf, struct build_info *info)
{
	struct work_info winfo = work_info_create(conf, info, info->srcs.size);
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

static void
link_fmt_command(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->conf->tc.ld);
}

static void
link_fmt_ldflags(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->conf->tc.ldflags);
}

static void
link_obj_fmt_escape(struct string *out_cmd, void *vp_data)
{
	string_push_ch(out_cmd, FMT_SPEC_CH);
}

static void
link_obj_fmt_object(struct string *out_cmd, void *vp_data)
{
	string_push_c_str(out_cmd, vp_data);
}

static void
link_fmt_objects(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	struct arraylist const *objs = data->objs;

	struct fmt_spec spec = fmt_spec_create(FMT_SPEC_CH);
	fmt_spec_add_ent(&spec, 0, link_obj_fmt_escape);
	fmt_spec_add_ent(&spec, FMT_SPEC_CH, link_obj_fmt_escape);
	fmt_spec_add_ent(&spec, 'o', link_obj_fmt_object);
	
	for (size_t i = 0; i < objs->size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.ld_obj_fmt, objs->data[i]);
		if (i < objs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}

static void
link_fmt_output(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	string_push_c_str(out_cmd, data->conf->proj.output);
}

static void
link_lib_fmt_escape(struct string *out_cmd, void *vp_data)
{
	string_push_ch(out_cmd, FMT_SPEC_CH);
}

static void
link_lib_fmt_library(struct string *out_cmd, void *vp_data)
{
	string_push_c_str(out_cmd, vp_data);
}

static void
link_fmt_libraries(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	struct arraylist const *libs = &conf->deps.libs;

	struct fmt_spec spec = fmt_spec_create(FMT_SPEC_CH);
	fmt_spec_add_ent(&spec, 0, link_lib_fmt_escape);
	fmt_spec_add_ent(&spec, FMT_SPEC_CH, link_lib_fmt_escape);
	fmt_spec_add_ent(&spec, 'l', link_lib_fmt_library);
	
	for (size_t i = 0; i < libs->size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.ld_lib_fmt, libs->data[i]);
		if (i < libs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}

static void
link_fmt_escape(struct string *out_cmd, void *vp_data)
{
	string_push_ch(out_cmd, FMT_SPEC_CH);
}

void
build_link(struct conf const *conf)
{
	log_info("linking project");

	// get all project object files, including those omitted during compilation.
	struct arraylist obj_exts = arraylist_create();
	arraylist_add(&obj_exts, "o", 2);
	struct arraylist all_objs = ext_files(conf->proj.lib_dir, &obj_exts);

	struct fmt_spec spec = fmt_spec_create(FMT_SPEC_CH);
	fmt_spec_add_ent(&spec, 0, link_fmt_escape);
	fmt_spec_add_ent(&spec, FMT_SPEC_CH, link_fmt_escape);
	fmt_spec_add_ent(&spec, 'c', link_fmt_command);
	fmt_spec_add_ent(&spec, 'f', link_fmt_ldflags);
	fmt_spec_add_ent(&spec, 'o', link_fmt_objects);
	fmt_spec_add_ent(&spec, 'b', link_fmt_output);
	fmt_spec_add_ent(&spec, 'l', link_fmt_libraries);

	struct link_fmt_data data = {
		.conf = conf,
		.objs = &all_objs,
	};

	char *cmd_unsan = fmt_c_str(&spec, conf->tc_info.ld_cmd_fmt, &data);
	char *cmd_san = sanitize_cmd(cmd_unsan);
	free(cmd_unsan);

	fmt_spec_destroy(&spec);
	arraylist_destroy(&obj_exts);
	arraylist_destroy(&all_objs);

	cmd_mkdir_p(conf->proj.output);
	cmd_rmdir(conf->proj.output);

	int rc = system(cmd_san);
	free(cmd_san);
	
	if (rc != conf->tc_info.ld_success_rc)
		log_fail("linking failed");
}
