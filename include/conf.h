#ifndef CONF_H__
#define CONF_H__

#include <stdbool.h>

#include <libtmcul/ds/arraylist.h>

// compile-time configuration.
// this information is used by mincbuild but is not important to the user.
// change this if, for example, you want to use ripgrep instead of grep for the
// build process.
#define CMD_GREP "/usr/bin/grep"
#define CMD_NPROC "/usr/bin/nproc"

struct conf {
	struct {
		char *cc, *ld;
		char *cflags, *ldflags;
	} tc;

	struct {
		char *cc_conly_flag, *cc_inc_flag, *cc_cobj_flag;
		char *ld_lib_flag, *ld_lbin_flag;
		int cc_success_rc, ld_success_rc;
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
