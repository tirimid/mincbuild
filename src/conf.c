#include "conf.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

#define RAWKEY_BUF_SIZE 64
#define RAWVAL_BUF_SIZE 1024

static ssize_t getraw(FILE *fp, char const *key, char out_vbuf[]);
static char *getstr(FILE *fp, char const *key);
static struct strlist getstrlist(FILE *fp, char const *key);
static bool getbool(FILE *fp, char const *key);
static int getint(FILE *fp, char const *key);

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
	conf.cc = getstr(fp, "cc");
	conf.cflags = getstr(fp, "cflags");
	conf.cc_cmd_fmt = getstr(fp, "cc_cmd_fmt");
	conf.cc_inc_fmt = getstr(fp, "cc_inc_fmt");
	conf.cc_success_rc = getint(fp, "cc_success_rc");
	conf.src_dir = getstr(fp, "src_dir");
	conf.inc_dir = getstr(fp, "inc_dir");
	conf.lib_dir = getstr(fp, "lib_dir");
	conf.produce_output = getbool(fp, "produce_output");
	conf.src_exts = getstrlist(fp, "src_exts");
	conf.hdr_exts = getstrlist(fp, "hdr_exts");
	conf.incs = getstrlist(fp, "incs");

	// then, if output should be produced, get necessary information for
	// linker to be run after compilation.
	if (conf.produce_output) {
		conf.ld = getstr(fp, "ld");
		conf.ldflags = getstr(fp, "ldflags");
		conf.ld_lib_fmt = getstr(fp, "ld_lib_fmt");
		conf.ld_obj_fmt = getstr(fp, "ld_obj_fmt");
		conf.ld_cmd_fmt = getstr(fp, "ld_cmd_fmt");
		conf.ld_success_rc = getint(fp, "ld_success_rc");
		conf.output = getstr(fp, "output");
		conf.libs = getstrlist(fp, "libs");
	}

	fclose(fp);
	
	return conf;
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
	strlist_destroy(&conf->src_exts);
	strlist_destroy(&conf->hdr_exts);

	if (conf->produce_output) {
		free(conf->ld);
		free(conf->ldflags);
		free(conf->ld_lib_fmt);
		free(conf->ld_obj_fmt);
		free(conf->ld_cmd_fmt);
		free(conf->output);
		strlist_destroy(&conf->incs);
		strlist_destroy(&conf->libs);
	}
}

static ssize_t
getraw(FILE *fp, char const *key, char out_vbuf[])
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
		char kbuf[RAWKEY_BUF_SIZE];
		if (fscanf(fp, "%s = %[^\n]", kbuf, out_vbuf) != 2) {
			fprintf(stderr, "error on line %zu of configuration!\n", line);
			exit(1);
		}

		if (!strcmp(out_vbuf, "NONE"))
			out_vbuf[0] = 0;

		if (!strncmp(kbuf, key, RAWKEY_BUF_SIZE))
			return line;
	}

	return -1;
}

static char *
getstr(FILE *fp, char const *key)
{
	char vbuf[RAWVAL_BUF_SIZE];
	if (getraw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing string key in configuration: '%s'!\n", key);
		exit(1);
	}

	return strndup(vbuf, RAWVAL_BUF_SIZE);
}

static struct strlist
getstrlist(FILE *fp, char const *key)
{
	char vbuf[RAWVAL_BUF_SIZE];
	if (getraw(fp, key, vbuf) == -1) {
		fprintf(stderr, "missing stringlist key in configuration: '%s'!\n", key);
		exit(1);
	}

	struct strlist sl = strlist_create();
	char accum[RAWVAL_BUF_SIZE];
	size_t vbuflen = strlen(vbuf), accumlen = 0;
	for (size_t i = 0; i < vbuflen; ++i) {
		if (vbuf[i] == '\\') {
			accum[accumlen++] = vbuf[++i];
			continue;
		} else if (isspace(vbuf[i]) && accumlen > 0) {
			accum[accumlen] = 0;
			accumlen = 0;
			strlist_add(&sl, accum);
		} else
			accum[accumlen++] = vbuf[i];
	}

	// last item in strlist won't get pushed unless followed by trailing
	// whitespace.
	// fix for this behavior.
	if (accumlen > 0) {
		accum[accumlen] = 0;
		strlist_add(&sl, accum);
	}

	return sl;
}

static bool
getbool(FILE *fp, char const *key)
{
	char vbuf[RAWVAL_BUF_SIZE];
	if (getraw(fp, key, vbuf) == -1) {
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
getint(FILE *fp, char const *key)
{
	char vbuf[RAWVAL_BUF_SIZE];
	if (getraw(fp, key, vbuf) == -1) {
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
