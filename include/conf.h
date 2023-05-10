#ifndef CONF_H__
#define CONF_H__

#include <stdbool.h>

#include "util.h"

struct conf {
	struct {
		char *cc, *ld;
		char *cflags, *ldflags;
	} tc;

	struct {
		char *cc_conly_flag, *cc_inc_flag, *cc_cobj_flag;
		char *ld_lib_flag, *ld_lbin_flag;
	} tc_info;

	struct {
		char *src_dir, *inc_dir, *lib_dir;
		bool produce_output;
		char *output;
		struct arraylist src_exts, hdr_exts;
	} proj;

	struct {
		struct arraylist incs;
		struct arraylist libs;
	} deps;
};

struct conf conf_from_file(char const *file);
void conf_validate(struct conf const *conf);
void conf_destroy(struct conf *conf);

#endif
