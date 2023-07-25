#include "conf.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <json-c/json.h>
#include <sys/stat.h>

#include "util.h"

static json_object *
json_get(json_object const *json, char const *sct, char const *key,
         char const *file)
{
	json_object *sct_obj;
	if (!json_object_object_get_ex(json, sct, &sct_obj))
		log_fail("%s must have section '%s'", file, sct);

	json_object *key_obj;
	if (!json_object_object_get_ex(sct_obj, key, &key_obj))
		log_fail("%s must have key '%s' in section '%s'", file, key, sct);
	
	return key_obj;
}

static char *
json_get_str(json_object const *json, char const *sct, char const *key,
             char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_string))
		log_fail("in %s, '%s.%s' must be a string", file, sct, key);

	size_t str_len = json_object_get_string_len(key_obj);
	char *str = malloc(str_len + 1);
	strcpy(str, json_object_get_string(key_obj));

	return str;
}

static struct strlist
json_get_str_list(json_object const *json, char const *sct, char const *key,
                  char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_array))
		log_fail("in %s, '%s.%s' must be a string array", file, sct, key);

	struct strlist sl = strlist_create();

	for (size_t i = 0; i < json_object_array_length(key_obj); ++i) {
		json_object *arr_obj = json_object_array_get_idx(key_obj, i);
		if (!json_object_is_type(arr_obj, json_type_string))
			log_fail("in %s, '%s.%s' must only have strings", file, sct, key);

		size_t str_len = json_object_get_string_len(arr_obj);
		char const *str = json_object_get_string(arr_obj);
		strlist_add(&sl, str);
	}
	
	return sl;
}

static bool
json_get_bool(json_object const *json, char const *sct, char const *key,
              char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_boolean))
		log_fail("in %s, '%s.%s' must be a bool", file, sct, key);

	return json_object_get_boolean(key_obj);
}

static int
json_get_int(json_object const *json, char const *sct, char const *key,
             char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_int))
		log_fail("in %s, '%s.%s' must be an int", file, sct, key);

	return json_object_get_int(key_obj);
}

struct conf
conf_from_file(char const *file)
{
	FILE *file_p = fopen(file, "rb");
	if (!file_p)
		log_fail("cannot open file %s", file);

	fseek(file_p, 0, SEEK_END);
	size_t file_size = ftell(file_p);
	fseek(file_p, 0, SEEK_SET);

	char *file_conts = malloc(file_size + 1);
	fread(file_conts, 1, file_size, file_p);
	file_conts[file_size] = 0;
	
	json_object *json = json_tokener_parse(file_conts);
	if (!json)
		log_fail("malformed JSON in %s", file);
	
	struct conf conf;

	// first, extract only mandatory information for compilation to objects.
	conf.tc.cc = json_get_str(json, "tc", "cc", file);
	conf.tc.cflags = json_get_str(json, "tc", "cflags", file);
	conf.tc_info.cc_cmd_fmt = json_get_str(json, "tc-info", "cc-cmd-fmt", file);
	conf.tc_info.cc_inc_fmt = json_get_str(json, "tc-info", "cc-inc-fmt", file);
	conf.tc_info.cc_success_rc = json_get_int(json, "tc-info", "cc-success-rc", file);
	conf.proj.src_dir = json_get_str(json, "proj", "src-dir", file);
	conf.proj.inc_dir = json_get_str(json, "proj", "inc-dir", file);
	conf.proj.lib_dir = json_get_str(json, "proj", "lib-dir", file);
	conf.proj.produce_output = json_get_bool(json, "proj", "produce-output", file);
	conf.proj.src_exts = json_get_str_list(json, "proj", "src-exts", file);
	conf.proj.hdr_exts = json_get_str_list(json, "proj", "hdr-exts", file);
	conf.deps.incs = json_get_str_list(json, "deps", "incs", file);

	// then, if output should be produced, get necessary information for linker
	// to be run after compilation.
	if (conf.proj.produce_output) {
		conf.tc.ld = json_get_str(json, "tc", "ld", file);
		conf.tc.ldflags = json_get_str(json, "tc", "ldflags", file);
		conf.tc_info.ld_lib_fmt = json_get_str(json, "tc-info", "ld-lib-fmt", file);
		conf.tc_info.ld_obj_fmt = json_get_str(json, "tc-info", "ld-obj-fmt", file);
		conf.tc_info.ld_cmd_fmt = json_get_str(json, "tc-info", "ld-cmd-fmt", file);
		conf.tc_info.ld_success_rc = json_get_int(json, "tc-info", "ld-success-rc", file);
		conf.proj.output = json_get_str(json, "proj", "output", file);
		conf.deps.libs = json_get_str_list(json, "deps", "libs", file);
	}

	json_object_put(json);
	free(file_conts);
	fclose(file_p);
	
	return conf;
}

void
conf_validate(struct conf const *conf)
{
	struct stat s;
	
	// make sure project has specified directories.
	if (stat(conf->proj.src_dir, &s))
		log_fail("no source directory (%s)", conf->proj.src_dir);

	if (stat(conf->proj.inc_dir, &s)) {
		log_warn("no include directory (%s), creating", conf->proj.inc_dir);
		cmd_mkdir_p(conf->proj.inc_dir);
	}

	if (stat(conf->proj.lib_dir, &s)) {
		log_warn("no build directory (%s), creating", conf->proj.lib_dir);
		cmd_mkdir_p(conf->proj.lib_dir);
	}

	// make sure system has specified compiler and linker.
	if (stat(conf->tc.cc, &s))
		log_fail("could not find compiler (%s) on system", conf->tc.cc);

	if (conf->proj.produce_output && stat(conf->tc.ld, &s))
		log_fail("could not find linker (%s) on system", conf->tc.ld);
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
