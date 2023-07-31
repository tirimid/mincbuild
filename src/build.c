#include "build.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fts.h>
#include <pthread.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "util.h"

#define INCLUDE_REGEX "#\\s*include\\s*[<\"].+[>\"]"

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
	struct strlist const *objs;
};

static struct work_info work_info_create(struct conf const *conf,
                                         struct build_info *info,
                                         size_t task_cnt);
static void work_info_destroy(struct work_info *winfo);
static struct strlist ext_files(char *dir, struct strlist const *exts);
static bool chk_inc_rebuild(struct build_info const *info, char const *path,
                            time_t obj_mt, struct conf const *conf,
                            struct strlist *chkd_incs);
static void *prune_worker(void *vp_arg);
static void comp_fmt_command(struct string *out_cmd, void *vp_data);
static void comp_fmt_cflags(struct string *out_cmd, void *vp_data);
static void comp_fmt_source(struct string *out_cmd, void *vp_data);
static void comp_fmt_object(struct string *out_cmd, void *vp_data);
static void comp_inc_fmt_include(struct string *out_cmd, void *vp_data);
static void comp_fmt_includes(struct string *out_cmd, void *vp_data);
static void *compile_worker(void *vp_arg);
static void link_fmt_command(struct string *out_cmd, void *vp_data);
static void link_fmt_ldflags(struct string *out_cmd, void *vp_data);
static void link_obj_fmt_object(struct string *out_cmd, void *vp_data);
static void link_fmt_objects(struct string *out_cmd, void *vp_data);
static void link_fmt_output(struct string *out_cmd, void *vp_data);
static void link_lib_fmt_library(struct string *out_cmd, void *vp_data);
static void link_fmt_libraries(struct string *out_cmd, void *vp_data);

struct build_info
build_info_get(struct conf const *conf)
{
	struct build_info info = {
		.srcs = ext_files(conf->proj.src_dir, &conf->proj.src_exts),
		.hdrs = ext_files(conf->proj.inc_dir, &conf->proj.hdr_exts),
		.objs = strlist_create(),
	};

	size_t src_dir_len = strlen(conf->proj.src_dir);
	size_t lib_dir_len = strlen(conf->proj.lib_dir);
	
	for (size_t i = 0; i < info.srcs.size; ++i) {
		char const *src = (char *)info.srcs.data[i] + src_dir_len;
		src += *src == '/';

		char *obj = malloc(lib_dir_len + strlen(src) + 4);
		sprintf(obj, "%s/%s.o", conf->proj.lib_dir, src);
		strlist_add(&info.objs, obj);
		free(obj);
	}
	
	return info;
}

void
build_info_destroy(struct build_info *info)
{
	strlist_destroy(&info->srcs);
	strlist_destroy(&info->hdrs);
	strlist_destroy(&info->objs);
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

	// remove sources/objects marked for pruning from strlists.
	for (size_t i = 0; i < info->srcs.size; ++i) {
		if (!info->srcs.data[i]) {
			strlist_rm_no_free(&info->srcs, i);
			strlist_rm_no_free(&info->objs, i);
			--i;
		}
	}
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

void
build_link(struct conf const *conf)
{
	log_info("linking project");

	// get all project object files, including those omitted during compilation.
	struct strlist obj_exts = strlist_create();
	strlist_add(&obj_exts, "o");
	struct strlist all_objs = ext_files(conf->proj.lib_dir, &obj_exts);

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'c', link_fmt_command);
	fmt_spec_add_ent(&spec, 'f', link_fmt_ldflags);
	fmt_spec_add_ent(&spec, 'o', link_fmt_objects);
	fmt_spec_add_ent(&spec, 'b', link_fmt_output);
	fmt_spec_add_ent(&spec, 'l', link_fmt_libraries);

	struct link_fmt_data data = {
		.conf = conf,
		.objs = &all_objs,
	};

	mkdir_recursive(conf->proj.output);
	rmdir(conf->proj.output);

	char *cmd = fmt_str(&spec, conf->tc_info.ld_cmd_fmt, &data);
	int rc = system(cmd);
	free(cmd);
	
	fmt_spec_destroy(&spec);
	strlist_destroy(&obj_exts);
	strlist_destroy(&all_objs);
	
	if (rc != conf->tc_info.ld_success_rc)
		log_fail("linking failed");
}

static struct work_info
work_info_create(struct conf const *conf, struct build_info *info,
                 size_t task_cnt)
{
	int cnt = get_nprocs();
	cnt = task_cnt < cnt ? task_cnt : cnt;
	
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

static struct strlist
ext_files(char *dir, struct strlist const *exts)
{
	unsigned long fts_opts = FTS_LOGICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
	char *const fts_dirs[] = {dir, NULL};
	FTS *fts_p = fts_open(fts_dirs, fts_opts, NULL);

	if (!fts_p)
		log_fail("error on fts_open()");

	struct strlist files = strlist_create();

	if (!fts_children(fts_p, 0))
		return files;

	FTSENT *fts_ent;
	while (fts_ent = fts_read(fts_p)) {
		if (fts_ent->fts_info != FTS_F)
			continue;

		char const *path = fts_ent->fts_path, *ext = strrchr(path, '.');
		ext = ext && ext != path ? ext + 1 : "\0";

		if (strlist_contains(exts, ext))
			strlist_add(&files, path);
	}

	fts_close(fts_p);
	return files;
}

static bool
chk_inc_rebuild(struct build_info const *info, char const *path, time_t obj_mt,
                struct conf const *conf, struct strlist *chkd_incs)
{
	// find included headers in the file.
	struct strlist incs = strlist_create();
	
	FILE *fp = fopen(path, "rb");
	if (!fp)
		log_fail("cannot open file %s for inclusion checks", path);

	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *fconts = malloc(fsize + 1);
	fread(fconts, 1, fsize, fp);
	fconts[fsize] = 0;

	fclose(fp);
	
	regex_t inc_re;
	if (regcomp(&inc_re, INCLUDE_REGEX, REG_EXTENDED | REG_NEWLINE))
		log_fail("failed to compile include regex: " INCLUDE_REGEX);

	regoff_t start = 0;
	regmatch_t match;
	while (!regexec(&inc_re, fconts + start, 1, &match, 0)) {
		char *inc;

		fconts[start + match.rm_eo - 1] = 0;
		for (inc = fconts + start + match.rm_so; !strchr("<\"", *inc); ++inc);
		++inc;

		char *inc_path = malloc(strlen(conf->proj.inc_dir) + strlen(inc) + 2);
		sprintf(inc_path, "%s/%s", conf->proj.inc_dir, inc);
		strlist_add(&incs, inc_path);
		free(inc_path);
		
		start += match.rm_eo;
	}

	free(fconts);
	regfree(&inc_re);

	// remove non-project includes from `incs`, and also includes for headers
	// which have already been checked, preventing excess resource usage and
	// hanging with coupled inclusions.
	for (size_t i = 0; i < incs.size; ++i) {
		if (!strlist_contains(&info->hdrs, incs.data[i])
		    || strlist_contains(chkd_incs, incs.data[i])) {
			strlist_rm(&incs, i);
			--i;
		}
	}
	
	// determine whether a rebuild is necessary based on gathered information.
	struct stat s;
	stat(path, &s);
	bool rebuild = difftime(s.st_mtime, obj_mt) > 0.0;
	
	for (size_t i = 0; i < incs.size && !rebuild; ++i) {
		strlist_add(chkd_incs, incs.data[i]);
		rebuild = rebuild || chk_inc_rebuild(info, incs.data[i], obj_mt, conf,
		                                     chkd_incs);
	}

	strlist_destroy(&incs);
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

		struct strlist chkd_incs = strlist_create();
		if (chk_inc_rebuild(info, info->srcs.data[i], obj_mt, conf, &chkd_incs))
			goto cleanup;

		printf("\t%s\n", (char *)info->srcs.data[i]);

		// mark source/object for removal in pruning.
		free(info->srcs.data[i]);
		free(info->objs.data[i]);
		info->srcs.data[i] = info->objs.data[i] = NULL;

	cleanup:
		strlist_destroy(&chkd_incs);
	}

	return NULL;
}

static void
comp_fmt_command(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	char *cc = sanitize_path(data->conf->tc.cc);
	string_push_str(out_cmd, cc);
	free(cc);
}

static void
comp_fmt_cflags(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	string_push_str(out_cmd, data->conf->tc.cflags);
}

static void
comp_fmt_source(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	char *src = sanitize_path(data->src);
	string_push_str(out_cmd, src);
	free(src);
}

static void
comp_fmt_object(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	char *obj = sanitize_path(data->obj);
	string_push_str(out_cmd, obj);
	free(obj);
}

static void
comp_inc_fmt_include(struct string *out_cmd, void *vp_data)
{
	char *inc = sanitize_path(vp_data);
	string_push_str(out_cmd, inc);
	free(inc);
}

static void
comp_fmt_includes(struct string *out_cmd, void *vp_data)
{
	struct comp_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	
	struct strlist incs = strlist_copy(&conf->deps.incs);
	strlist_add(&incs, conf->proj.inc_dir);

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'i', comp_inc_fmt_include);
	
	for (size_t i = 0; i < incs.size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.cc_inc_fmt, incs.data[i]);
		if (i < incs.size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
	strlist_destroy(&incs);
}

static void *
compile_worker(void *vp_arg)
{
	struct thread_arg *arg = vp_arg;
	struct conf const *conf = arg->conf;
	struct build_info const *info = arg->info;

	struct fmt_spec spec = fmt_spec_create();
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
		
		mkdir_recursive(obj);
		rmdir(obj);

		char *cmd = fmt_str(&spec, conf->tc_info.cc_cmd_fmt, &data);
		int rc = system(cmd);
		free(cmd);
		
		if (rc != conf->tc_info.cc_success_rc) {
			*arg->out_rc = rc;
			goto cleanup;
		}
	}

	*arg->out_rc = conf->tc_info.cc_success_rc;

cleanup:
	fmt_spec_destroy(&spec);
	return NULL;
}

static void
link_fmt_command(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	char *ld = sanitize_path(data->conf->tc.ld);
	string_push_str(out_cmd, ld);
	free(ld);
}

static void
link_fmt_ldflags(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	string_push_str(out_cmd, data->conf->tc.ldflags);
}

static void
link_obj_fmt_object(struct string *out_cmd, void *vp_data)
{
	char *obj = sanitize_path(vp_data);
	string_push_str(out_cmd, obj);
	free(obj);
}

static void
link_fmt_objects(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	struct strlist const *objs = data->objs;

	struct fmt_spec spec = fmt_spec_create();
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
	char *output = sanitize_path(data->conf->proj.output);
	string_push_str(out_cmd, output);
	free(output);
}

static void
link_lib_fmt_library(struct string *out_cmd, void *vp_data)
{
	string_push_str(out_cmd, vp_data);
}

static void
link_fmt_libraries(struct string *out_cmd, void *vp_data)
{
	struct link_fmt_data const *data = vp_data;
	struct conf const *conf = data->conf;
	struct strlist const *libs = &conf->deps.libs;

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'l', link_lib_fmt_library);
	
	for (size_t i = 0; i < libs->size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.ld_lib_fmt, libs->data[i]);
		if (i < libs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}
