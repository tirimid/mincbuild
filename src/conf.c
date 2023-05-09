#include "conf.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <json-c/json.h>

#include "util/log.h"
#include "util/arraylist.h"

static json_object *json_get(json_object const *json, char const *sct,
                             char const *key, char const *file)
{
	json_object *sct_obj;
	if (!json_object_object_get_ex(json, sct, &sct_obj))
		log_fail("%s must have section '%s'", file, sct);

	json_object *key_obj;
	if (!json_object_object_get_ex(sct_obj, key, &key_obj))
		log_fail("%s must have key '%s' in section '%s'", file, key, sct);
	
	return key_obj;
}

static char *json_get_str(json_object const *json, char const *sct,
                          char const *key, char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_string))
		log_fail("in %s, '%s.%s' must be a string", file, sct, key);

	size_t str_len = json_object_get_string_len(key_obj);
	char *str = malloc(str_len + 1);
	strcpy(str, json_object_get_string(key_obj));

	return str;
}

static struct arraylist json_get_str_list(json_object const *json,
                                          char const *sct, char const *key,
                                          char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_array))
		log_fail("in %s, '%s.%s' must be a string array", file, sct, key);

	struct arraylist str_list = arraylist_create();

	for (size_t i = 0; i < json_object_array_length(key_obj); ++i) {
		json_object *arr_obj = json_object_array_get_idx(key_obj, i);
		if (!json_object_is_type(arr_obj, json_type_string))
			log_fail("in %s, '%s.%s' must only be strings", file, sct, key);

		size_t str_len = json_object_get_string_len(arr_obj);
		char const *str = json_object_get_string(arr_obj);
		arraylist_add(&str_list, str, str_len + 1);
		*((char *)str_list.data[i] + str_len) = 0;
	}
	
	return str_list;
}

static bool json_get_bool(json_object const *json, char const *sct,
                          char const *key, char const *file)
{
	json_object *key_obj = json_get(json, sct, key, file);
	if (!json_object_is_type(key_obj, json_type_boolean))
		log_fail("in %s, '%s.%s' must be a bool", file, sct, key);

	return json_object_get_boolean(key_obj);
}

struct conf conf_from_file(char const *file)
{
	FILE *fp = fopen(file, "rb");
	if (!fp)
		log_fail("cannot open %s", file);
	
	fseek(fp, 0, SEEK_END);
	size_t flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char *fconts = malloc(flen + 1);
	fread(fconts, 1, flen, fp);
	fconts[flen] = 0;

	json_object *json = json_tokener_parse(fconts);
	if (!json)
		log_fail("malformed JSON in %s", file);
	
	struct conf conf;

	// first, extract only mandatory information for compilation to objects.
	conf.tc.cc = json_get_str(json, "tc", "cc", file);
	conf.tc.cflags = json_get_str(json, "tc", "cflags", file);
	conf.tc_info.cc_conly_flag = json_get_str(json, "tc-info", "cc-conly-flag", file);
	conf.tc_info.cc_inc_flag = json_get_str(json, "tc-info", "cc-inc-flag", file);
	conf.tc_info.cc_cobj_flag = json_get_str(json, "tc-info", "cc-cobj-flag", file);
	conf.proj.src_dir = json_get_str(json, "proj", "src-dir", file);
	conf.proj.inc_dir = json_get_str(json, "proj", "inc-dir", file);
	conf.proj.lib_dir = json_get_str(json, "proj", "lib-dir", file);
	conf.proj.produce_output = json_get_bool(json, "proj", "produce-output", file);
	conf.proj.src_exts = json_get_str_list(json, "proj", "src-exts", file);
	conf.proj.hdr_exts = json_get_str_list(json, "proj", "hdr-exts", file);

	// then, if output should be produced, get necessary information for linker
	// to be run after compilation.
	if (conf.proj.produce_output) {
		conf.tc.ld = json_get_str(json, "tc", "ld", file);
		conf.tc.ldflags = json_get_str(json, "tc", "ldflags", file);
		conf.tc_info.ld_lib_flag = json_get_str(json, "tc-info", "ld-lib-flag", file);
		conf.tc_info.ld_lbin_flag = json_get_str(json, "tc-info", "ld-lbin-flag", file);
		conf.proj.output = json_get_str(json, "proj", "output", file);
		conf.deps.incs = json_get_str_list(json, "deps", "incs", file);
		conf.deps.libs = json_get_str_list(json, "deps", "incs", file);
	}

	json_object_put(json);
	free(fconts);
	fclose(fp);
	
	return conf;
}

void conf_destroy(struct conf *conf)
{
	free(conf->tc.cc);
	free(conf->tc.cflags);
	free(conf->tc_info.cc_conly_flag);
	free(conf->tc_info.cc_inc_flag);
	free(conf->tc_info.cc_cobj_flag);
	free(conf->proj.src_dir);
	free(conf->proj.inc_dir);
	free(conf->proj.lib_dir);
	arraylist_destroy(&conf->proj.src_exts);
	arraylist_destroy(&conf->proj.hdr_exts);

	if (conf->proj.produce_output) {
		free(conf->tc.ld);
		free(conf->tc.ldflags);
		free(conf->tc_info.ld_lib_flag);
		free(conf->tc_info.ld_lbin_flag);
		free(conf->proj.output);
		arraylist_destroy(&conf->deps.incs);
		arraylist_destroy(&conf->deps.libs);
	}
}
