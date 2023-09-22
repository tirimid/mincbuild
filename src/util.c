#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fts.h>
#include <sys/stat.h>

#define SANITIZE_ESCAPE " \t\n\v\f\r\\'\"<>;"
#define FMT_SPEC_CH '%'

struct string
string_create(void)
{
	return (struct string){
		.str = malloc(1),
		.len = 0,
		.cap = 1,
	};
}

void
string_destroy(struct string *s)
{
	free(s->str);
}

void
string_push_ch(struct string *s, char ch)
{
	if (s->len >= s->cap) {
		s->cap *= 2;
		s->str = realloc(s->str, s->cap);
	}

	s->str[s->len++] = ch;
}

void
string_push_str(struct string *s, char const *str)
{
	for (char const *c = str; *c; ++c)
		string_push_ch(s, *c);
}

char *
string_to_str(struct string const *s)
{
	return strndup(s->str, s->len);
}

struct strlist
strlist_create(void)
{
	return (struct strlist){
		.data = malloc(sizeof(char *)),
		.size = 0,
		.cap = 1,
	};
}

void
strlist_destroy(struct strlist *s)
{
	for (size_t i = 0; i < s->size; ++i)
		free(s->data[i]);

	free(s->data);
}

struct strlist
strlist_copy(struct strlist const *s)
{
	struct strlist cp = strlist_create();

	for (size_t i = 0; i < s->size; ++i)
		strlist_add(&cp, s->data[i]);

	return cp;
}

void
strlist_add(struct strlist *s, char const *new)
{
	if (s->size >= s->cap) {
		s->cap *= 2;
		s->data = realloc(s->data, sizeof(char *) * s->cap);
	}

	s->data[s->size++] = strdup(new);
}

void
strlist_rm(struct strlist *s, size_t ind)
{
	free(s->data[ind]);
	strlist_rm_no_free(s, ind);
}

void
strlist_rm_no_free(struct strlist *s, size_t ind)
{
	size_t mv_size = (s->size-- - ind) * sizeof(char *);
	memmove(s->data + ind, s->data + ind + 1, mv_size);
}

bool
strlist_contains(struct strlist const *s, char const *str)
{
	for (size_t i = 0; i < s->size; ++i) {
		if (!strcmp(str, s->data[i]))
			return true;
	}

	return false;
}

struct fmt_spec
fmt_spec_create(void)
{
	return (struct fmt_spec){
		.data = malloc(sizeof(struct fmt_spec_ent)),
		.size = 0,
		.cap = 1,
	};
}

void
fmt_spec_destroy(struct fmt_spec *f)
{
	free(f->data);
}

void
fmt_spec_add_ent(struct fmt_spec *f, char ch,
                 void (*fn)(struct string *, void *))
{
	if (f->size >= f->cap) {
		f->cap *= 2;
		f->data = realloc(f->data, sizeof(struct fmt_spec_ent) * f->cap);
	}

	f->data[f->size++] = (struct fmt_spec_ent){
		.ch = ch,
		.fn = fn,
	};
}

struct string
fmt_string(struct fmt_spec const *f, char const *fmt, void *data)
{
	struct string s = string_create();
	fmt_inplace(&s, f, fmt, data);
	return s;
}

void
fmt_inplace(struct string *out_str, struct fmt_spec const *f, char const *fmt,
            void *data)
{
	for (size_t i = 0, fmt_len = strlen(fmt), j; i < fmt_len; ++i) {
		if (fmt[i] != FMT_SPEC_CH) {
			string_push_ch(out_str, fmt[i]);
			continue;
		}

		++i;
		for (j = 0; j < f->size; ++j) {
			if (!fmt[i] || fmt[i] == FMT_SPEC_CH) {
				string_push_ch(out_str, FMT_SPEC_CH);
				break;
			} else if (fmt[i] == f->data[j].ch) {
				f->data[j].fn(out_str, data);
				break;
			}
		}

		if (j == f->size)
			--i;
	}
}

char *
fmt_str(struct fmt_spec const *f, char const *fmt, void *data)
{
	struct string s = fmt_string(f, fmt, data);
	char *str = string_to_str(&s);
	string_destroy(&s);

	return str;
}

void
mkdir_recursive(char const *dir)
{
	struct string path_build = string_create();

	for (char const *c = dir; *c; ++c) {
		if (*c == '/') {
			char *pstr = string_to_str(&path_build);
			mkdir(pstr, S_IRWXU | S_IRWXG | S_IRWXO);
			free(pstr);
		}

		string_push_ch(&path_build, *c);
	}

	string_destroy(&path_build);
}

char *
sanitize_path(char const *path)
{
	struct string san_path = string_create();

	for (char const *c = path; *c; ++c) {
		if (strchr(SANITIZE_ESCAPE, *c))
			string_push_ch(&san_path, '\\');
		
		string_push_ch(&san_path, *c);
	}
	
	char *san_path_str = string_to_str(&san_path);
	string_destroy(&san_path);
	
	return san_path_str;
}

struct strlist
extfind(char *dir, struct strlist const *exts)
{
	unsigned long fts_opts = FTS_LOGICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
	char *const fts_dirs[] = {dir, NULL};
	FTS *fts_p = fts_open(fts_dirs, fts_opts, NULL);

	if (!fts_p) {
		fputs("failed to fts_open()!\n", stderr);
		exit(1);
	}

	struct strlist files = strlist_create();

	if (!fts_children(fts_p, 0))
		return files;

	FTSENT *fts_ent;
	while (fts_ent = fts_read(fts_p)) {
		if (fts_ent->fts_info != FTS_F)
			continue;

		char const *ext = strrchr(fts_ent->fts_path, '.');
		ext = ext && ext != fts_ent->fts_path ? ext + 1 : "\0";

		if (strlist_contains(exts, ext))
			strlist_add(&files, fts_ent->fts_path);
	}

	fts_close(fts_p);
	return files;
}
