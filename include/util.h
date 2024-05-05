#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>

struct string
{
	char *str;
	size_t len, cap;
};

struct str_list
{
	char **data;
	size_t size, cap;
};

struct fmt_spec_ent
{
	char ch;
	void (*fn)(struct string *, void *);
};

struct fmt_spec
{
	struct fmt_spec_ent *data;
	size_t size, cap;
};

struct string string_create(void);
void string_destroy(struct string *s);
void string_push_ch(struct string *s, char ch);
void string_push_str(struct string *s, char const *str);
char *string_to_str(struct string const *s);

struct str_list str_list_create(void);
void str_list_destroy(struct str_list *s);
struct str_list str_list_copy(struct str_list const *s);
void str_list_add(struct str_list *s, char const *new);
void str_list_rm(struct str_list *s, size_t ind);
void str_list_rm_no_free(struct str_list *s, size_t ind);
bool str_list_contains(struct str_list const *s, char const *str);

struct fmt_spec fmt_spec_create(void);
void fmt_spec_destroy(struct fmt_spec *f);
void fmt_spec_add_ent(struct fmt_spec *f, char ch, void (*fn)(struct string *, void *));
struct string fmt_string(struct fmt_spec const *f, char const *fmt, void *data);
void fmt_inplace(struct string *out_str, struct fmt_spec const *f, char const *fmt, void *data);
char *fmt_str(struct fmt_spec const *f, char const *fmt, void *data);

void mkdir_recursive(char const *dir);
char *sanitize_path(char const *path);
struct str_list ext_find(char *dir, struct str_list const *exts);

#endif
