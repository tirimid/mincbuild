#include "build.h"

#include <fts.h>
#include <sys/stat.h>

struct build_info build_info_get(struct conf const *conf)
{
	struct build_info info = {
		.srcs = arraylist_create(),
		.hdrs = arraylist_create(),
		.objs = arraylist_create(),
	};

	return info;
}

void build_info_destroy(struct build_info *info)
{
	arraylist_destroy(&info->srcs);
	arraylist_destroy(&info->hdrs);
	arraylist_destroy(&info->objs);
}

void build_prune(struct build_info *info)
{
}

void build_compile(struct conf const *conf, struct build_info const *info)
{
}

void build_link(struct conf const *conf, struct build_info const *info)
{
}
