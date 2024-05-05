#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "compile.h"
#include "conf.h"
#include "link.h"
#include "prune.h"

#define DEFAULT_CONF "mincbuild.conf"

bool flag_r = false, flag_v = false;

static void usage(char const *name);

int
main(int argc, char const *argv[])
{
	int ch;
	while ((ch = getopt(argc, (char *const *)argv, "hrv")) != -1)
	{
		switch (ch)
		{
		case 'h':
			usage(argv[0]);
			return 0;
		case 'r':
			flag_r = true;
			break;
		case 'v':
			flag_v = true;
			break;
		default:
			return 1;
		}
	}

	int first_arg = 1;
	while (first_arg < argc
	       && *argv[first_arg] == '-'
	       && strcmp(argv[first_arg - 1], "--"))
	{
		++first_arg;
	}
	
	if (argc > first_arg + 1)
	{
		fprintf(stderr, "usage: %s [options] [build config]\n", argv[0]);
		return 1;
	}
	
	struct conf conf = conf_from_file(argc == first_arg + 1 ? argv[first_arg] : DEFAULT_CONF);
	conf_apply_overrides(&conf);
	conf_validate(&conf);
	
	struct str_list srcs = ext_find(conf.src_dir, &conf.src_exts);
	
	struct str_list hdrs;
	if (!flag_r)
		hdrs = ext_find(conf.inc_dir, &conf.hdr_exts);

	struct str_list objs = str_list_create();
	size_t src_dir_len = strlen(conf.src_dir);
	size_t lib_dir_len = strlen(conf.lib_dir);
	for (size_t i = 0; i < srcs.size; ++i)
	{
		char const *src = srcs.data[i] + src_dir_len;
		src += *src == '/';

		char *obj = malloc(lib_dir_len + strlen(src) + 4);
		sprintf(obj, "%s/%s.o", conf.lib_dir, src);
		str_list_add(&objs, obj);
		free(obj);
	}

	if (!flag_r)
	{
		prune(&conf, &srcs, &objs, &hdrs);
		str_list_destroy(&hdrs);
	}
	
	compile(&conf, &srcs, &objs);
	str_list_destroy(&srcs);
	str_list_destroy(&objs);

	if (conf.produce_output)
		link_objs(&conf);

	conf_destroy(&conf);
	
	return 0;
}

static void
usage(char const *name)
{
	printf("usage:\n"
	       "\t%s [options] [build config]\n"
	       "options:\n"
	       "\t-h  display this menu\n"
	       "\t-r  force rebuild by skipping pruning phase of build\n"
	       "\t-v  write verbose build information\n", name);
}
