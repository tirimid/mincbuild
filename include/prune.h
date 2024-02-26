#ifndef PRUNE_H
#define PRUNE_H

#include "conf.h"
#include "util.h"

void prune(struct conf const *conf, struct str_list *srcs, struct str_list *objs, struct str_list const *hdrs);

#endif
