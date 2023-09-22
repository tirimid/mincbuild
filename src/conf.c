#include "conf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <sys/stat.h>

static json_object *get_obj(json_object const *json, char const *s, char const *k, char const *file);
static char *get_str(json_object const *json, char const *s, char const *k, char const *file);
static struct strlist get_strlist(json_object const *json, char const *s, char const *k, char const *file);
static bool get_bool(json_object const *json, char const *s, char const *k, char const *file);
static int get_int(json_object const *json, char const *s, char const *k, char const *file);

struct conf
conf_from_file(char const *file)
{
	FILE *fp = fopen(file, "rb");
	if (!fp) {
		fprintf(stderr, "cannot open file: '%s'!\n", file);
		exit(1);
	}

	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *fconts = malloc(fsize + 1);
	fread(fconts, 1, fsize, fp);
	fconts[fsize] = 0;

	fclose(fp);
	
	json_object *json = json_tokener_parse(fconts);
	if (!json) {
		fprintf(stderr, "malformed JSON in %s!\n", file);
		exit(1);
	}
	
	struct conf conf;

	// first, extract only mandatory information for compilation to objects.
	conf.tc.cc = get_str(json, "tc", "cc", file);
	conf.tc.cflags = get_str(json, "tc", "cflags", file);
	conf.tc_info.cc_cmd_fmt = get_str(json, "tc-info", "cc-cmd-fmt", file);
	conf.tc_info.cc_inc_fmt = get_str(json, "tc-info", "cc-inc-fmt", file);
	conf.tc_info.cc_success_rc = get_int(json, "tc-info", "cc-success-rc", file);
	conf.proj.src_dir = get_str(json, "proj", "src-dir", file);
	conf.proj.inc_dir = get_str(json, "proj", "inc-dir", file);
	conf.proj.lib_dir = get_str(json, "proj", "lib-dir", file);
	conf.proj.produce_output = get_bool(json, "proj", "produce-output", file);
	conf.proj.src_exts = get_strlist(json, "proj", "src-exts", file);
	conf.proj.hdr_exts = get_strlist(json, "proj", "hdr-exts", file);
	conf.deps.incs = get_strlist(json, "deps", "incs", file);

	// then, if output should be produced, get necessary information for
	// linker to be run after compilation.
	if (conf.proj.produce_output) {
		conf.tc.ld = get_str(json, "tc", "ld", file);
		conf.tc.ldflags = get_str(json, "tc", "ldflags", file);
		conf.tc_info.ld_lib_fmt = get_str(json, "tc-info", "ld-lib-fmt", file);
		conf.tc_info.ld_obj_fmt = get_str(json, "tc-info", "ld-obj-fmt", file);
		conf.tc_info.ld_cmd_fmt = get_str(json, "tc-info", "ld-cmd-fmt", file);
		conf.tc_info.ld_success_rc = get_int(json, "tc-info", "ld-success-rc", file);
		conf.proj.output = get_str(json, "proj", "output", file);
		conf.deps.libs = get_strlist(json, "deps", "libs", file);
	}

	json_object_put(json);
	free(fconts);
	
	return conf;
}

void
conf_validate(struct conf const *conf)
{
	struct stat s;
	
	// make sure project has specified directories.
	if (stat(conf->proj.src_dir, &s)) {
		fprintf(stderr, "no source directory: '%s'!\n", conf->proj.src_dir);
		exit(1);
	}

	if (stat(conf->proj.inc_dir, &s)) {
		fprintf(stderr, "no include directory, creating: '%s'\n", conf->proj.inc_dir);
		mkdir_recursive(conf->proj.inc_dir);
	}

	if (stat(conf->proj.lib_dir, &s)) {
		fprintf(stderr, "no build directory, creating: '%s'\n", conf->proj.lib_dir);
		mkdir_recursive(conf->proj.lib_dir);
	}

	// make sure system has specified compiler and linker.
	if (stat(conf->tc.cc, &s)) {
		fprintf(stderr, "compiler not present on system: '%s'!\n", conf->tc.cc);
		exit(1);
	}

	if (conf->proj.produce_output && stat(conf->tc.ld, &s)) {
		fprintf(stderr, "linker not present on system: '%s'!\n", conf->tc.ld);
		exit(1);
	}
}

void
conf_destroy(struct conf *conf)
{
	free(conf->tc.cc);
	free(conf->tc.cflags);
	free(conf->tc_info.cc_cmd_fmt);
	free(conf->tc_info.cc_inc_fmt);
	free(conf->proj.src_dir);
	free(conf->proj.inc_dir);
	free(conf->proj.lib_dir);
	strlist_destroy(&conf->proj.src_exts);
	strlist_destroy(&conf->proj.hdr_exts);

	if (conf->proj.produce_output) {
		free(conf->tc.ld);
		free(conf->tc.ldflags);
		free(conf->tc_info.ld_lib_fmt);
		free(conf->tc_info.ld_obj_fmt);
		free(conf->tc_info.ld_cmd_fmt);
		free(conf->proj.output);
		strlist_destroy(&conf->deps.incs);
		strlist_destroy(&conf->deps.libs);
	}
}

static json_object *
get_obj(json_object const *json, char const *s, char const *k, char const *file)
{
	json_object *s_obj;
	if (!json_object_object_get_ex(json, s, &s_obj)) {
		fprintf(stderr, "%s must have section '%s'!\n", file, s);
		exit(1);
	}

	json_object *k_obj;
	if (!json_object_object_get_ex(s_obj, k, &k_obj)) {
		fprintf(stderr, "%s must have key '%s' in section '%s'!\n", file, k, s);
		exit(1);
	}
	
	return k_obj;
}

static char *
get_str(json_object const *json, char const *s, char const *k, char const *file)
{
	json_object *obj = get_obj(json, s, k, file);
	if (!json_object_is_type(obj, json_type_string)) {
		fprintf(stderr, "in %s, '%s.%s' must be a string!\n", file, s, k);
		exit(1);
	}

	size_t len = json_object_get_string_len(obj);
	char const *str = json_object_get_string(obj);
	return strndup(str, len);
}

static struct strlist
get_strlist(json_object const *json, char const *s, char const *k, char const *file)
{
	json_object *arr_obj = get_obj(json, s, k, file);
	if (!json_object_is_type(arr_obj, json_type_array)) {
		fprintf(stderr, "in %s, '%s.%s' must be a string array!\n", file, s, k);
		exit(1);
	}

	struct strlist sl = strlist_create();

	for (size_t i = 0; i < json_object_array_length(arr_obj); ++i) {
		json_object *child_obj = json_object_array_get_idx(arr_obj, i);
		if (!json_object_is_type(child_obj, json_type_string)) {
			fprintf(stderr, "in %s, '%s.%s' must only have strings!\n", file, s, k);
			exit(1);
		}

		strlist_add(&sl, json_object_get_string(child_obj));
	}
	
	return sl;
}

static bool
get_bool(json_object const *json, char const *s, char const *k, char const *file)
{
	json_object *obj = get_obj(json, s, k, file);
	if (!json_object_is_type(obj, json_type_boolean)) {
		fprintf(stderr, "in %s, '%s.%s' must be a bool!\n", file, s, k);
		exit(1);
	}

	return json_object_get_boolean(obj);
}

static int
get_int(json_object const *json, char const *s, char const *k, char const *file)
{
	json_object *obj = get_obj(json, s, k, file);
	if (!json_object_is_type(obj, json_type_int)) {
		fprintf(stderr, "in %s, '%s.%s' must be an int!\n", file, s, k);
		exit(1);
	}

	return json_object_get_int(obj);
}
