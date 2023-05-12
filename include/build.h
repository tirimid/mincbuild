#ifndef BUILD_H__
#define BUILD_H__

#include "conf.h"
#include "util.h"

struct build_info {
	struct arraylist srcs, objs, hdrs;
};

struct build_info build_info_get(struct conf const *conf);
void build_info_destroy(struct build_info *info);

void build_prune(struct conf const *conf, struct build_info *info);
void build_compile(struct conf const *conf, struct build_info const *info);
void build_link(struct conf const *conf, struct build_info const *info);

#endif
