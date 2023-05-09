#include "util/string.h"

#include <stdlib.h>
#include <string.h>

struct string string_create(void)
{
	return (struct string){
		.data = malloc(1),
		.len = 0,
		.cap = 1,
	};
}

void string_destroy(struct string *s)
{
	free(s->data);
}

void string_push_ch(struct string *s, char ch)
{
	if (s->len >= s->cap) {
		s->cap *= 2;
		s->data = realloc(s->data, s->cap);
	}

	s->data[s->len++] = ch;
}

void string_push_c_str(struct string *s, char const *c_str)
{
	for (char const *c = c_str; *c; ++c)
		string_push_ch(s, *c);
}

char *string_to_c_str(struct string const *s)
{
	char *c_str = malloc(s->len + 1);
	
	memcpy(c_str, s->data, s->len);
	c_str[s->len] = 0;

	return c_str;
}
