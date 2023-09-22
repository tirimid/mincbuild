#ifndef PRUNE_H__
#define PRUNE_H__

#include "conf.h"
#include "util.h"

void prune(struct conf const *conf, struct strlist *srcs, struct strlist *objs,
           struct strlist const *hdrs);

#endif
