#ifndef UTIL_STRING_H__
#define UTIL_STRING_H__

#include <stddef.h>

struct string {
	char *text;
	size_t len, cap;
};

struct string string_create(void);
void string_destroy(struct string *s);
void string_push_ch(struct string *s, char ch);
void string_push_c_str(struct string *s, char const *c_str);
char *string_to_c_str(struct string const *s);

#endif
