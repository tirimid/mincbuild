#include <stdio.h>

#include "conf.h"
#include "build.h"

#define DEFAULT_CONF_FILE "mincbuild.json"

int
main(int argc, char const *argv[])
{
	if (argc > 2) {
		printf("usage: %s [build config file]\n", argv[0]);
		return -1;
	}
	
	struct conf conf = conf_from_file(argc == 2 ? argv[1] : DEFAULT_CONF_FILE);
	conf_validate(&conf);
	
	struct build_info info = build_info_get(&conf);
	build_prune(&conf, &info);
	build_compile(&conf, &info);
	if (conf.proj.produce_output)
		build_link(&conf, &info);

	build_info_destroy(&info);
	conf_destroy(&conf);
	
	return 0;
}
