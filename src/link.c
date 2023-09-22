#include "link.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include "util.h"

struct fmtdata {
	struct conf const *conf;
	struct strlist const *objs;
};

static void fmt_command(struct string *out_cmd, void *vp_data);
static void fmt_ldflags(struct string *out_cmd, void *vp_data);
static void obj_fmt_object(struct string *out_cmd, void *vp_data);
static void fmt_objects(struct string *out_cmd, void *vp_data);
static void fmt_output(struct string *out_cmd, void *vp_data);
static void lib_fmt_library(struct string *out_cmd, void *vp_data);
static void fmt_libraries(struct string *out_cmd, void *vp_data);

void
linkobjs(struct conf const *conf)
{
	puts("linking project");

	// get all project object files, including those omitted during compilation.
	struct strlist obj_exts = strlist_create();
	strlist_add(&obj_exts, "o");
	struct strlist objs = extfind(conf->proj.lib_dir, &obj_exts);
	strlist_destroy(&obj_exts);

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'c', fmt_command);
	fmt_spec_add_ent(&spec, 'f', fmt_ldflags);
	fmt_spec_add_ent(&spec, 'o', fmt_objects);
	fmt_spec_add_ent(&spec, 'b', fmt_output);
	fmt_spec_add_ent(&spec, 'l', fmt_libraries);

	struct fmtdata data = {
		.conf = conf,
		.objs = &objs,
	};

	mkdir_recursive(conf->proj.output);
	rmdir(conf->proj.output);

	char *cmd = fmt_str(&spec, conf->tc_info.ld_cmd_fmt, &data);
	fmt_spec_destroy(&spec);
	strlist_destroy(&objs);
	
	int rc = system(cmd);
	free(cmd);
	
	if (rc != conf->tc_info.ld_success_rc) {
		fputs("linking failed!\n", stderr);
		exit(1);
	}
}

static void
fmt_command(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *ld = sanitize_path(data->conf->tc.ld);
	string_push_str(out_cmd, ld);
	free(ld);
}

static void
fmt_ldflags(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	string_push_str(out_cmd, data->conf->tc.ldflags);
}

static void
obj_fmt_object(struct string *out_cmd, void *vp_data)
{
	char *obj = sanitize_path(vp_data);
	string_push_str(out_cmd, obj);
	free(obj);
}

static void
fmt_objects(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	struct conf const *conf = data->conf;
	struct strlist const *objs = data->objs;

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'o', obj_fmt_object);
	
	for (size_t i = 0; i < objs->size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.ld_obj_fmt, objs->data[i]);
		if (i < objs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}

static void
fmt_output(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *output = sanitize_path(data->conf->proj.output);
	string_push_str(out_cmd, output);
	free(output);
}

static void
lib_fmt_library(struct string *out_cmd, void *vp_data)
{
	string_push_str(out_cmd, vp_data);
}

static void
fmt_libraries(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	struct conf const *conf = data->conf;
	struct strlist const *libs = &conf->deps.libs;

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'l', lib_fmt_library);
	
	for (size_t i = 0; i < libs->size; ++i) {
		fmt_inplace(out_cmd, &spec, conf->tc_info.ld_lib_fmt, libs->data[i]);
		if (i < libs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}
