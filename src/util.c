#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/stat.h>
#include <tmcul/ds/string.h>

#define ESCAPABLE " \t\n\v\f\r\\'\"<>;"
#define SANITIZE_ESCAPE "'\"<>;"

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

void
cmd_rmdir(char const *dir)
{
	struct stat s;
	if (stat(dir, &s) || !S_ISDIR(s.st_mode))
		return;
	
	char *cmd = malloc(7 + strlen(dir));
	sprintf(cmd, "rmdir %s", dir);
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
	
	char *san_cmd_cs = string_to_c_str(&san_cmd);
	string_destroy(&san_cmd);
	return san_cmd_cs;
}
