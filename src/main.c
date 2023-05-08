#include "util/log.h"

int main(void)
{
	log_info("log message");
	log_warn("log message");
	log_fail("log message");
	return 0;
}
