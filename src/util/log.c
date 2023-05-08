#include "util/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void log_info(char const *fmt, ...)
{
	printf("\33[106;30m| info |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void log_warn(char const *fmt, ...)
{
	printf("\33[103;30m| warn |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
}

void log_fail(char const *fmt, ...)
{
	printf("\33[101;97m| fail |\33[0m ");
	
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	putc('\n', stdout);
	exit(-1);
}
