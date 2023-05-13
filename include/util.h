#ifndef UTIL_H__
#define UTIL_H__

#include <stddef.h>
#include <stdbool.h>

struct arraylist {
	void **data;
	size_t *data_sizes;
	size_t size, cap;
};

struct string {
	char *data;
	size_t len, cap;
};

struct arraylist arraylist_create(void);
void arraylist_destroy(struct arraylist *al);
void arraylist_add(struct arraylist *al, void const *new, size_t size);
void arraylist_rm(struct arraylist *al, size_t ind);
void arraylist_rm_no_free(struct arraylist *al, size_t ind);
struct arraylist arraylist_copy(struct arraylist const *al);
bool arraylist_contains(struct arraylist const *al, void const *item);

struct string string_create(void);
void string_destroy(struct string *s);
void string_push_ch(struct string *s, char ch);
void string_push_c_str(struct string *s, char const *c_str);
void string_push_c_str_n(struct string *s, ...);
char *string_to_c_str(struct string const *s);

void log_info(char const *fmt, ...);
void log_warn(char const *fmt, ...);
void log_fail(char const *fmt, ...);

char const *file_ext(char const *file);
char *file_read(char const *file, size_t *out_size);
void file_mkdir_p(char const *dir);
void file_rmdir(char const *dir);

char *sanitize_cmd(char const *cmd);

#endif
