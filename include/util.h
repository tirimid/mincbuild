#ifndef UTIL_H__
#define UTIL_H__

#include <stdbool.h>
#include <stddef.h>

struct string {
	char *str;
	size_t len, cap;
};

struct strlist {
	char **data;
	size_t size, cap;
};

struct fmt_spec_ent {
	char ch;
	void (*fn)(struct string *, void *);
};

struct fmt_spec {
	struct fmt_spec_ent *data;
	size_t size, cap;
};

void log_info(char const *fmt, ...);
void log_warn(char const *fmt, ...);
void log_fail(char const *fmt, ...);

struct string string_create(void);
void string_destroy(struct string *s);
void string_push_ch(struct string *s, char ch);
void string_push_str(struct string *s, char const *str);
char *string_to_str(struct string const *s);

struct strlist strlist_create(void);
void strlist_destroy(struct strlist *s);
struct strlist strlist_copy(struct strlist const *s);
void strlist_add(struct strlist *s, char const *new);
void strlist_rm(struct strlist *s, size_t ind);
void strlist_rm_no_free(struct strlist *s, size_t ind);
bool strlist_contains(struct strlist const *s, char const *str);

struct fmt_spec fmt_spec_create(void);
void fmt_spec_destroy(struct fmt_spec *f);
void fmt_spec_add_ent(struct fmt_spec *f, char ch,
                      void (*fn)(struct string *, void *));
struct string fmt_string(struct fmt_spec const *f, char const *fmt, void *data);
void fmt_inplace(struct string *out_str, struct fmt_spec const *f,
                 char const *fmt, void *data);
char *fmt_str(struct fmt_spec const *f, char const *fmt, void *data);

void cmd_mkdir_p(char const *dir);
char *sanitize_cmd(char const *cmd);

#endif
