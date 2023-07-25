#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#define ESCAPABLE " \t\n\v\f\r\\'\"<>;"
#define SANITIZE_ESCAPE "'\"<>;"
#define FMT_SPEC_CH '%'

void
log_info(char const *fmt, ...)
{
	printf("%s", "\033[1;36minfo\033[0m: ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void
log_warn(char const *fmt, ...)
{
	printf("%s", "\033[1;33mwarn\033[0m: ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void
log_fail(char const *fmt, ...)
{
	printf("%s", "\033[1;31mfail\033[0m: ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
	exit(1);
}

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
cmd_mkdir_p(char const *dir)
{
	struct stat s;
	if (!stat(dir, &s))
		return;
	
	char *cmd = malloc(10 + strlen(dir));
	sprintf(cmd, "mkdir -p %s", dir);
	char *san_cmd = sanitize_cmd(cmd);
	system(san_cmd);
	
	free(san_cmd);
	free(cmd);
}

char *
sanitize_cmd(char const *cmd)
{
	size_t cmd_len = strlen(cmd);
	struct string san_cmd = string_create();

	for (size_t i = 0; i < cmd_len; ++i) {
		if (strchr(SANITIZE_ESCAPE, cmd[i])) {
			string_push_ch(&san_cmd, '\\');
			string_push_ch(&san_cmd, cmd[i]);
		} else if (cmd[i] == '\\') {
			char next = cmd[i + 1];
			string_push_ch(&san_cmd, '\\');
			string_push_ch(&san_cmd, strchr(ESCAPABLE, next) ? next : '\\');
			i += strchr(ESCAPABLE, next) ? 1 : 0;
		} else
			string_push_ch(&san_cmd, cmd[i]);
	}
	
	char *san_cmd_cs = string_to_str(&san_cmd);
	string_destroy(&san_cmd);
	return san_cmd_cs;
}
