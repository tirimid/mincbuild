#ifndef COMPILE_H
#define COMPILE_H

#include "conf.h"
#include "util.h"

void compile(struct conf const *conf, struct str_list const *srcs, struct str_list const *objs);

#endif
