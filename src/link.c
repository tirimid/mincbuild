#include "link.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include "util.h"

struct fmtdata {
	struct conf const *conf;
	struct strlist const *objs;
};

extern bool flag_v;

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
	
	// get all project object files, including those omitted during
	// compilation.
	struct strlist obj_exts = strlist_create();
	strlist_add(&obj_exts, "o");
	struct strlist objs = extfind(conf->lib_dir, &obj_exts);
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

	mkdir_recursive(conf->output);
	rmdir(conf->output);

	char *cmd = fmt_str(&spec, conf->ld_cmd_fmt, &data);
	fmt_spec_destroy(&spec);
	strlist_destroy(&objs);

	if (flag_v)
		printf("(+)\t%s\t<- %s\n", conf->output, cmd);
	else
		printf("(+)\t%s\n", conf->output);
	
	int rc = system(cmd);
	free(cmd);
	
	if (rc != conf->ld_success_rc) {
		fputs("linking failed!\n", stderr);
		exit(1);
	}
}

static void
fmt_command(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *ld = sanitize_path(data->conf->ld);
	string_push_str(out_cmd, ld);
	free(ld);
}

static void
fmt_ldflags(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	string_push_str(out_cmd, data->conf->ldflags);
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

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'o', obj_fmt_object);
	
	for (size_t i = 0; i < data->objs->size; ++i) {
		fmt_inplace(out_cmd, &spec, data->conf->ld_obj_fmt, data->objs->data[i]);
		if (i < data->objs->size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}

static void
fmt_output(struct string *out_cmd, void *vp_data)
{
	struct fmtdata const *data = vp_data;
	char *output = sanitize_path(data->conf->output);
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

	struct fmt_spec spec = fmt_spec_create();
	fmt_spec_add_ent(&spec, 'l', lib_fmt_library);
	
	for (size_t i = 0; i < data->conf->libs.size; ++i) {
		fmt_inplace(out_cmd, &spec, data->conf->ld_lib_fmt, data->conf->libs.data[i]);
		if (i < data->conf->libs.size - 1)
			string_push_ch(out_cmd, ' ');
	}

	fmt_spec_destroy(&spec);
}
