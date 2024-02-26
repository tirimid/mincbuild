#include "conf.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

#define RAW_KEY_BUF_SIZE 64
#define RAW_VAL_BUF_SIZE 1024
#define SCAN_FMT "%64s = %1024[^\r\n]"

static ssize_t get_raw(FILE *fp, char const *key, char out_vbuf[]);
static char *get_str(FILE *fp, char const *key);
static struct str_list get_str_list(FILE *fp, char const *key);
static bool get_bool(FILE *fp, char const *key);
static int get_int(FILE *fp, char const *key);

struct conf
conf_from_file(char const *file)
{
	FILE *fp = fopen(file, "rb");
	if (!fp) {
		fprintf(stderr, "cannot open file: '%s'!\n", file);
		exit(1);
	}
	
	struct conf conf;

	// first, extract only mandatory information for compilation to objects.
	conf.cc = get_str(fp, "cc");
	conf.cflags = get_str(fp, "cflags");
	conf.cc_cmd_fmt = get_str(fp, "cc_cmd_fmt");
	conf.cc_inc_fmt = get_str(fp, "cc_inc_fmt");
	conf.cc_success_rc = get_int(fp, "cc_success_rc");
	conf.src_dir = get_str(fp, "src_dir");
	conf.inc_dir = get_str(fp, "inc_dir");
	conf.lib_dir = get_str(fp, "lib_dir");
	conf.produce_output = get_bool(fp, "produce_output");
	conf.src_exts = get_str_list(fp, "src_exts");
	conf.hdr_exts = get_str_list(fp, "hdr_exts");
	conf.incs = get_str_list(fp, "incs");

	// then, if output should be produced, get necessary information for
	// linker to be run after compilation.
	if (conf.produce_output) {
		conf.ld = get_str(fp, "ld");
		conf.ldflags = get_str(fp, "ldflags");
		conf.ld_lib_fmt = get_str(fp, "ld_lib_fmt");
		conf.ld_obj_fmt = get_str(fp, "ld_obj_fmt");
		conf.ld_cmd_fmt = get_str(fp, "ld_cmd_fmt");
		conf.ld_success_rc = get_int(fp, "ld_success_rc");
		conf.output = get_str(fp, "output");
		conf.libs = get_str_list(fp, "libs");
	}

	fclose(fp);
	
	return conf;
}

void
conf_apply_overrides(struct conf *conf)
{
	// this is mainly done to allow the user to more easily configure builds
	// than manually editing the conf.
	
	char const *cc = secure_getenv("CC");
	if (cc) {
		free(conf->cc);
		conf->cc = strdup(cc);
	}
	
	char const *cflags = secure_getenv("CFLAGS");
	if (cflags) {
		free(conf->cflags);
		conf->cflags = strdup(cflags);
	}
	
	if (!conf->produce_output)
		return;
	
	char const *ld = secure_getenv("LD");
	if (ld) {
		free(conf->ld);
		conf->ld = strdup(ld);
	}
	
	char const *ldflags = secure_getenv("LDFLAGS");
	if (ldflags) {
		free(conf->ldflags);
		conf->ldflags = strdup(ldflags);
	}
}

void
conf_validate(struct conf const *conf)
{
	struct stat s;
	
	// make sure project has specified directories.
	if (stat(conf->src_dir, &s)) {
		fprintf(stderr, "no source directory: '%s'!\n", conf->src_dir);
		exit(1);
	}

	if (stat(conf->inc_dir, &s)) {
		fprintf(stderr, "no include directory, creating: '%s'\n", conf->inc_dir);
		mkdir_recursive(conf->inc_dir);
	}

	if (stat(conf->lib_dir, &s)) {
		fprintf(stderr, "no build directory, creating: '%s'\n", conf->lib_dir);
		mkdir_recursive(conf->lib_dir);
	}

	// make sure system has specified compiler and linker.
	if (stat(conf->cc, &s)) {
		fprintf(stderr, "compiler not present on system: '%s'!\n", conf->cc);
		exit(1);
	}

	if (conf->produce_output && stat(conf->ld, &s)) {
		fprintf(stderr, "linker not present on system: '%s'!\n", conf->ld);
		exit(1);
	}
}

void
conf_destroy(struct conf *conf)
{
	free(conf->cc);
	free(conf->cflags);
	free(conf->cc_cmd_fmt);
	free(conf->cc_inc_fmt);
	free(conf->src_dir);
	free(conf->inc_dir);
	free(conf->lib_dir);
	str_list_destroy(&conf->src_exts);
	str_list_destroy(&conf->hdr_exts);

	if (conf->produce_output) {
		free(conf->ld);
		free(conf->ldflags);
		free(conf->ld_lib_fmt);
		free(conf->ld_obj_fmt);
		free(conf->ld_cmd_fmt);
		free(conf->output);
		str_list_destroy(&conf->incs);
		str_list_destroy(&conf->libs);
	}
}

static ssize_t
get_raw(FILE *fp, char const *key, char out_vbuf[])
{
	fseek(fp, 0, SEEK_SET);

	for (size_t line = 1; !feof(fp) && !ferror(fp); ++line) {
		int ch;
		while ((ch = fgetc(fp)) != EOF && isspace(ch));
		if (ch == '#')
			while ((ch = fgetc(fp)) != EOF && ch != '\n');
		if (ch == '\n' || feof(fp))
			continue;

		fseek(fp, -1, SEEK_CUR);
		char kbuf[RAW_KEY_BUF_SIZE];
		if (fscanf(fp, SCAN_FMT, kbuf, out_vbuf) != 2) {
			fprintf(stderr, "error on line %zu of configuration!\n", line);
			exit(1);
		}

		if (!strcmp(out_vbuf, "NONE"))
			out_vbuf[0] = 0;

		if (!strncmp(kbuf, key, RAW_KEY_BUF_SIZE))
			return line;
	}

	return -1;
}

static char *
get_str(FILE *fp, char const *key)
{
	char vbuf[RAW_VAL_BUF_SIZE];
	if (get_raw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing string key in configuration: '%s'!\n", key);
		exit(1);
	}

	return strndup(vbuf, RAW_VAL_BUF_SIZE);
}

static struct str_list
get_str_list(FILE *fp, char const *key)
{
	char vbuf[RAW_VAL_BUF_SIZE];
	if (get_raw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing stringlist key in configuration: '%s'!\n", key);
		exit(1);
	}

	struct str_list sl = str_list_create();
	char accum[RAW_VAL_BUF_SIZE];
	size_t vbuflen = strlen(vbuf), accumlen = 0;
	for (size_t i = 0; i < vbuflen; ++i) {
		if (vbuf[i] == '\\') {
			accum[accumlen++] = vbuf[++i];
			continue;
		} else if (isspace(vbuf[i]) && accumlen > 0) {
			accum[accumlen] = 0;
			accumlen = 0;
			str_list_add(&sl, accum);
		} else
			accum[accumlen++] = vbuf[i];
	}

	// last item in strlist won't get pushed unless followed by trailing
	// whitespace.
	// fix for this behavior.
	if (accumlen > 0) {
		accum[accumlen] = 0;
		str_list_add(&sl, accum);
	}

	return sl;
}

static bool
get_bool(FILE *fp, char const *key)
{
	char vbuf[RAW_VAL_BUF_SIZE];
	if (get_raw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing bool key in configuration: '%s'!\n", key);
		exit(1);
	}

	if (!strcmp("true", vbuf))
		return true;
	else if (!strcmp("false", vbuf))
		return false;
	else {
		fprintf(stderr, "invalid bool value for %s: '%s'!\n", key, vbuf);
		exit(1);
	}
}

static int
get_int(FILE *fp, char const *key)
{
	char vbuf[RAW_VAL_BUF_SIZE];
	if (get_raw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing int key in configuration: '%s'!\n", key);
		exit(1);
	}

	for (char const *c = vbuf; *c; ++c) {
		if (!strchr("0123456789-", *c)) {
			fprintf(stderr, "invalid int value for %s: '%s'!\n", key, vbuf);
			exit(1);
		}
	}

	return atoi(vbuf);
}
