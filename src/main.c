#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"
#include "conf.h"
#include "link.h"
#include "prune.h"

#define DEFAULT_CONF "mincbuild.conf"

int
main(int argc, char const *argv[])
{
	if (argc > 2) {
		fprintf(stderr, "usage: %s [build config file]\n", argv[0]);
		return 1;
	}
	
	struct conf conf = conf_from_file(argc == 2 ? argv[1] : DEFAULT_CONF);
	conf_validate(&conf);
	
	struct strlist srcs = extfind(conf.src_dir, &conf.src_exts);
	struct strlist hdrs = extfind(conf.inc_dir, &conf.hdr_exts);

	struct strlist objs = strlist_create();
	size_t src_dir_len = strlen(conf.src_dir);
	size_t lib_dir_len = strlen(conf.lib_dir);
	for (size_t i = 0; i < srcs.size; ++i) {
		char const *src = srcs.data[i] + src_dir_len;
		src += *src == '/';

		char *obj = malloc(lib_dir_len + strlen(src) + 4);
		sprintf(obj, "%s/%s.o", conf.lib_dir, src);
		strlist_add(&objs, obj);
		free(obj);
	}
	
	prune(&conf, &srcs, &objs, &hdrs);
	strlist_destroy(&hdrs);
	
	compile(&conf, &srcs, &objs);
	strlist_destroy(&srcs);
	strlist_destroy(&objs);

	if (conf.produce_output)
		linkobjs(&conf);

	conf_destroy(&conf);
	
	return 0;
}
