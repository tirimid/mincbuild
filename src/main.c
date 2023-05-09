#include <stdio.h>

#include "conf.h"

#define DEFAULT_CONF_FILE "mincbuild.json"

int main(int argc, char const *argv[])
{
	if (argc > 2) {
		printf("usage: %s [build config file]\n", argv[0]);
		return -1;
	}
	
	struct conf conf = conf_from_file(argc == 2 ? argv[1] : DEFAULT_CONF_FILE);
	
	conf_destroy(&conf);
	return 0;
}
