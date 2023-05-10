#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define ESCAPABLE " \t\n\v\f\r\\'\""

struct arraylist arraylist_create(void)
{
	return (struct arraylist){
		.data = malloc(sizeof(void *)),
		.data_sizes = malloc(sizeof(size_t)),
		.size = 0,
		.cap = 1,
	};
}

void arraylist_destroy(struct arraylist *al)
{
	for (size_t i = 0; i < al->size; ++i)
		free(al->data[i]);
	
	free(al->data);
	free(al->data_sizes);
}

void arraylist_add(struct arraylist *al, void const *new, size_t size)
{
	if (al->size >= al->cap) {
		al->cap *= 2;
		al->data = realloc(al->data, sizeof(void *) * al->cap);
		al->data_sizes = realloc(al->data_sizes, sizeof(void *) * al->cap);
	}

	al->data[al->size] = malloc(size);
	al->data_sizes[al->size] = size;
	memcpy(al->data[al->size], new, size);
	++al->size;
}

void arraylist_rm(struct arraylist *al, size_t ind)
{
	free(al->data[ind]);
	size_t mv_size = (al->size-- - ind) * sizeof(void *);
	memmove(al->data + ind, al->data + ind + 1, mv_size);
}

struct arraylist arraylist_copy(struct arraylist const *al)
{
	struct arraylist cp = arraylist_create();

	for (size_t i = 0; i < al->size; ++i)
		arraylist_add(&cp, al->data[i], al->data_sizes[i]);

	return cp;
}

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

void log_info(char const *fmt, ...)
{
	printf("\33[106;30m| info |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void log_warn(char const *fmt, ...)
{
	printf("\33[103;30m| warn |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void log_fail(char const *fmt, ...)
{
	printf("\33[101;97m| fail |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
	exit(-1);
}

char const *file_ext(char const *file)
{
}

char *file_read(char const *file, size_t *out_size)
{
	FILE *fp = fopen(file, "rb");
	if (!fp)
		log_fail("cannot open file: %s", file);

	fseek(fp, 0, SEEK_END);
	size_t flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char *fconts = malloc(flen + 1);
	fread(fconts, 1, flen, fp);
	fconts[flen] = 0;
	
	if (out_size)
		*out_size = flen;

	fclose(fp);

	return fconts;
}

void file_mkdir_p(char const *dir)
{
	char *cmd = malloc(10 + strlen(dir));
	sprintf(cmd, "mkdir -p %s", dir);
	safe_system(cmd);
	free(cmd);
}

void file_rmdir(char const *dir)
{
	char *cmd = malloc(7 + strlen(dir));
	sprintf(cmd, "rmdir %s", dir);
	safe_system(cmd);
	free(cmd);
}

int safe_system(char const *cmd)
{
	size_t cmd_len = strlen(cmd);
	struct string cmd_real = string_create();

	for (size_t i = 0; i < cmd_len; ++i) {
		if (cmd[i] == ';')
			string_push_c_str(&cmd_real, "\\;");
		else if (cmd[i] == '\'' || cmd[i] == '"') {
			string_push_ch(&cmd_real, '\\');
			string_push_ch(&cmd_real, cmd[i]);
		} else if (cmd[i] == '\\') {
			char next = cmd[i + 1];
			string_push_ch(&cmd_real, '\\');
			string_push_ch(&cmd_real, strchr(ESCAPABLE, next) ? next : '\\');
			++i;
		} else
			string_push_ch(&cmd_real, cmd[i]);
	}
	
	char *cmd_real_cs = string_to_c_str(&cmd_real);
	int rc = system(cmd_real_cs);
	string_destroy(&cmd_real);
	free(cmd_real_cs);

	return rc;
}
