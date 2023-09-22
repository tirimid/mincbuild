#ifndef COMPILE_H__
#define COMPILE_H__

#include "conf.h"
#include "util.h"

void compile(struct conf const *conf, struct strlist const *srcs, struct strlist const *objs);

#endif
