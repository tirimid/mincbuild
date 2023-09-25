#ifndef CONF_H__
#define CONF_H__

#include <stdbool.h>

#include "util.h"

struct conf {
	// toolchain.
	char *cc, *ld;
	char *cflags, *ldflags;

	// project.
	char *src_dir, *inc_dir, *lib_dir;
	bool produce_output;
	char *output;
	struct strlist src_exts, hdr_exts;

	// dependencies.
	struct strlist incs;
	struct strlist libs;

	// toolchain information.
	char *cc_inc_fmt, *ld_lib_fmt, *ld_obj_fmt;
	char *cc_cmd_fmt, *ld_cmd_fmt;
	int cc_success_rc, ld_success_rc;
};

struct conf conf_from_file(char const *file);
void conf_validate(struct conf const *conf);
void conf_destroy(struct conf *conf);

#endif
